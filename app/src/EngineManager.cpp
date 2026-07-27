#include "EngineManager.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
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
         {},
         QStringLiteral("Komodo artwork is not redistributed"),
         {},
         0,
         3400,
         false,
         true},
    };

    QSettings settings;
    for (Profile &profile : m_profiles) {
        profile.rating = settings.value(QStringLiteral("engines/%1/displayRating").arg(profile.key),
                                        profile.rating)
                             .toInt();
        if (profile.key == QStringLiteral("reckless"))
            profile.upstreamUrl =
                QStringLiteral("https://github.com/codedeliveryservice/Reckless/releases/"
                               "download/v0.9.0/reckless-linux-generic");
    }
    if (qEnvironmentVariableIsSet("OMACHESS_TEST_CHANNEL")) {
        for (Profile &profile : m_profiles) {
            const QByteArray variable =
                QByteArray("OMACHESS_TEST_") + profile.key.toUpper().toUtf8() + "_URL";
            const QString overrideUrl = qEnvironmentVariable(variable.constData());
            if (!overrideUrl.isEmpty())
                profile.upstreamUrl = overrideUrl;
        }
    }
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
    case InstallOfferedRole:
        return !profile.detectOnly && !profile.found && !profile.upstreamUrl.isEmpty()
            && m_installing != index.row();
    case ExecutablePathRole: return profile.path;
    case LaunchArgumentsRole: return profile.arguments;
    case LaunchWorkingDirectoryRole: return profile.workingDirectory;
    case CapabilitiesRole: return profile.capabilities.join(QStringLiteral(", "));
    case CustomRole: return profile.custom;
    case InstallingRole: return m_installing == index.row();
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
            {CustomRole, "custom"},
            {InstallingRole, "installing"}};
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
    const QString store = storeDirectory(profile);
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

QString EngineManager::storeDirectory(const Profile &profile) const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("omachess/engines/%1").arg(profile.key));
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

void EngineManager::install(const QString &key)
{
    const int profileIndex = indexOf(key);
    if (profileIndex < 0 || m_installing >= 0 || m_profiles.at(profileIndex).found)
        return;
    Profile &profile = m_profiles[profileIndex];
    if (profile.detectOnly || profile.upstreamUrl.isEmpty())
        return;

    const QString directory = storeDirectory(profile);
    if (!QDir().mkpath(directory)) {
        profile.state = QStringLiteral("Install failed — App Engine Store is unavailable");
        emit dataChanged(index(profileIndex), index(profileIndex));
        return;
    }
    m_downloadFile.setFileName(QDir(directory).filePath(QStringLiteral(".download")));
    if (!m_downloadFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        profile.state = QStringLiteral("Install failed — could not create staging file");
        emit dataChanged(index(profileIndex), index(profileIndex));
        return;
    }

    m_installing = profileIndex;
    profile.state = QStringLiteral("Downloading…");
    emit dataChanged(index(profileIndex), index(profileIndex));
    QNetworkRequest request{QUrl(profile.upstreamUrl)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_download = m_network.get(request);
    connect(m_download, &QNetworkReply::readyRead, this, [this] {
        if (m_download && m_downloadFile.write(m_download->readAll()) < 0)
            failInstall(QStringLiteral("could not write staging file"));
    });
    connect(m_download, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
        constexpr qint64 maximumDownloadBytes = 512 * 1024 * 1024;
        if (m_installing < 0)
            return;
        if (received > maximumDownloadBytes || total > maximumDownloadBytes) {
            if (m_download)
                m_download->abort();
            failInstall(QStringLiteral("upstream download is too large"));
            return;
        }
        if (total <= 0)
            return;
        m_profiles[m_installing].state =
            QStringLiteral("Downloading… %1%").arg(received * 100 / total);
        emit dataChanged(index(m_installing), index(m_installing), {StateRole});
    });
    connect(m_download, &QNetworkReply::finished, this, [this] {
        if (!m_download || m_installing < 0)
            return;
        if (m_download->error() != QNetworkReply::NoError) {
            failInstall(QStringLiteral("upstream error: %1").arg(m_download->errorString()));
            return;
        }
        finishInstall();
    });
}

void EngineManager::cancelInstall(const QString &key)
{
    if (m_installing < 0 || m_profiles.at(m_installing).key != key)
        return;
    if (m_download) {
        disconnect(m_download, nullptr, this, nullptr);
        m_download->abort();
    }
    failInstall(QStringLiteral("cancelled"));
}

void EngineManager::finishInstall()
{
    const int profileIndex = m_installing;
    Profile &profile = m_profiles[profileIndex];
    if (m_download)
        m_downloadFile.write(m_download->readAll());
    m_downloadFile.close();
    const QString staged = m_downloadFile.fileName();
    const QString target = QDir(storeDirectory(profile)).filePath(profile.executableNames.first());
    if (!QFile::setPermissions(staged, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                          | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                          | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                          | QFileDevice::ExeOther)) {
        failInstall(QStringLiteral("could not make downloaded engine executable"));
        return;
    }
    if (!QFile::rename(staged, target)) {
        failInstall(QStringLiteral("could not publish downloaded engine"));
        return;
    }
    if (m_download)
        m_download->deleteLater();
    m_download = nullptr;
    m_installing = -1;
    const QFileInfo installed(target);
    profile.path = installed.canonicalFilePath();
    if (profile.path.isEmpty() || !installed.isFile() || !installed.isExecutable()) {
        QFile::remove(target);
        profile.path.clear();
        failInstall(QStringLiteral("downloaded engine is not executable"));
        return;
    }
    profile.found = true;
    profile.state = QStringLiteral("Consent required");
    emit dataChanged(index(profileIndex), index(profileIndex));
}

void EngineManager::failInstall(const QString &reason)
{
    if (m_installing < 0)
        return;
    const int profileIndex = m_installing;
    if (m_download) {
        disconnect(m_download, nullptr, this, nullptr);
        m_download->deleteLater();
    }
    m_download = nullptr;
    m_downloadFile.close();
    QFile::remove(m_downloadFile.fileName());
    m_installing = -1;
    m_profiles[profileIndex].state = QStringLiteral("Install failed — %1").arg(reason);
    emit dataChanged(index(profileIndex), index(profileIndex));
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
    m_operation = Operation::Probe;
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

void EngineManager::analyzePosition(const QString &fen, bool ruleValid)
{
    if (m_requestedFen == fen && m_requestedRuleValid == ruleValid
        && (analyzing() || analysisReady()))
        return;

    m_requestedFen = fen;
    m_requestedRuleValid = ruleValid;
    m_analysisEvaluation.clear();
    m_analysisVariations.clear();
    m_searchVariations.clear();
    m_analysisMessage = ruleValid ? QStringLiteral("Waiting for a Ready engine.")
                                  : QStringLiteral("Engine analysis is not guaranteed for a Freeform Position.");
    emit analysisChanged();

    if (m_operation == Operation::Analysis && m_active >= 0) {
        m_active = -1;
        m_stage = Stage::Idle;
        stopProcess();
    }
    if (!ruleValid) {
        return;
    }
    if (m_readyProfile >= 0 && m_active < 0)
        startAnalysis();
}

void EngineManager::clearAnalysis()
{
    if (m_operation == Operation::Analysis) {
        m_active = -1;
        m_stage = Stage::Idle;
        stopProcess();
    }
    m_analysisEvaluation.clear();
    m_analysisVariations.clear();
    m_searchVariations.clear();
    m_analysisMessage.clear();
    emit analysisChanged();
}

void EngineManager::startAnalysis()
{
    if (m_readyProfile < 0 || m_requestedFen.isEmpty() || !m_requestedRuleValid)
        return;
    m_operation = Operation::Analysis;
    m_active = m_readyProfile;
    m_output.clear();
    m_searchVariations.clear();
    m_analysisMessage = QStringLiteral("Analyzing…");
    emit analysisChanged();
    const Profile &profile = m_profiles.at(m_readyProfile);
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
            if (m_operation == Operation::Analysis) {
                send("setoption name MultiPV value 3\nisready\n");
                advance(Stage::Ready, deadline(5000));
                return;
            }
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
        if (m_operation == Operation::Analysis) {
            send("position fen " + m_requestedFen.toUtf8() + "\ngo movetime 250\n");
        } else {
            send("ucinewgame\nposition startpos\ngo movetime 50\n");
        }
        advance(Stage::Search, deadline(1500));
    } else if (m_stage == Stage::Search && m_operation == Operation::Analysis
               && line.startsWith(QStringLiteral("info "))) {
        consumeAnalysisInfo(line);
    } else if (m_stage == Stage::Search && line.startsWith(QStringLiteral("bestmove "))) {
        if (m_operation == Operation::Analysis) {
            finishAnalysis();
            return;
        }
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

void EngineManager::consumeAnalysisInfo(const QString &line)
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
            m_analysisEvaluation = score >= 0 ? QStringLiteral("#%1").arg(score)
                                               : QStringLiteral("-#%1").arg(-score);
        else
            m_analysisEvaluation =
                QStringLiteral("%1%2").arg(score >= 0 ? QStringLiteral("+") : QString())
                    .arg(score / 100.0, 0, 'f', 2);
    }
    m_searchVariations.insert(rank, pvMatch.captured(1));
}

void EngineManager::finishAnalysis()
{
    m_deadline.stop();
    m_analysisVariations.clear();
    for (auto variation = m_searchVariations.cbegin(); variation != m_searchVariations.cend();
         ++variation)
        m_analysisVariations.append(QStringLiteral("%1. %2").arg(variation.key()).arg(variation.value()));
    m_analysisMessage = m_analysisEvaluation.isEmpty()
                            ? QStringLiteral("The engine returned no analysis.")
                            : QStringLiteral("Live Position Analysis");
    m_active = -1;
    m_stage = Stage::Idle;
    send("quit\n");
    stopProcess();
    emit analysisChanged();
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
    if (m_operation == Operation::Analysis) {
        m_analysisMessage = QStringLiteral("Analysis unavailable — %1").arg(reason);
        m_analysisEvaluation.clear();
        m_analysisVariations.clear();
        m_stage = Stage::Idle;
        stopProcess();
        m_active = -1;
        emit analysisChanged();
        return;
    }
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
    m_readyProfile = m_active;
    m_active = -1;
    m_stage = Stage::Idle;
    if (!m_requestedFen.isEmpty() && m_requestedRuleValid)
        startAnalysis();
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
