#pragma once

#include <QAbstractListModel>
#include <QProcess>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>

// Curated engines are discovered only at catalog-owned paths. A discovered
// executable is never started until consent is recorded for that exact path.
class EngineManager : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
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
    };

    explicit EngineManager(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void grantConsent(const QString &key);
    Q_INVOKABLE void setDisplayRating(const QString &key, int rating);
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

    void discover();
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
    void failLivePlay(const QString &reason);
    void requestLiveMove();

    QList<Profile> m_profiles;
    QProcess m_process;
    QTimer m_deadline;
    QByteArray m_output;
    Stage m_stage = Stage::Idle;
    int m_active = -1;
    bool m_registrationRequired = false;
    bool m_sawMalformedHandshake = false;
    bool m_livePlayActive = false;
    QString m_livePlayEngineSide;
    QString m_livePlayStatus;
    QString m_liveMoves;
    QString m_liveSideToMove;
    int m_liveWhiteMs = 0;
    int m_liveBlackMs = 0;
    int m_liveSearchTimeMs = 250;
};
