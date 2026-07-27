#include "EngineManager.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

extern "C" {
#include "omachess_core.h"
}

EngineManager::EngineManager(QObject *parent)
    : QAbstractListModel(parent)
{
    m_profiles = {
        {QStringLiteral("stockfish"),
         QStringLiteral("Stockfish"),
         {QStringLiteral("stockfish")},
         {QStringLiteral("stockfish")},
         QStringLiteral("Not found"),
         {},
         {},
         {},
         QStringLiteral("engine-art/stockfish.svg"),
         QStringLiteral("Official Stockfish logo; GPL-3.0, stockfishchess.org"),
         {},
         0,
         3600},
        {QStringLiteral("leela"),
         QStringLiteral("Leela Chess Zero"),
         {QStringLiteral("lc0"), QStringLiteral("leelaz")},
         {QStringLiteral("lc0"), QStringLiteral("leela chess zero")},
         QStringLiteral("Not found"),
         {},
         {},
         {},
         QStringLiteral("engine-art/leela.svg"),
         QStringLiteral("Official Leela Chess Zero logo; GPL-3.0-or-later, lczero.org"),
         {},
         0,
         3500},
        {QStringLiteral("reckless"),
         QStringLiteral("Reckless"),
         {QStringLiteral("reckless")},
         {QStringLiteral("reckless")},
         QStringLiteral("Not found"),
         {},
         {},
         {},
         QStringLiteral("engine-art/reckless.svg"),
         QStringLiteral("Official Reckless project artwork; recklesschess.com"),
         {},
         0,
         3200},
        {QStringLiteral("komodo"),
         QStringLiteral("Komodo"),
         {QStringLiteral("komodo"), QStringLiteral("komodo-generic")},
         {QStringLiteral("komodo"), QStringLiteral("dragon")},
         QStringLiteral("Not found"),
         {},
         {},
         {},
         QStringLiteral("engine-art/komodo.svg"),
         QStringLiteral("Official Komodo mark; komodochess.com"),
         {},
         0,
         3400,
         false,
         true},
    };

    QSettings settings;
    for (Profile &profile : m_profiles)
        profile.rating = settings.value(QStringLiteral("engines/%1/displayRating").arg(profile.key),
                                        profile.rating)
                             .toInt();

    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        switch (m_stage) {
        case Stage::Starting:
        case Stage::Uci:
            fail(QStringLiteral("startup timeout"));
            break;
        case Stage::Ready:
            fail(QStringLiteral("readiness timeout"));
            break;
        case Stage::Search:
            fail(QStringLiteral("search timeout"));
            break;
        case Stage::Shutdown:
            fail(QStringLiteral("shutdown timeout"));
            break;
        case Stage::LiveStarting:
        case Stage::LiveUci:
            failLivePlay(QStringLiteral("could not start"));
            break;
        case Stage::LiveReady:
        case Stage::LiveSearch:
            failLivePlay(QStringLiteral("did not respond"));
            break;
        case Stage::Idle:
            break;
        }
    });
    connect(&m_process, &QProcess::started, this, [this] {
        if (m_stage == Stage::LiveStarting) {
            send("uci\n");
            advance(Stage::LiveUci, deadline(3000));
            return;
        }
        if (m_stage != Stage::Starting)
            return;
        send("uci\n");
        advance(Stage::Uci, deadline(3000));
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &EngineManager::readOutput);
    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                if (m_stage == Stage::Shutdown) {
                    m_deadline.stop();
                    if (exitStatus == QProcess::NormalExit && exitCode == 0)
                        finishReady();
                    else
                        fail(QStringLiteral("unclean shutdown"));
                } else if (m_livePlayActive && m_stage != Stage::Idle) {
                    Q_UNUSED(exitCode)
                    Q_UNUSED(exitStatus)
                    failLivePlay(QStringLiteral("engine stopped unexpectedly"));
                }
            });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_livePlayActive)
            failLivePlay(error == QProcess::Crashed
                             ? QStringLiteral("engine crashed")
                             : QStringLiteral("could not start"));
        else if (m_stage != Stage::Idle && error != QProcess::Crashed)
            fail(QStringLiteral("could not start"));
    });

    discover();
}

int EngineManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_profiles.size();
}

QVariant EngineManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_profiles.size())
        return {};
    const Profile &profile = m_profiles.at(index.row());
    switch (role) {
    case KeyRole: return profile.key;
    case NameRole: return profile.name;
    case StateRole: return profile.state;
    case IdentityRole: return profile.identity;
    case AuthorRole: return profile.author;
    case OptionCountRole: return profile.optionCount;
    case RatingRole: return profile.rating;
    case ArtworkRole: return profile.artwork;
    case ArtworkProvenanceRole: return profile.artworkProvenance;
    case FoundRole: return profile.found;
    case ConsentRequiredRole:
        return profile.found
            && (profile.state == QStringLiteral("Consent required")
                || profile.state == QStringLiteral("Consent granted — probe required"));
    case InstallOfferedRole: return !profile.detectOnly;
    default: return {};
    }
}

QHash<int, QByteArray> EngineManager::roleNames() const
{
    return {{KeyRole, "key"},
            {NameRole, "name"},
            {StateRole, "readinessState"},
            {IdentityRole, "identity"},
            {AuthorRole, "author"},
            {OptionCountRole, "optionCount"},
            {RatingRole, "rating"},
            {ArtworkRole, "artwork"},
            {ArtworkProvenanceRole, "artworkProvenance"},
            {FoundRole, "found"},
            {ConsentRequiredRole, "consentRequired"},
            {InstallOfferedRole, "installOffered"}};
}

void EngineManager::discover()
{
    for (int index = 0; index < m_profiles.size(); ++index) {
        Profile &profile = m_profiles[index];
        profile.path = discoverPath(profile);
        profile.found = !profile.path.isEmpty();
        if (!profile.found)
            continue;
        QSettings settings;
        const QString consentKey =
            QStringLiteral("engines/%1/consent/%2").arg(profile.key, profile.path);
        if (settings.value(consentKey, false).toBool())
            profile.state = QStringLiteral("Consent granted — probe required");
        else
            profile.state = QStringLiteral("Consent required");
        emit dataChanged(this->index(index), this->index(index));
    }
}

QString EngineManager::discoverPath(const Profile &profile) const
{
    QStringList candidates;
    const QString store =
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
            .filePath(QStringLiteral("omachess/engines/%1").arg(profile.key));
    for (const QString &name : profile.executableNames)
        candidates.append(QDir(store).filePath(name));
    for (const QString &name : profile.executableNames)
        candidates.append(QDir(QStringLiteral("/usr/bin")).filePath(name));

    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable())
            return info.canonicalFilePath();
    }
    return {};
}

int EngineManager::indexOf(const QString &key) const
{
    for (int index = 0; index < m_profiles.size(); ++index)
        if (m_profiles.at(index).key == key)
            return index;
    return -1;
}

void EngineManager::grantConsent(const QString &key)
{
    const int index = indexOf(key);
    if (index < 0 || !m_profiles.at(index).found || m_active >= 0)
        return;
    Profile &profile = m_profiles[index];
    QSettings settings;
    settings.setValue(QStringLiteral("engines/%1/consent/%2").arg(profile.key, profile.path), true);
    startProbe(index);
}

void EngineManager::setDisplayRating(const QString &key, int rating)
{
    const int index = indexOf(key);
    if (index < 0 || rating < 0 || rating > 5000)
        return;
    m_profiles[index].rating = rating;
    QSettings().setValue(QStringLiteral("engines/%1/displayRating").arg(key), rating);
    emit dataChanged(this->index(index), this->index(index), {RatingRole});
}

void EngineManager::setLivePlaySearchTime(const QString &key, int milliseconds)
{
    if (indexOf(key) < 0 || milliseconds < 10 || milliseconds > 60000)
        return;
    QSettings().setValue(QStringLiteral("livePlay/%1/searchTimeMs").arg(key), milliseconds);
}

int EngineManager::livePlaySearchTime(const QString &key) const
{
    return QSettings()
        .value(QStringLiteral("livePlay/%1/searchTimeMs").arg(key), 250)
        .toInt();
}

void EngineManager::setLivePlayClock(const QString &key, int milliseconds)
{
    if (indexOf(key) < 0 || milliseconds < 0)
        return;
    QSettings().setValue(QStringLiteral("livePlay/%1/clockMs").arg(key), milliseconds);
}

int EngineManager::livePlayClock(const QString &key) const
{
    return QSettings().value(QStringLiteral("livePlay/%1/clockMs").arg(key), 0).toInt();
}

void EngineManager::startLivePlay(const QString &key, const QString &humanSide)
{
    const int profileIndex = indexOf(key);
    if (profileIndex < 0 || m_livePlayActive || m_active >= 0
        || !m_profiles.at(profileIndex).state.startsWith(QStringLiteral("Ready"))
        || (humanSide != QStringLiteral("white") && humanSide != QStringLiteral("black"))) {
        return;
    }
    m_active = profileIndex;
    m_livePlayActive = true;
    m_livePlayEngineSide =
        humanSide == QStringLiteral("white") ? QStringLiteral("black") : QStringLiteral("white");
    m_liveSearchTimeMs = livePlaySearchTime(key);
    m_liveMoves.clear();
    m_liveSideToMove = QStringLiteral("white");
    m_livePlayStatus = QStringLiteral("Playing %1").arg(m_profiles.at(profileIndex).name);
    emit livePlayChanged();

    m_output.clear();
    m_process.setProgram(m_profiles.at(profileIndex).path);
    m_process.setArguments({});
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.start();
    advance(Stage::LiveStarting, deadline(3000));
}

void EngineManager::updateLivePosition(const QString &moves, const QString &sideToMove,
                                       bool gameOver, int whiteMs, int blackMs)
{
    if (!m_livePlayActive)
        return;
    m_liveMoves = moves;
    m_liveSideToMove = sideToMove;
    m_liveWhiteMs = whiteMs;
    m_liveBlackMs = blackMs;
    if (gameOver) {
        stopLivePlay();
        return;
    }
    requestLiveMove();
}

void EngineManager::rejectLiveMove()
{
    failLivePlay(QStringLiteral("returned an illegal or malformed move"));
}

void EngineManager::stopLivePlay()
{
    if (!m_livePlayActive)
        return;
    m_deadline.stop();
    if (m_process.state() != QProcess::NotRunning) {
        send("quit\n");
        if (!m_process.waitForFinished(50))
            m_process.kill();
    }
    m_livePlayActive = false;
    m_livePlayEngineSide.clear();
    m_livePlayStatus.clear();
    m_active = -1;
    m_stage = Stage::Idle;
    emit livePlayChanged();
}

void EngineManager::startProbe(int index)
{
    m_active = index;
    Profile &profile = m_profiles[index];
    profile.state = QStringLiteral("Probing…");
    profile.identity.clear();
    profile.author.clear();
    profile.optionCount = 0;
    profile.options.clear();
    profile.identityMismatch = false;
    m_registrationRequired = false;
    m_sawMalformedHandshake = false;
    m_output.clear();
    emit dataChanged(this->index(index), this->index(index));

    m_process.setProgram(profile.path);
    m_process.setArguments({});
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.start();
    advance(Stage::Starting, deadline(3000));
}

void EngineManager::send(const QByteArray &command)
{
    m_process.write(command);
}

void EngineManager::readOutput()
{
    m_output += m_process.readAllStandardOutput();
    while (true) {
        const qsizetype newline = m_output.indexOf('\n');
        if (newline < 0)
            return;
        const QString line = QString::fromUtf8(m_output.left(newline)).trimmed();
        m_output.remove(0, newline + 1);
        consumeLine(line);
    }
}

void EngineManager::consumeLine(const QString &line)
{
    if (m_active < 0)
        return;
    Profile &profile = m_profiles[m_active];
    if (m_stage == Stage::LiveUci) {
        if (line == QStringLiteral("uciok")) {
            send("isready\n");
            advance(Stage::LiveReady, deadline(5000));
        }
        return;
    }
    if (m_stage == Stage::LiveReady && line == QStringLiteral("readyok")) {
        m_deadline.stop();
        requestLiveMove();
        return;
    }
    if (m_stage == Stage::LiveSearch && line.startsWith(QStringLiteral("bestmove "))) {
        m_deadline.stop();
        const QString move = line.sliced(9).section(QLatin1Char(' '), 0, 0).toLower();
        if (move.size() < 4) {
            failLivePlay(QStringLiteral("returned an invalid move"));
            return;
        }
        m_stage = Stage::LiveReady;
        const QString promotion =
            move.size() > 4
            ? QHash<QChar, QString>{{'q', QStringLiteral("queen")},
                                    {'r', QStringLiteral("rook")},
                                    {'b', QStringLiteral("bishop")},
                                    {'n', QStringLiteral("knight")}}
                  .value(move.at(4))
            : QString();
        emit liveMove(move.first(2), move.sliced(2, 2), promotion);
        return;
    }
    if (line.startsWith(QStringLiteral("registration"))
        || line.startsWith(QStringLiteral("copyprotection"))) {
        m_registrationRequired = true;
        fail(QStringLiteral("unsupported registration required"));
        return;
    }

    if (m_stage == Stage::Uci) {
        if (line.startsWith(QStringLiteral("id name ")))
            profile.identity = line.mid(8).trimmed();
        else if (line.startsWith(QStringLiteral("id author ")))
            profile.author = line.mid(10).trimmed();
        else if (line.startsWith(QStringLiteral("option name "))) {
            static const QRegularExpression optionPattern(
                QStringLiteral("^option name (.+) type (check|spin|combo|button|string)"
                               "(?: default (.*?))?(?: min (-?\\d+))?(?: max (-?\\d+))?"
                               "((?: var .*?)*)$"));
            const QRegularExpressionMatch match = optionPattern.match(line);
            if (!match.hasMatch()) {
                m_sawMalformedHandshake = true;
                return;
            }
            QVariantMap option{
                {QStringLiteral("name"), match.captured(1)},
                {QStringLiteral("type"), match.captured(2)},
                {QStringLiteral("default"), match.captured(3)},
            };
            if (!match.captured(4).isEmpty())
                option.insert(QStringLiteral("min"), match.captured(4).toLongLong());
            if (!match.captured(5).isEmpty())
                option.insert(QStringLiteral("max"), match.captured(5).toLongLong());
            QString variants = match.captured(6).trimmed();
            QStringList values;
            if (!variants.isEmpty()) {
                variants.remove(0, 4);
                values = variants.split(QStringLiteral(" var "));
            }
            option.insert(QStringLiteral("variants"), values);
            profile.options.append(option);
            ++profile.optionCount;
        }
        else if (line == QStringLiteral("uciok")) {
            if (profile.identity.isEmpty() || m_sawMalformedHandshake) {
                fail(QStringLiteral("malformed UCI handshake"));
                return;
            }
            profile.identityMismatch = !identityMatches(profile);
            QByteArray defaults;
            if (!profile.identityMismatch) {
                for (const QVariant &value : profile.options) {
                    const QString name = value.toMap().value(QStringLiteral("name")).toString();
                    if (name.compare(QStringLiteral("Threads"), Qt::CaseInsensitive) == 0)
                        defaults += "setoption name " + name.toUtf8() + " value 1\n";
                    else if (name.compare(QStringLiteral("Hash"), Qt::CaseInsensitive) == 0)
                        defaults += "setoption name " + name.toUtf8() + " value 16\n";
                }
            }
            send(defaults + "isready\n");
            advance(Stage::Ready, deadline(5000));
            return;
        } else if (!line.isEmpty() && !line.startsWith(QStringLiteral("info "))) {
            m_sawMalformedHandshake = true;
        }
    } else if (m_stage == Stage::Ready && line == QStringLiteral("readyok")) {
        send("ucinewgame\nposition startpos\ngo movetime 50\n");
        advance(Stage::Search, deadline(1500));
    } else if (m_stage == Stage::Search && line.startsWith(QStringLiteral("bestmove "))) {
        const QString move = line.sliced(9).section(QLatin1Char(' '), 0, 0).toLower();
        if (!omachess_standard_start_move_is_legal(move.toUtf8().constData())) {
            fail(QStringLiteral("illegal or malformed bestmove"));
            return;
        }
        send("quit\n");
        advance(Stage::Shutdown, deadline(1000));
        if (m_process.waitForFinished(0))
            finishReady();
    }
}

void EngineManager::advance(Stage next, int deadlineMs)
{
    m_stage = next;
    m_deadline.start(deadlineMs);
}

void EngineManager::fail(const QString &reason)
{
    if (m_active < 0)
        return;
    Profile &profile = m_profiles[m_active];
    profile.state =
        m_registrationRequired
        ? QStringLiteral("Recognized — unsupported registration required")
        : QStringLiteral("Probe failed — %1").arg(reason);
    emit dataChanged(index(m_active), index(m_active));
    m_stage = Stage::Idle;
    stopProcess();
    m_active = -1;
}

void EngineManager::finishReady()
{
    if (m_active < 0)
        return;
    const int completed = m_active;
    Profile &profile = m_profiles[completed];
    profile.state = profile.identityMismatch ? QStringLiteral("Ready — identity mismatch")
                                             : QStringLiteral("Ready");
    m_active = -1;
    m_stage = Stage::Idle;
    emit dataChanged(index(completed), index(completed));
}

bool EngineManager::identityMatches(const Profile &profile) const
{
    const QString identity = profile.identity.toLower();
    for (const QString &alias : profile.identityAliases)
        if (identity.startsWith(alias))
            return true;
    return false;
}

void EngineManager::stopProcess()
{
    m_deadline.stop();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(50))
            m_process.kill();
    }
}

int EngineManager::deadline(int productionMs) const
{
    bool ok = false;
    const int testValue = qEnvironmentVariableIntValue("OMACHESS_TEST_ENGINE_DEADLINE_MS", &ok);
    return ok && qEnvironmentVariableIsSet("OMACHESS_TEST_CHANNEL") ? testValue : productionMs;
}

void EngineManager::requestLiveMove()
{
    if (!m_livePlayActive || m_stage != Stage::LiveReady
        || m_liveSideToMove != m_livePlayEngineSide) {
        return;
    }
    QByteArray position("position startpos");
    if (!m_liveMoves.isEmpty())
        position += " moves " + m_liveMoves.toUtf8();
    QByteArray go;
    if (m_liveWhiteMs > 0 && m_liveBlackMs > 0) {
        go = "go wtime " + QByteArray::number(m_liveWhiteMs) + " btime "
             + QByteArray::number(m_liveBlackMs);
    } else {
        go = "go movetime " + QByteArray::number(m_liveSearchTimeMs);
    }
    send(position + "\n" + go + "\n");
    advance(Stage::LiveSearch, deadline(qMax(1500, m_liveSearchTimeMs + 1000)));
}

void EngineManager::failLivePlay(const QString &reason)
{
    if (!m_livePlayActive)
        return;
    m_deadline.stop();
    m_livePlayActive = false;
    m_livePlayEngineSide.clear();
    m_livePlayStatus = QStringLiteral("Engine %1. The Played Game is safe.").arg(reason);
    m_stage = Stage::Idle;
    stopProcess();
    m_active = -1;
    emit livePlayChanged();
}
