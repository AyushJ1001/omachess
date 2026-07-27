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
    loadCustomEngine();

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
        case Stage::Idle:
            break;
        }
    });
    connect(&m_process, &QProcess::started, this, [this] {
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
                } else if (m_stage != Stage::Idle) {
                    fail(QStringLiteral("engine exited before completing the UCI probe"));
                }
            });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_stage != Stage::Idle && error != QProcess::Crashed)
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
    case ExecutablePathRole: return profile.path;
    case LaunchArgumentsRole: return profile.arguments;
    case LaunchWorkingDirectoryRole: return profile.workingDirectory;
    case CapabilitiesRole: return profile.capabilities.join(QStringLiteral(", "));
    case CustomRole: return profile.custom;
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
            {InstallOfferedRole, "installOffered"},
            {ExecutablePathRole, "executablePath"},
            {LaunchArgumentsRole, "launchArguments"},
            {LaunchWorkingDirectoryRole, "launchWorkingDirectory"},
            {CapabilitiesRole, "capabilities"},
            {CustomRole, "custom"}};
}

void EngineManager::loadCustomEngine()
{
    QSettings settings;
    const QString path = settings.value(QStringLiteral("engines/custom/path")).toString();
    if (path.isEmpty())
        return;
    Profile profile;
    profile.key = QStringLiteral("custom");
    profile.name = QStringLiteral("Custom Engine");
    profile.state = QStringLiteral("Not found");
    profile.path = path;
    profile.arguments = settings.value(QStringLiteral("engines/custom/arguments")).toString();
    profile.workingDirectory =
        settings.value(QStringLiteral("engines/custom/workingDirectory")).toString();
    profile.custom = true;
    profile.found = QFileInfo(path).isFile() && QFileInfo(path).isExecutable();
    if (profile.found) {
        const QString consentKey =
            QStringLiteral("engines/custom/consent/%1").arg(profile.path);
        profile.state = settings.value(consentKey, false).toBool()
            ? QStringLiteral("Consent granted — probe required")
            : QStringLiteral("Consent required");
    }
    m_profiles.prepend(profile);
}

void EngineManager::saveCustomEngine(const Profile &profile)
{
    QSettings settings;
    settings.setValue(QStringLiteral("engines/custom/path"), profile.path);
    settings.setValue(QStringLiteral("engines/custom/arguments"), profile.arguments);
    settings.setValue(QStringLiteral("engines/custom/workingDirectory"), profile.workingDirectory);
}

void EngineManager::registerCustomEngine(const QUrl &selectedFile,
                                         const QString &arguments,
                                         const QString &workingDirectory)
{
    const QString path = selectedFile.toLocalFile();
    if (m_active >= 0 || path.isEmpty())
        return;
    const int existing = indexOf(QStringLiteral("custom"));
    if (existing >= 0) {
        beginRemoveRows({}, existing, existing);
        m_profiles.removeAt(existing);
        endRemoveRows();
    }

    Profile profile;
    profile.key = QStringLiteral("custom");
    profile.name = QStringLiteral("Custom Engine");
    profile.path = path;
    profile.arguments = arguments;
    profile.workingDirectory = workingDirectory;
    profile.custom = true;
    const QFileInfo executable(path);
    profile.found = executable.isFile() && executable.isExecutable();
    const bool consented =
        QSettings().value(QStringLiteral("engines/custom/consent/%1").arg(path), false).toBool();
    profile.state = !profile.found
        ? QStringLiteral("Probe failed — path is not executable")
        : consented ? QStringLiteral("Consent granted — probe required")
                    : QStringLiteral("Consent required");
    saveCustomEngine(profile);
    beginInsertRows({}, 0, 0);
    m_profiles.prepend(profile);
    endInsertRows();
}

void EngineManager::discover()
{
    for (int index = 0; index < m_profiles.size(); ++index) {
        Profile &profile = m_profiles[index];
        if (profile.custom)
            continue;
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
    m_process.setArguments(QProcess::splitCommand(profile.arguments));
    m_process.setWorkingDirectory(profile.workingDirectory);
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
            profile.capabilities.clear();
            QByteArray defaults;
            if (!profile.identityMismatch || profile.custom) {
                for (const QVariant &value : profile.options) {
                    const QVariantMap option = value.toMap();
                    const QString name = option.value(QStringLiteral("name")).toString();
                    const QString type = option.value(QStringLiteral("type")).toString();
                    const bool hasBounds = option.contains(QStringLiteral("min"))
                        && option.contains(QStringLiteral("max"));
                    const qlonglong minimum = option.value(QStringLiteral("min")).toLongLong();
                    const qlonglong maximum = option.value(QStringLiteral("max")).toLongLong();
                    if (name.compare(QStringLiteral("Threads"), Qt::CaseInsensitive) == 0
                        && type == QStringLiteral("spin") && hasBounds && minimum <= 1
                        && maximum >= 1) {
                        profile.capabilities.append(QStringLiteral("Threads"));
                        defaults += "setoption name " + name.toUtf8() + " value 1\n";
                    } else if (name.compare(QStringLiteral("Hash"), Qt::CaseInsensitive) == 0
                               && type == QStringLiteral("spin") && hasBounds && minimum <= 16
                               && maximum >= 16) {
                        profile.capabilities.append(QStringLiteral("Hash"));
                        defaults += "setoption name " + name.toUtf8() + " value 16\n";
                    } else if (name.compare(QStringLiteral("MultiPV"), Qt::CaseInsensitive) == 0
                               && type == QStringLiteral("spin") && hasBounds && maximum >= 2) {
                        profile.capabilities.append(QStringLiteral("MultiPV"));
                    } else if (name.compare(QStringLiteral("UCI_Variant"), Qt::CaseInsensitive) == 0
                               && type == QStringLiteral("combo")
                               && !option.value(QStringLiteral("variants")).toStringList().isEmpty()) {
                        profile.capabilities.append(QStringLiteral("UCI_Variant"));
                    }
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
    Profile &profile = m_profiles[m_active];
    profile.state = profile.identityMismatch ? QStringLiteral("Ready — identity mismatch")
                                             : QStringLiteral("Ready");
    emit dataChanged(index(m_active), index(m_active));
    m_active = -1;
    m_stage = Stage::Idle;
}

bool EngineManager::identityMatches(const Profile &profile) const
{
    if (profile.custom)
        return true;
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
