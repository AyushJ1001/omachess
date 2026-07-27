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
        const int analysisProbeDeadline = m_operation == Operation::Analysis
                && m_computerAnalysisActive
            ? qMax(500, deadline(3000)) : deadline(3000);
        advance(Stage::Uci, analysisProbeDeadline);
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
                } else if (m_stage != Stage::Idle) {
                    fail(QStringLiteral("engine exited before completing the UCI probe"));
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

    compileComputerAnalysis();
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
    if (m_operation == Operation::Analysis && m_active >= 0)
        clearAnalysis();
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
    const Profile &profile = m_profiles.at(profileIndex);
    m_process.setProgram(profile.path);
    m_process.setArguments(QProcess::splitCommand(profile.arguments));
    m_process.setWorkingDirectory(profile.workingDirectory);
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
    if (!m_requestedFen.isEmpty() && m_requestedRuleValid)
        startAnalysis();
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
    m_analysisDepth = 0;
    m_analysisMessage = ruleValid ? QStringLiteral("Waiting for a Ready engine.")
                                  : QStringLiteral("Engine analysis is not guaranteed for a Freeform Position.");
    if (ruleValid && m_computerAnalysisActive) {
        m_analysisMessage = QStringLiteral("%1 · searching position %2 of %3")
                                .arg(m_computerAnalysisDisclosure)
                                .arg(m_computerAnalysisPositionsCompleted + 1)
                                .arg(m_computerAnalysisPositionCount);
    }
    if (m_livePlayActive && ruleValid)
        m_analysisMessage = QStringLiteral("Live Position Analysis pauses while this engine is playing.");
    emit analysisChanged();

    if (m_livePlayActive)
        return;
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
        if (!m_computerAnalysisActive)
            stopProcess();
    }
    m_analysisEvaluation.clear();
    m_analysisVariations.clear();
    m_searchVariations.clear();
    m_analysisDepth = 0;
    m_analysisMessage.clear();
    emit analysisChanged();
}

void EngineManager::setComputerAnalysisBudget(const QString &budget, int positionCount)
{
    const Budget selected = budgetDefinition(budget);
    m_computerAnalysisBudget = selected.key;
    if (positionCount >= 0)
        m_computerAnalysisPositionCount = positionCount;
    compileComputerAnalysis();
    updateComputerAnalysisEstimate();
    emit analysisChanged();
}

void EngineManager::beginComputerAnalysis(const QString &budget, int positionCount)
{
    setComputerAnalysisBudget(budget, positionCount);
    m_computerAnalysisActive = true;
    m_computerAnalysisPositionsCompleted = 0;
    m_computerAnalysisTimer.start();
    updateComputerAnalysisEstimate();
    emit analysisChanged();
}

void EngineManager::recordComputerAnalysisPosition()
{
    if (!m_computerAnalysisActive)
        return;
    ++m_computerAnalysisPositionsCompleted;
    updateComputerAnalysisEstimate();
    emit analysisChanged();
}

void EngineManager::endComputerAnalysis()
{
    if (!m_computerAnalysisActive)
        return;
    if (m_computerAnalysisPositionsCompleted > 0 && m_computerAnalysisTimer.isValid()) {
        const double observed = static_cast<double>(m_computerAnalysisTimer.elapsed())
                                / m_computerAnalysisPositionsCompleted;
        const QString profileKey = m_readyProfile >= 0
            ? m_profiles.at(m_readyProfile).key : QStringLiteral("engine");
        QSettings().setValue(QStringLiteral("analysisCalibration/%1/%2/msPerPosition")
                                 .arg(profileKey, m_computerAnalysisBudget), observed);
    }
    m_computerAnalysisActive = false;
    if (m_operation == Operation::Analysis && m_active < 0) {
        m_stage = Stage::Idle;
        stopProcess();
    }
}

EngineManager::Budget EngineManager::budgetDefinition(const QString &key) const
{
    if (key == QStringLiteral("quick"))
        return {QStringLiteral("quick"), QStringLiteral("Quick"), 250, 1,
                QStringLiteral("Low resources"), 1, 16};
    if (key == QStringLiteral("deep"))
        return {QStringLiteral("deep"), QStringLiteral("Deep"), 5000, 3,
                QStringLiteral("High resources"), 4, 256};
    return {QStringLiteral("standard"), QStringLiteral("Standard"), 1000, 2,
            QStringLiteral("Moderate resources"), 2, 64};
}

QString EngineManager::formatDuration(qint64 milliseconds) const
{
    if (milliseconds < 1000)
        return QStringLiteral("%1 ms").arg(qMax<qint64>(0, milliseconds));
    const double seconds = static_cast<double>(milliseconds) / 1000.0;
    if (qFuzzyCompare(seconds, qRound(seconds)))
        return QStringLiteral("%1 s").arg(qRound(seconds));
    return QStringLiteral("%1 s").arg(seconds, 0, 'f', 1);
}

void EngineManager::compileComputerAnalysis()
{
    const Budget selected = budgetDefinition(m_computerAnalysisBudget);
    m_computerAnalysisTimeMs = selected.milliseconds;
    m_computerAnalysisLineLimit = selected.lines;
    m_computerAnalysisSettings.clear();

    const Profile *profile = m_readyProfile >= 0 && m_readyProfile < m_profiles.size()
        ? &m_profiles.at(m_readyProfile) : nullptr;
    auto optionNamed = [profile](const QString &wanted) -> QVariantMap {
        if (!profile)
            return {};
        for (const QVariant &value : profile->options) {
            const QVariantMap option = value.toMap();
            if (option.value(QStringLiteral("name")).toString().compare(
                    wanted, Qt::CaseInsensitive) == 0)
                return option;
        }
        return {};
    };
    auto boundedValue = [](const QVariantMap &option, int requested, int softMaximum) {
        if (option.isEmpty() || option.value(QStringLiteral("type")).toString()
                .compare(QStringLiteral("spin"), Qt::CaseInsensitive) != 0
            || !option.contains(QStringLiteral("min"))
            || !option.contains(QStringLiteral("max")))
            return -1;
        const int minimum = option.value(QStringLiteral("min")).toInt();
        const int maximum = qMin(softMaximum, option.value(QStringLiteral("max")).toInt());
        if (minimum > maximum)
            return -1;
        return qBound(minimum, requested, maximum);
    };

    const QVariantMap multiPv = optionNamed(QStringLiteral("MultiPV"));
    const int requestedLines = selected.lines;
    const int effectiveLines = boundedValue(multiPv, requestedLines, 64);
    if (effectiveLines >= 1) {
        m_computerAnalysisLineLimit = effectiveLines;
        m_computerAnalysisSettings =
            QStringLiteral("setoption name %1 value %2\n")
                .arg(multiPv.value(QStringLiteral("name")).toString())
                .arg(effectiveLines);
    } else {
        m_computerAnalysisLineLimit = 1;
    }

    QStringList resources;
    const QVariantMap threads = optionNamed(QStringLiteral("Threads"));
    const int effectiveThreads = boundedValue(threads, qMin(selected.threadTarget, 4), 4);
    if (effectiveThreads >= 1) {
        resources.append(QStringLiteral("Threads %1 (soft cap 4)").arg(effectiveThreads));
        m_computerAnalysisSettings.prepend(
            QStringLiteral("setoption name %1 value %2\n")
                .arg(threads.value(QStringLiteral("name")).toString())
                .arg(effectiveThreads));
    } else {
        resources.append(QStringLiteral("Threads unavailable"));
    }

    const QVariantMap hash = optionNamed(QStringLiteral("Hash"));
    const int effectiveHash = boundedValue(hash, selected.hashTarget, 256);
    if (effectiveHash >= 1) {
        resources.append(QStringLiteral("Hash %1 MB (soft cap 256 MB)").arg(effectiveHash));
        m_computerAnalysisSettings.prepend(
            QStringLiteral("setoption name %1 value %2\n")
                .arg(hash.value(QStringLiteral("name")).toString())
                .arg(effectiveHash));
    } else {
        resources.append(QStringLiteral("Hash unavailable"));
    }

    QString backendDisclosure = QStringLiteral("Backend preserved (no override)");
    if (profile) {
        for (const QVariant &value : profile->options) {
            const QVariantMap option = value.toMap();
            const QString name = option.value(QStringLiteral("name")).toString().toLower();
            if (name.contains(QStringLiteral("backend")) || name.contains(QStringLiteral("device"))
                || name.contains(QStringLiteral("gpu")) || name.contains(QStringLiteral("nnue"))
                || name.contains(QStringLiteral("network")) || name.contains(QStringLiteral("weights"))) {
                const QString defaultValue = option.value(QStringLiteral("default")).toString();
                backendDisclosure = defaultValue.isEmpty()
                    ? QStringLiteral("Backend preserved")
                    : QStringLiteral("Backend preserved: %1").arg(defaultValue);
                break;
            }
        }
    }
    resources.append(backendDisclosure);

    QString fallback;
    if (m_computerAnalysisLineLimit < requestedLines) {
        fallback = QStringLiteral(" · fallback: MultiPV capped to %1 lines")
                       .arg(m_computerAnalysisLineLimit);
    }
    const QString capabilityState = profile
        ? QString() : QStringLiteral(" · capability probe pending");
    m_computerAnalysisDisclosure = QStringLiteral(
        "%1 · %2/position · %3 requested lines · %4 effective lines · %5 · Engine limit: go movetime %6 ms · %7%8%9")
        .arg(selected.label)
        .arg(selected.milliseconds == 1000
                 ? QStringLiteral("1 s")
                 : selected.milliseconds == 5000 ? QStringLiteral("5 s")
                                                  : QStringLiteral("250 ms"))
        .arg(requestedLines)
        .arg(m_computerAnalysisLineLimit)
        .arg(selected.resources)
        .arg(selected.milliseconds)
        .arg(resources.join(QStringLiteral(", ")))
        .arg(fallback)
        .arg(capabilityState);
}

void EngineManager::updateComputerAnalysisEstimate()
{
    const Budget selected = budgetDefinition(m_computerAnalysisBudget);
    const int positions = qMax(1, m_computerAnalysisPositionCount);
    const QString profileKey = m_readyProfile >= 0
        ? m_profiles.at(m_readyProfile).key : QStringLiteral("engine");
    const double calibrated = QSettings()
        .value(QStringLiteral("analysisCalibration/%1/%2/msPerPosition")
                   .arg(profileKey, selected.key), selected.milliseconds + 150)
        .toDouble();

    if (!m_computerAnalysisActive || m_computerAnalysisPositionsCompleted <= 0
        || !m_computerAnalysisTimer.isValid()) {
        const qint64 low = qRound64(calibrated * positions * 0.8);
        const qint64 high = qRound64(calibrated * positions * 1.25);
        m_computerAnalysisEstimate = QStringLiteral("Estimate: %1–%2 for %3 positions")
                                          .arg(formatDuration(low))
                                          .arg(formatDuration(high))
                                          .arg(positions);
        return;
    }

    const qint64 elapsed = m_computerAnalysisTimer.elapsed();
    const double observedPerPosition = static_cast<double>(elapsed)
                                       / m_computerAnalysisPositionsCompleted;
    const int remainingPositions = qMax(0, positions - m_computerAnalysisPositionsCompleted);
    const qint64 remaining = qRound64(observedPerPosition * remainingPositions);
    const qint64 margin = qMax<qint64>(50, qRound64(remaining * 0.2));
    m_computerAnalysisEstimate = QStringLiteral(
        "Corrected estimate: %1–%2 remaining · observed %3/position")
        .arg(formatDuration(qMax<qint64>(0, remaining - margin)))
        .arg(formatDuration(remaining + margin))
        .arg(formatDuration(qRound64(observedPerPosition)));
}

void EngineManager::startAnalysis()
{
    if (m_readyProfile < 0 || m_requestedFen.isEmpty() || !m_requestedRuleValid)
        return;
    m_operation = Operation::Analysis;
    m_analysisMode = m_computerAnalysisActive ? AnalysisMode::Computer : AnalysisMode::Live;
    if (m_analysisMode == AnalysisMode::Computer) {
        compileComputerAnalysis();
        m_analysisTimeMs = m_computerAnalysisTimeMs;
        m_analysisLineLimit = m_computerAnalysisLineLimit;
    } else {
        m_analysisTimeMs = 250;
        m_analysisLineLimit = 3;
    }
    m_active = m_readyProfile;
    m_output.clear();
    m_searchVariations.clear();
    m_analysisDepth = 0;
    m_analysisMessage = m_analysisMode == AnalysisMode::Computer
        ? QStringLiteral("%1 · searching position %2 of %3")
              .arg(m_computerAnalysisDisclosure)
              .arg(m_computerAnalysisPositionsCompleted + 1)
              .arg(m_computerAnalysisPositionCount)
        : QStringLiteral("Analyzing…");
    emit analysisChanged();
    const Profile &profile = m_profiles.at(m_readyProfile);
    if (m_analysisMode == AnalysisMode::Computer
        && m_process.state() == QProcess::Running) {
        send("isready\n");
        advance(Stage::Ready, qMax(500, deadline(5000)));
        return;
    }
    m_process.setProgram(profile.path);
    m_process.setArguments(QProcess::splitCommand(profile.arguments));
    m_process.setWorkingDirectory(profile.workingDirectory);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.start();
    advance(Stage::Starting, m_computerAnalysisActive
                                   ? qMax(500, deadline(3000))
                                   : deadline(3000));
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
            if (m_operation == Operation::Analysis) {
                if (m_analysisMode == AnalysisMode::Computer)
                    send(m_computerAnalysisSettings.toUtf8() + "isready\n");
                else
                    send("setoption name MultiPV value 3\nisready\n");
                const int analysisReadyDeadline = m_analysisMode == AnalysisMode::Computer
                    ? qMax(500, deadline(5000)) : deadline(5000);
                advance(Stage::Ready, analysisReadyDeadline);
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
            send("position fen " + m_requestedFen.toUtf8() + "\ngo movetime "
                 + QByteArray::number(m_analysisTimeMs) + "\n");
        } else {
            send("ucinewgame\nposition startpos\ngo movetime 50\n");
        }
        const int analysisSearchDeadline = m_analysisMode == AnalysisMode::Computer
            ? qMax(500, deadline(qMax(1500, m_analysisTimeMs + 1000)))
            : deadline(qMax(1500, m_analysisTimeMs + 1000));
        advance(Stage::Search, analysisSearchDeadline);
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
    static const QRegularExpression depthPattern(QStringLiteral("(?:^| )depth (\\d+)"));
    static const QRegularExpression scorePattern(QStringLiteral("(?:^| )score (cp|mate) (-?\\d+)"));
    static const QRegularExpression pvPattern(QStringLiteral("(?:^| )pv (.+)$"));
    static const QRegularExpression rankPattern(QStringLiteral("(?:^| )multipv (\\d+)"));
    const QRegularExpressionMatch scoreMatch = scorePattern.match(line);
    const QRegularExpressionMatch pvMatch = pvPattern.match(line);
    const QRegularExpressionMatch depthMatch = depthPattern.match(line);
    if (!scoreMatch.hasMatch() || !pvMatch.hasMatch())
        return;
    if (depthMatch.hasMatch())
        m_analysisDepth = qMax(m_analysisDepth, depthMatch.captured(1).toInt());
    const QRegularExpressionMatch rankMatch = rankPattern.match(line);
    const int rank = rankMatch.hasMatch() ? rankMatch.captured(1).toInt() : 1;
    if (m_analysisMode == AnalysisMode::Computer && rank > m_analysisLineLimit)
        return;
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

QString EngineManager::analysisEngine() const
{
    if (m_readyProfile < 0 || m_readyProfile >= m_profiles.size())
        return {};
    const Profile &profile = m_profiles.at(m_readyProfile);
    return profile.identity.isEmpty() ? profile.name : profile.identity;
}

QString EngineManager::analysisSearchContext() const
{
    if (m_analysisDepth <= 0)
        return QStringLiteral("movetime %1 ms").arg(m_analysisTimeMs);
    if (m_analysisMode == AnalysisMode::Computer)
        return QStringLiteral("depth %1 · movetime %2 ms · MultiPV %3")
            .arg(m_analysisDepth).arg(m_analysisTimeMs).arg(m_analysisLineLimit);
    return QStringLiteral("depth %1 · movetime %2 ms").arg(m_analysisDepth).arg(m_analysisTimeMs);
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
                            : m_analysisMode == AnalysisMode::Computer
                                  ? QStringLiteral("%1 · position complete")
                                        .arg(m_computerAnalysisDisclosure)
                                  : QStringLiteral("Live Position Analysis");
    m_active = -1;
    m_stage = Stage::Idle;
    if (m_analysisMode == AnalysisMode::Live) {
        send("quit\n");
        stopProcess();
    }
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
        m_analysisMessage = m_analysisMode == AnalysisMode::Computer
            ? QStringLiteral("%1 · unavailable — %2").arg(m_computerAnalysisDisclosure, reason)
            : QStringLiteral("Analysis unavailable — %1").arg(reason);
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
    const int completed = m_active;
    Profile &profile = m_profiles[completed];
    profile.state = profile.identityMismatch ? QStringLiteral("Ready — identity mismatch")
                                             : QStringLiteral("Ready");
    m_readyProfile = completed;
    m_active = -1;
    m_stage = Stage::Idle;
    compileComputerAnalysis();
    updateComputerAnalysisEstimate();
    emit dataChanged(index(completed), index(completed));
    emit analysisChanged();
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
        if (!m_process.waitForFinished(50)) {
            m_process.kill();
            m_process.waitForFinished(100);
        }
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
