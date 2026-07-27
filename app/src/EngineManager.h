#pragma once

#include <QAbstractListModel>
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
    Q_INVOKABLE void registerCustomEngine(const QUrl &path,
                                           const QString &arguments,
                                           const QString &workingDirectory);

    bool analysisReady() const { return !m_analysisEvaluation.isEmpty(); }
    bool analyzing() const { return m_operation == Operation::Analysis && m_active >= 0; }
    QString analysisEvaluation() const { return m_analysisEvaluation; }
    QStringList analysisVariations() const { return m_analysisVariations; }
    QString analysisMessage() const { return m_analysisMessage; }

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

    enum class Stage { Idle, Starting, Uci, Ready, Search, Shutdown };
    enum class Operation { Probe, Analysis };

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
    QMap<int, QString> m_searchVariations;
    QNetworkAccessManager m_network;
    QNetworkReply *m_download = nullptr;
    QFile m_downloadFile;
    int m_installing = -1;
};
