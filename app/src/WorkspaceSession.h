#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "BoardModel.h"

struct OmachessSession;

// The workspace side of the command-and-event C ABI.
//
// QML calls the invokable methods to express player intent; every visible
// property here changes only when a core event says so. Nothing in the
// workspace decides what the board looks like, which moves exist, or what a
// game's result is — it reads the answers the core sent and draws them.
class WorkspaceSession : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(BoardModel *board READ board CONSTANT)
    Q_PROPERTY(QString orientation READ orientation NOTIFY boardChanged)
    Q_PROPERTY(QString variant READ variant NOTIFY boardChanged)
    Q_PROPERTY(QString sideToMove READ sideToMove NOTIFY boardChanged)
    Q_PROPERTY(bool inCheck READ inCheck NOTIFY boardChanged)

    // The played moves, as {number, side, san} entries in playing order.
    Q_PROPERTY(QVariantList moveList READ moveList NOTIFY boardChanged)
    // How many of those moves the Displayed Position includes.
    Q_PROPERTY(int cursor READ cursor NOTIFY boardChanged)
    Q_PROPERTY(bool reviewing READ reviewing NOTIFY boardChanged)

    // The squares of the move that produced the Displayed Position, or empty
    // strings when the position is the start of the game.
    Q_PROPERTY(QString lastMoveFrom READ lastMoveFrom NOTIFY boardChanged)
    Q_PROPERTY(QString lastMoveTo READ lastMoveTo NOTIFY boardChanged)

    Q_PROPERTY(QString resultLabel READ resultLabel NOTIFY boardChanged)
    Q_PROPERTY(QString resultStatus READ resultStatus NOTIFY boardChanged)
    Q_PROPERTY(QString resultScore READ resultScore NOTIFY boardChanged)
    Q_PROPERTY(bool gameOver READ gameOver NOTIFY boardChanged)
    Q_PROPERTY(bool clockEnabled READ clockEnabled NOTIFY boardChanged)
    Q_PROPERTY(bool clockRunning READ clockRunning NOTIFY boardChanged)
    Q_PROPERTY(bool gameSuspended READ gameSuspended NOTIFY boardChanged)
    Q_PROPERTY(bool canSuspendGame READ canSuspendGame NOTIFY boardChanged)
    Q_PROPERTY(int whiteClockMs READ whiteClockMs NOTIFY boardChanged)
    Q_PROPERTY(int blackClockMs READ blackClockMs NOTIFY boardChanged)
    Q_PROPERTY(QString whitePlayer READ whitePlayer NOTIFY boardChanged)
    Q_PROPERTY(QString blackPlayer READ blackPlayer NOTIFY boardChanged)
    Q_PROPERTY(QString gameEvent READ gameEvent NOTIFY boardChanged)
    Q_PROPERTY(QString gameDate READ gameDate NOTIFY boardChanged)
    Q_PROPERTY(QString gameTitle READ gameTitle NOTIFY boardChanged)
    Q_PROPERTY(QString gameTags READ gameTags NOTIFY boardChanged)
    Q_PROPERTY(bool positionSetup READ positionSetup NOTIFY boardChanged)
    Q_PROPERTY(QString positionClass READ positionClass NOTIFY boardChanged)
    Q_PROPERTY(QString setupFen READ setupFen NOTIFY boardChanged)
    Q_PROPERTY(QString setupError READ setupError NOTIFY boardChanged)
    Q_PROPERTY(QString positionCapabilities READ positionCapabilities NOTIFY boardChanged)

    // Personal Library summaries from the Live Store.
    Q_PROPERTY(QVariantList libraryRecords READ libraryRecords NOTIFY libraryChanged)
    // Open record tabs and the active Game Record id.
    Q_PROPERTY(QVariantList openTabs READ openTabs NOTIFY tabsChanged)
    Q_PROPERTY(QString activeRecordId READ activeRecordId NOTIFY tabsChanged)

    // Shown when a prior Game Record can be restored after restart.
    Q_PROPERTY(bool restoreAvailable READ restoreAvailable NOTIFY restoreChanged)
    Q_PROPERTY(QString restoreLabel READ restoreLabel NOTIFY restoreChanged)
    Q_PROPERTY(QString storeError READ storeError CONSTANT)

public:
    explicit WorkspaceSession(QObject *parent = nullptr);
    ~WorkspaceSession() override;

    BoardModel *board() { return &m_board; }
    QString orientation() const { return field(QStringLiteral("orientation")); }
    QString variant() const { return field(QStringLiteral("variant")); }
    QString sideToMove() const { return field(QStringLiteral("sideToMove")); }
    bool inCheck() const { return m_state.value(QStringLiteral("inCheck")).toBool(); }

    QVariantList moveList() const;
    int cursor() const { return m_state.value(QStringLiteral("cursor")).toInt(); }
    bool reviewing() const { return m_state.value(QStringLiteral("reviewing")).toBool(); }

    QString lastMoveFrom() const { return lastMoveSquare(QStringLiteral("from")); }
    QString lastMoveTo() const { return lastMoveSquare(QStringLiteral("to")); }

    QString resultLabel() const { return result(QStringLiteral("label")); }
    QString resultStatus() const { return result(QStringLiteral("status")); }
    QString resultScore() const { return result(QStringLiteral("score")); }
    bool gameOver() const;
    bool clockEnabled() const { return clockField(QStringLiteral("enabled")).toBool(); }
    bool clockRunning() const { return clockField(QStringLiteral("running")).toBool(); }
    bool gameSuspended() const { return m_state.value(QStringLiteral("suspended")).toBool(); }
    bool canSuspendGame() const { return m_state.value(QStringLiteral("canSuspend")).toBool(); }
    int whiteClockMs() const { return clockField(QStringLiteral("whiteMs")).toInt(); }
    int blackClockMs() const { return clockField(QStringLiteral("blackMs")).toInt(); }
    QString whitePlayer() const { return metadataField(QStringLiteral("white")); }
    QString blackPlayer() const { return metadataField(QStringLiteral("black")); }
    QString gameEvent() const { return metadataField(QStringLiteral("event")); }
    QString gameDate() const { return metadataField(QStringLiteral("date")); }
    QString gameTitle() const { return metadataField(QStringLiteral("title")); }
    QString gameTags() const { return metadataField(QStringLiteral("tags")); }
    bool positionSetup() const { return field(QStringLiteral("activity")) == QStringLiteral("position_setup"); }
    QString positionClass() const { return field(QStringLiteral("positionClass")); }
    QString setupFen() const { return field(QStringLiteral("setupFen")); }
    QString setupError() const { return field(QStringLiteral("setupError")); }
    QString positionCapabilities() const { return field(QStringLiteral("positionCapabilities")); }

    QVariantList libraryRecords() const { return m_libraryRecords; }
    QVariantList openTabs() const { return m_openTabs; }
    QString activeRecordId() const { return m_activeRecordId; }

    bool restoreAvailable() const { return m_restoreAvailable; }
    QString restoreLabel() const { return m_restoreLabel; }
    QString storeError() const { return m_storeError; }

    // Asks the core to describe the board it owns. Called once at startup so
    // the first frame is drawn from core-owned state.
    Q_INVOKABLE void describeBoard();

    // Player intent: swap which side is at the bottom.
    Q_INVOKABLE void flipBoard();

    // Player intent: play a move. `promotion` names the piece a promoting
    // pawn becomes and is empty for every other move. The core decides
    // whether the move actually happens.
    Q_INVOKABLE void playMove(const QString &from, const QString &to,
                              const QString &promotion = QString());

    // Player intent: show a different position of this game. `destination` is
    // "backward", "forward", "start", or "end".
    Q_INVOKABLE void navigate(const QString &destination);

    // Player intent: restore the Game Record offered after restart.
    Q_INVOKABLE void restoreRecord();
    Q_INVOKABLE void suspendGame();
    Q_INVOKABLE void resumeGame();

    // Player intent: decline the restore offer and keep the fresh board.
    Q_INVOKABLE void dismissRestore();

    // Player intent: clear the board so the next move starts a new Game Record.
    Q_INVOKABLE void newGame();

    // Player intent: open a Personal Library record in a tab (or focus it).
    Q_INVOKABLE void openRecord(const QString &id);

    // Player intent: close a tab without removing the record from the library.
    Q_INVOKABLE void closeTab(const QString &id);
    Q_INVOKABLE void configureClock(int milliseconds);
    Q_INVOKABLE void tickClock();
    Q_INVOKABLE void updateMetadata(const QString &white, const QString &black,
                                    const QString &event, const QString &date,
                                    const QString &title, const QString &tags);
    Q_INVOKABLE void beginPositionSetup();
    Q_INVOKABLE void setSetupFen(const QString &fen);
    Q_INVOKABLE void placeSetupPiece(const QString &square, const QString &piece);
    Q_INVOKABLE void relocateSetupPiece(const QString &from, const QString &to);
    Q_INVOKABLE void startSetupGame();

    // --- What the core said a player may do -------------------------------
    //
    // These read the moves the last event carried. They are how the board
    // knows what a player may pick up and where they may drop it; the core
    // still refuses anything else that reaches it.

    // The coordinate of the square the core placed at display position
    // `index`, or "" when there is no such square. Display position 0 is the
    // top-left square of the board as it is currently drawn.
    Q_INVOKABLE QString squareNameAt(int index) const;

    // The piece the core placed on `square`, or "" when it is empty.
    Q_INVOKABLE QString pieceOn(const QString &square) const;

    // Whether a piece on `square` has anywhere to go.
    Q_INVOKABLE bool canPickUp(const QString &square) const;

    // The squares a piece on `from` may be dropped on.
    Q_INVOKABLE QStringList destinationsFrom(const QString &from) const;

    // The pieces a pawn moving from `from` to `to` may become, or an empty
    // list when that move is not a promotion.
    Q_INVOKABLE QStringList promotionsFor(const QString &from, const QString &to) const;

signals:
    void boardChanged();
    void libraryChanged();
    void tabsChanged();
    void restoreChanged();

private:
    // Submits a command and applies every event it produced.
    void submit(const QByteArray &commandJson);
    void applyEvent(const QByteArray &eventJson);

    // The moves the last event said a player may make.
    QJsonArray movesOffered() const;

    QString field(const QString &name) const;
    QString result(const QString &name) const;
    QString lastMoveSquare(const QString &name) const;
    QJsonValue clockField(const QString &name) const;
    QString metadataField(const QString &name) const;

    OmachessSession *m_session = nullptr;
    BoardModel m_board;
    // The last board_changed event, kept whole so every property answers from
    // one core-owned snapshot.
    QJsonObject m_state;
    QVariantList m_libraryRecords;
    QVariantList m_openTabs;
    QString m_activeRecordId;
    bool m_restoreAvailable = false;
    QString m_restoreLabel;
    QString m_storeError;
};
