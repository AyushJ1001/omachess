#pragma once

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
    // How many of those moves the displayed position includes.
    Q_PROPERTY(int cursor READ cursor NOTIFY boardChanged)
    Q_PROPERTY(bool reviewing READ reviewing NOTIFY boardChanged)

    // The squares of the move that produced the displayed position, or empty
    // strings when the position is the start of the game.
    Q_PROPERTY(QString lastMoveFrom READ lastMoveFrom NOTIFY boardChanged)
    Q_PROPERTY(QString lastMoveTo READ lastMoveTo NOTIFY boardChanged)

    Q_PROPERTY(QString resultLabel READ resultLabel NOTIFY boardChanged)
    Q_PROPERTY(QString resultStatus READ resultStatus NOTIFY boardChanged)
    Q_PROPERTY(QString resultScore READ resultScore NOTIFY boardChanged)
    Q_PROPERTY(bool gameOver READ gameOver NOTIFY boardChanged)

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

private:
    // Submits a command and applies every event it produced.
    void submit(const QByteArray &commandJson);
    void applyEvent(const QByteArray &eventJson);

    QString field(const QString &name) const;
    QString result(const QString &name) const;
    QString lastMoveSquare(const QString &name) const;

    OmachessSession *m_session = nullptr;
    BoardModel m_board;
    // The last board_changed event, kept whole so every property answers from
    // one core-owned snapshot.
    QJsonObject m_state;
};
