#pragma once

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QFile>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QQmlEngine>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QMap>

// Curated engines are discovered only at catalog-owned paths. A discovered
// executable is never started until consent is recorded for that exact path.
class EngineManager : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool analysisReady READ analysisReady NOTIFY analysisChanged)
    Q_PROPERTY(bool analyzing READ analyzing NOTIFY analysisChanged)
    Q_PROPERTY(QString analysisEvaluation READ analysisEvaluation NOTIFY analysisChanged)
    Q_PROPERTY(QStringList analysisVariations READ analysisVariations NOTIFY analysisChanged)
    Q_PROPERTY(QString analysisMessage READ analysisMessage NOTIFY analysisChanged)
    Q_PROPERTY(QString analysisEngine READ analysisEngine NOTIFY analysisChanged)
    Q_PROPERTY(QString analysisSearchContext READ analysisSearchContext NOTIFY analysisChanged)
    Q_PROPERTY(QString computerAnalysisBudget READ computerAnalysisBudget NOTIFY analysisChanged)
    Q_PROPERTY(QString computerAnalysisDisclosure READ computerAnalysisDisclosure NOTIFY analysisChanged)
    Q_PROPERTY(QString computerAnalysisEstimate READ computerAnalysisEstimate NOTIFY analysisChanged)
    Q_PROPERTY(bool livePlayActive READ livePlayActive NOTIFY livePlayChanged)
    Q_PROPERTY(QString livePlayEngineSide READ livePlayEngineSide NOTIFY livePlayChanged)
    Q_PROPERTY(QString livePlayStatus READ livePlayStatus NOTIFY livePlayChanged)

public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        NameRole,
        StateRole,
        IdentityRole,
        AuthorRole,
        OptionCountRole,
        RatingRole,
        ArtworkRole,
        ArtworkProvenanceRole,
        FoundRole,
        ConsentRequiredRole,
        InstallOfferedRole,
        ExecutablePathRole,
        LaunchArgumentsRole,
        LaunchWorkingDirectoryRole,
        CapabilitiesRole,
        CustomRole,
        InstallingRole,
    };

    explicit EngineManager(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void grantConsent(const QString &key);
    Q_INVOKABLE void install(const QString &key);
    Q_INVOKABLE void cancelInstall(const QString &key);
    Q_INVOKABLE void setDisplayRating(const QString &key, int rating);
    Q_INVOKABLE void analyzePosition(const QString &fen, bool ruleValid);
    Q_INVOKABLE void clearAnalysis();
    Q_INVOKABLE void setComputerAnalysisBudget(const QString &budget, int positionCount = -1);
    Q_INVOKABLE void beginComputerAnalysis(const QString &budget, int positionCount);
    Q_INVOKABLE void recordComputerAnalysisPosition();
    Q_INVOKABLE void endComputerAnalysis();
    Q_INVOKABLE int computerAnalysisSearchTimeMs() const { return m_computerAnalysisTimeMs; }
    Q_INVOKABLE int computerAnalysisLineLimit() const { return m_computerAnalysisLineLimit; }
    Q_INVOKABLE QString computerAnalysisSearchSettings() const { return m_computerAnalysisSettings; }
    Q_INVOKABLE void setLivePlaySearchTime(const QString &key, int milliseconds);
    Q_INVOKABLE int livePlaySearchTime(const QString &key) const;
    Q_INVOKABLE void setLivePlayClock(const QString &key, int milliseconds);
    Q_INVOKABLE int livePlayClock(const QString &key) const;
    Q_INVOKABLE void startLivePlay(const QString &key, const QString &humanSide);
    Q_INVOKABLE void updateLivePosition(const QString &moves, const QString &sideToMove,
                                        bool gameOver, int whiteMs, int blackMs);
    Q_INVOKABLE void rejectLiveMove();
    Q_INVOKABLE void stopLivePlay();

    bool livePlayActive() const { return m_livePlayActive; }
    QString livePlayEngineSide() const { return m_livePlayEngineSide; }
    QString livePlayStatus() const { return m_livePlayStatus; }

signals:
    void livePlayChanged();
    void liveMove(const QString &from, const QString &to, const QString &promotion);

public:
    Q_INVOKABLE void registerCustomEngine(const QUrl &path,
                                           const QString &arguments,
                                           const QString &workingDirectory);

    bool analysisReady() const { return !m_analysisEvaluation.isEmpty(); }
    bool analyzing() const { return m_operation == Operation::Analysis && m_active >= 0; }
    QString analysisEvaluation() const { return m_analysisEvaluation; }
    QStringList analysisVariations() const { return m_analysisVariations; }
    QString analysisMessage() const { return m_analysisMessage; }
    QString analysisEngine() const;
    QString analysisSearchContext() const;
    QString computerAnalysisBudget() const { return m_computerAnalysisBudget; }
    QString computerAnalysisDisclosure() const { return m_computerAnalysisDisclosure; }
    QString computerAnalysisEstimate() const { return m_computerAnalysisEstimate; }

signals:
    void analysisChanged();

private:
    struct Profile {
        QString key;
        QString name;
        QStringList executableNames;
        QStringList identityAliases;
        QString state = QStringLiteral("Not found");
        QString path;
        QString identity;
        QString author;
        QString artwork;
        QString artworkProvenance;
        QVariantList options;
        int optionCount = 0;
        int rating = 0;
        bool found = false;
        bool detectOnly = false;
        bool identityMismatch = false;
        bool custom = false;
        QString arguments;
        QString workingDirectory;
        QStringList capabilities;
        QString upstreamUrl;
    };

    enum class Stage {
        Idle,
        Starting,
        Uci,
        Ready,
        Search,
        Shutdown,
        LiveStarting,
        LiveUci,
        LiveReady,
        LiveSearch
    };
    enum class Operation { Probe, Analysis };
    enum class AnalysisMode { Live, Computer };

    struct Budget {
        QString key;
        QString label;
        int milliseconds = 0;
        int lines = 0;
        QString resources;
        int threadTarget = 1;
        int hashTarget = 16;
    };

    void discover();
    void loadCustomEngine();
    void saveCustomEngine(const Profile &profile);
    QString discoverPath(const Profile &profile) const;
    int indexOf(const QString &key) const;
    void startProbe(int index);
    void startAnalysis();
    void finishAnalysis();
    void consumeAnalysisInfo(const QString &line);
    void send(const QByteArray &command);
    void readOutput();
    void consumeLine(const QString &line);
    void advance(Stage next, int deadlineMs);
    void fail(const QString &reason);
    void finishReady();
    bool identityMatches(const Profile &profile) const;
    void stopProcess();
    void finishInstall();
    void failInstall(const QString &reason);
    QString storeDirectory(const Profile &profile) const;
    int deadline(int productionMs) const;
    void failLivePlay(const QString &reason);
    void requestLiveMove();
    Budget budgetDefinition(const QString &key) const;
    void compileComputerAnalysis();
    void updateComputerAnalysisEstimate();
    QString formatDuration(qint64 milliseconds) const;

    QList<Profile> m_profiles;
    QProcess m_process;
    QTimer m_deadline;
    QByteArray m_output;
    Stage m_stage = Stage::Idle;
    int m_active = -1;
    int m_readyProfile = -1;
    Operation m_operation = Operation::Probe;
    bool m_registrationRequired = false;
    bool m_sawMalformedHandshake = false;
    QString m_requestedFen;
    bool m_requestedRuleValid = true;
    QString m_analysisEvaluation;
    QStringList m_analysisVariations;
    QString m_analysisMessage;
    int m_analysisDepth = 0;
    QMap<int, QString> m_searchVariations;
    AnalysisMode m_analysisMode = AnalysisMode::Live;
    int m_analysisTimeMs = 250;
    int m_analysisLineLimit = 3;
    int m_computerAnalysisTimeMs = 1000;
    int m_computerAnalysisLineLimit = 2;
    QString m_computerAnalysisBudget = QStringLiteral("standard");
    QString m_computerAnalysisDisclosure;
    QString m_computerAnalysisEstimate;
    QString m_computerAnalysisSettings;
    bool m_computerAnalysisActive = false;
    int m_computerAnalysisPositionCount = 0;
    int m_computerAnalysisPositionsCompleted = 0;
    QElapsedTimer m_computerAnalysisTimer;
    bool m_livePlayActive = false;
    QString m_livePlayEngineSide;
    QString m_livePlayStatus;
    QString m_liveMoves;
    QString m_liveSideToMove;
    int m_liveWhiteMs = 0;
    int m_liveBlackMs = 0;
    int m_liveSearchTimeMs = 250;
    QNetworkAccessManager m_network;
    QNetworkReply *m_download = nullptr;
    QFile m_downloadFile;
    int m_installing = -1;
};
