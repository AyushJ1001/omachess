#pragma once

#include <QAbstractListModel>
#include <QProcess>
#include <QQmlEngine>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

// Curated engines are discovered only at catalog-owned paths. A discovered
// executable is never started until consent is recorded for that exact path.
class EngineManager : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

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
    };

    explicit EngineManager(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void grantConsent(const QString &key);
    Q_INVOKABLE void setDisplayRating(const QString &key, int rating);
    Q_INVOKABLE void registerCustomEngine(const QUrl &path,
                                          const QString &arguments,
                                          const QString &workingDirectory);

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
    };

    enum class Stage { Idle, Starting, Uci, Ready, Search, Shutdown };

    void discover();
    void loadCustomEngine();
    void saveCustomEngine(const Profile &profile);
    QString discoverPath(const Profile &profile) const;
    int indexOf(const QString &key) const;
    void startProbe(int index);
    void send(const QByteArray &command);
    void readOutput();
    void consumeLine(const QString &line);
    void advance(Stage next, int deadlineMs);
    void fail(const QString &reason);
    void finishReady();
    bool identityMatches(const Profile &profile) const;
    void stopProcess();
    int deadline(int productionMs) const;

    QList<Profile> m_profiles;
    QProcess m_process;
    QTimer m_deadline;
    QByteArray m_output;
    Stage m_stage = Stage::Idle;
    int m_active = -1;
    bool m_registrationRequired = false;
    bool m_sawMalformedHandshake = false;
};
