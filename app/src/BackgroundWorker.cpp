#include <QCoreApplication>
#include <QDBusConnection>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <optional>

extern "C" {
#include "omachess_core.h"
}

namespace {

struct EngineLaunch {
    QString program;
    QStringList arguments;
    QString workingDirectory;
};

struct Position {
    uint ply = 0;
    QString fen;
};

int deadlineMs()
{
    bool ok = false;
    const int value = qEnvironmentVariableIntValue("OMACHESS_TEST_ENGINE_DEADLINE_MS", &ok);
    return ok && value > 0 ? value : 3000;
}

QString engineStoreDirectory(const QString &key)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("omachess/engines/%1").arg(key));
}

QString discoverExecutable(const QString &key, const QStringList &names)
{
    QStringList candidates;
    for (const QString &name : names)
        candidates.append(QDir(engineStoreDirectory(key)).filePath(name));
    for (const QString &name : names)
        candidates.append(QDir(QStringLiteral("/usr/bin")).filePath(name));

    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable())
            return info.canonicalFilePath();
    }
    return {};
}

std::optional<EngineLaunch> consentedEngine()
{
    QSettings settings;
    const QString custom = settings.value(QStringLiteral("engines/custom/path")).toString();
    if (!custom.isEmpty()) {
        const QFileInfo info(custom);
        const QString canonical = info.canonicalFilePath();
        const bool consented =
            settings.value(QStringLiteral("engines/custom/consent/%1").arg(custom), false).toBool()
            || settings.value(QStringLiteral("engines/custom/consent/%1").arg(canonical), false).toBool();
        if (!canonical.isEmpty()
            && info.isFile()
            && info.isExecutable()
            && consented) {
            return EngineLaunch{
                canonical,
                QProcess::splitCommand(settings.value(QStringLiteral("engines/custom/arguments")).toString()),
                settings.value(QStringLiteral("engines/custom/workingDirectory")).toString(),
            };
        }
    }

    const QList<QPair<QString, QStringList>> profiles{
        {QStringLiteral("stockfish"), {QStringLiteral("stockfish")}},
        {QStringLiteral("leela"), {QStringLiteral("lc0"), QStringLiteral("leelaz")}},
        {QStringLiteral("reckless"), {QStringLiteral("reckless")}},
        {QStringLiteral("komodo"), {QStringLiteral("komodo"), QStringLiteral("komodo-generic")}},
    };
    for (const auto &profile : profiles) {
        const QString path = discoverExecutable(profile.first, profile.second);
        if (!path.isEmpty()
            && settings.value(QStringLiteral("engines/%1/consent/%2").arg(profile.first, path), false).toBool()) {
            return EngineLaunch{path, {}, {}};
        }
    }
    return std::nullopt;
}

} // namespace

class AnalysisRunner final : public QObject {
    Q_OBJECT
public:
    AnalysisRunner(const QString &id, uint checkpoint, QObject *parent = nullptr)
        : QObject(parent)
        , m_id(id)
        , m_nextIndex(checkpoint)
    {
        connect(&m_process, &QProcess::started, this, [this] {
            if (m_stopping)
                return;
            m_stage = Stage::Uci;
            send("uci\n");
            m_deadline.start(deadlineMs());
        });
        connect(&m_process, &QProcess::readyReadStandardOutput, this, &AnalysisRunner::readOutput);
        connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (!m_stopping && error != QProcess::UnknownError)
                fail();
        });
        connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                this, [this](int, QProcess::ExitStatus) {
                    if (m_stopping)
                        return;
                    if (m_stage == Stage::Stopping) {
                        checkpointAndContinue();
                        return;
                    }
                    fail();
                });
        m_deadline.setSingleShot(true);
        connect(&m_deadline, &QTimer::timeout, this, &AnalysisRunner::fail);
    }

    bool start()
    {
        const QByteArray job = m_id.toUtf8();
        char *positionsJson = omachess_background_job_positions_json(job.constData());
        if (!positionsJson)
            return false;
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray(positionsJson));
        omachess_string_free(positionsJson);
        if (!document.isArray())
            return false;
        const QJsonArray positions = document.array();
        if (m_nextIndex > static_cast<uint>(positions.size()))
            return false;
        for (const QJsonValue &value : positions) {
            const QJsonObject object = value.toObject();
            m_positions.append(Position{
                static_cast<uint>(object.value(QStringLiteral("ply")).toInt()),
                object.value(QStringLiteral("fen")).toString(),
            });
        }
        if (!loadSavedEvaluations())
            return false;
        const auto launch = consentedEngine();
        if (!launch)
            return false;
        m_launch = *launch;
        startNext();
        return true;
    }

    void stop()
    {
        m_stopping = true;
        m_deadline.stop();
        if (m_process.state() != QProcess::NotRunning)
            m_process.kill();
    }

signals:
    void finished(const QString &id, bool success);

private:
    enum class Stage { Idle, Uci, Ready, Search, Stopping };

    void startNext()
    {
        if (m_nextIndex >= static_cast<uint>(m_positions.size())) {
            const QByteArray job = m_id.toUtf8();
            const QByteArray payload =
                QJsonDocument(m_evaluations).toJson(QJsonDocument::Compact);
            emit finished(m_id, omachess_background_job_complete_with_payload(job.constData(),
                                                                              payload.constData()));
            return;
        }
        m_output.clear();
        m_evaluation.clear();
        m_variations.clear();
        m_stage = Stage::Idle;
        m_process.setProgram(m_launch.program);
        m_process.setArguments(m_launch.arguments);
        if (!m_launch.workingDirectory.isEmpty())
            m_process.setWorkingDirectory(m_launch.workingDirectory);
        m_process.start();
        m_deadline.start(deadlineMs());
    }

    void send(const QByteArray &command)
    {
        m_process.write(command);
    }

    void readOutput()
    {
        m_output.append(m_process.readAllStandardOutput());
        int newline = -1;
        while ((newline = m_output.indexOf('\n')) >= 0) {
            const QString line = QString::fromUtf8(m_output.left(newline)).trimmed();
            m_output.remove(0, newline + 1);
            consumeLine(line);
        }
    }

    void consumeLine(const QString &line)
    {
        if (line.isEmpty())
            return;
        if (m_stage == Stage::Uci && line == QStringLiteral("uciok")) {
            m_stage = Stage::Ready;
            send("isready\n");
            m_deadline.start(deadlineMs());
            return;
        }
        if (m_stage == Stage::Ready && line == QStringLiteral("readyok")) {
            m_stage = Stage::Search;
            const Position &position = m_positions.at(static_cast<int>(m_nextIndex));
            send("position fen " + position.fen.toUtf8() + "\ngo movetime 250\n");
            m_deadline.start(deadlineMs());
            return;
        }
        if (m_stage != Stage::Search)
            return;
        consumeAnalysisInfo(line);
        if (line.startsWith(QStringLiteral("bestmove ")))
            finishPosition();
    }

    void consumeAnalysisInfo(const QString &line)
    {
        static const QRegularExpression scorePattern(QStringLiteral("(?:^| )score (cp|mate) (-?\\d+)"));
        static const QRegularExpression pvPattern(QStringLiteral("(?:^| )pv (.+)$"));
        static const QRegularExpression rankPattern(QStringLiteral("(?:^| )multipv (\\d+)"));
        const QRegularExpressionMatch scoreMatch = scorePattern.match(line);
        const QRegularExpressionMatch pvMatch = pvPattern.match(line);
        if (!scoreMatch.hasMatch() || !pvMatch.hasMatch())
            return;
        const QRegularExpressionMatch rankMatch = rankPattern.match(line);
        const int rank = rankMatch.hasMatch() ? rankMatch.captured(1).toInt() : 1;
        if (rank == 1) {
            const int score = scoreMatch.captured(2).toInt();
            if (scoreMatch.captured(1) == QStringLiteral("mate"))
                m_evaluation = score >= 0 ? QStringLiteral("#%1").arg(score)
                                          : QStringLiteral("-#%1").arg(-score);
            else
                m_evaluation = QStringLiteral("%1%2")
                    .arg(score >= 0 ? QStringLiteral("+") : QString())
                    .arg(score / 100.0, 0, 'f', 2);
        }
        m_variations.insert(rank, pvMatch.captured(1));
    }

    void finishPosition()
    {
        const Position &position = m_positions.at(static_cast<int>(m_nextIndex));
        QJsonObject evaluation{
            {QStringLiteral("ply"), static_cast<int>(position.ply)},
            {QStringLiteral("position_fen"), position.fen},
            {QStringLiteral("evaluation"), m_evaluation.isEmpty() ? QStringLiteral("0.00") : m_evaluation},
            {QStringLiteral("glyph"), QString()},
        };
        const QString bestLine = m_variations.value(1);
        evaluation.insert(QStringLiteral("better_line"),
                          bestLine.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(bestLine));
        m_evaluations.append(evaluation);
        m_stage = Stage::Stopping;
        send("quit\n");
        m_deadline.start(deadlineMs());
    }

    void checkpointAndContinue()
    {
        const QByteArray job = m_id.toUtf8();
        const QByteArray payload =
            QJsonDocument(m_evaluations).toJson(QJsonDocument::Compact);
        const uint checkpoint = m_nextIndex + 1;
        if (!omachess_background_job_checkpoint_with_payload(job.constData(), checkpoint, "running",
                                                            payload.constData())) {
            fail();
            return;
        }
        m_nextIndex = checkpoint;
        startNext();
    }

    void fail()
    {
        m_deadline.stop();
        if (m_process.state() != QProcess::NotRunning) {
            m_stopping = true;
            m_process.kill();
        }
        emit finished(m_id, false);
    }

    bool loadSavedEvaluations()
    {
        const QByteArray job = m_id.toUtf8();
        char *jobJson = omachess_background_job_json(job.constData());
        if (!jobJson)
            return false;
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray(jobJson));
        omachess_string_free(jobJson);
        if (!document.isObject())
            return false;
        const QString payload = document.object().value(QStringLiteral("payload")).toString();
        if (payload.isEmpty() || payload == QStringLiteral("{}"))
            return m_nextIndex == 0;
        const QJsonDocument payloadDocument = QJsonDocument::fromJson(payload.toUtf8());
        if (!payloadDocument.isArray())
            return false;
        const QJsonArray saved = payloadDocument.array();
        if (saved.size() < static_cast<int>(m_nextIndex))
            return false;
        for (int index = 0; index < static_cast<int>(m_nextIndex); ++index) {
            const QJsonObject evaluation = saved.at(index).toObject();
            if (evaluation.value(QStringLiteral("ply")).toInt(-1) != index)
                return false;
            m_evaluations.append(evaluation);
        }
        return true;
    }

    QString m_id;
    uint m_nextIndex = 0;
    QList<Position> m_positions;
    EngineLaunch m_launch;
    QProcess m_process;
    QTimer m_deadline;
    QByteArray m_output;
    Stage m_stage = Stage::Idle;
    QJsonArray m_evaluations;
    QString m_evaluation;
    QHash<int, QString> m_variations;
    bool m_stopping = false;
};

// The public process boundary for Background Jobs. It exposes no workspace or
// engine objects: only a job id and its supported controls cross D-Bus.
class BackgroundWorker final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.omachess.Omachess.BackgroundJobs")
public slots:
    bool MatchesContext(const QString &dataLocation, const QString &configLocation)
    {
        return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                   == QDir::cleanPath(dataLocation)
               && QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
                   == QDir::cleanPath(configLocation);
    }
    bool QuitIfIdle(const QString &dataLocation, const QString &configLocation)
    {
        if (MatchesContext(dataLocation, configLocation))
            return true;
        if (!m_runners.isEmpty())
            return false;
        QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
        return true;
    }
    QString StartComputerAnalysis(const QString &recordId, uint total)
    {
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QByteArray job = id.toUtf8();
        const QByteArray record = recordId.toUtf8();
        if (!omachess_background_job_create(job.constData(), record.constData(), total))
            return {};
        startRunner(id, 0);
        return id;
    }
    bool Pause(const QString &id)
    {
        const QByteArray job = id.toUtf8();
        const uint checkpoint = omachess_background_job_checkpoint_value(job.constData());
        if (checkpoint == UINT_MAX || !omachess_background_job_checkpoint(job.constData(), checkpoint, "paused"))
            return false;
        stopRunner(id);
        return true;
    }
    bool Resume(const QString &id)
    {
        const QByteArray job = id.toUtf8();
        const uint checkpoint = omachess_background_job_checkpoint_value(job.constData());
        const uint total = omachess_background_job_total_value(job.constData());
        if (checkpoint == UINT_MAX || total == UINT_MAX
            || !omachess_background_job_checkpoint(job.constData(), checkpoint, "running"))
            return false;
        startRunner(id, checkpoint);
        return true;
    }
    bool Cancel(const QString &id)
    {
        const QByteArray job = id.toUtf8();
        const uint checkpoint = omachess_background_job_checkpoint_value(job.constData());
        if (checkpoint == UINT_MAX || !omachess_background_job_checkpoint(job.constData(), checkpoint, "cancelled"))
            return false;
        stopRunner(id);
        return true;
    }
    bool Dismiss(const QString &id)
    {
        const QByteArray job = id.toUtf8();
        const uint checkpoint = omachess_background_job_checkpoint_value(job.constData());
        if (checkpoint == UINT_MAX || !omachess_background_job_checkpoint(job.constData(), checkpoint, "dismissed"))
            return false;
        stopRunner(id);
        return true;
    }
    QString Open(const QString &id)
    {
        const QByteArray job = id.toUtf8();
        // Opening is a workspace action. The worker validates the durable job
        // and returns its stable id so a newly launched workspace can attach.
        return omachess_background_job_checkpoint_value(job.constData()) == UINT_MAX ? QString() : id;
    }
    QString Job(const QString &id)
    {
        const QByteArray job = id.toUtf8();
        char *json = omachess_background_job_json(job.constData());
        if (!json)
            return {};
        const QString result = QString::fromUtf8(json);
        omachess_string_free(json);
        return result;
    }
    QString Jobs()
    {
        char *json = omachess_background_jobs_json();
        if (!json)
            return {};
        const QString result = QString::fromUtf8(json);
        omachess_string_free(json);
        return result;
    }
private:
    void startRunner(const QString &id, uint checkpoint)
    {
        stopRunner(id);
        const QByteArray job = id.toUtf8();
        auto *runner = new AnalysisRunner(id, checkpoint, this);
        connect(runner, &AnalysisRunner::finished, this, [this, id, job](const QString &, bool success) {
            if (!success) {
                const uint checkpoint = omachess_background_job_checkpoint_value(job.constData());
                if (checkpoint != UINT_MAX)
                    omachess_background_job_checkpoint(job.constData(), checkpoint, "failed");
            }
            stopRunner(id);
        });
        m_runners.insert(id, runner);
        if (!runner->start()) {
            const uint current = omachess_background_job_checkpoint_value(job.constData());
            if (current != UINT_MAX)
                omachess_background_job_checkpoint(job.constData(), current, "failed");
            stopRunner(id);
        }
    }
    void stopRunner(const QString &id)
    {
        if (auto *runner = m_runners.take(id)) {
            runner->stop();
            runner->deleteLater();
        }
    }
    QHash<QString, AnalysisRunner *> m_runners;
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Omachess"));
    omachess_background_jobs_recover();
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService("com.omachess.Omachess.BackgroundWorker")) return 1;
    BackgroundWorker worker;
    if (!bus.registerObject("/BackgroundJobs", &worker, QDBusConnection::ExportAllSlots)) return 1;
    return app.exec();
}

#include "BackgroundWorker.moc"
