#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

#include "BoardModel.h"

struct OmachessSession;

// The workspace side of the command-and-event C ABI.
//
// QML calls the invokable methods to express player intent; every visible
// property here changes only when a core event says so. Nothing in the
// workspace decides what the board looks like.
class WorkspaceSession : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(BoardModel *board READ board CONSTANT)
    Q_PROPERTY(QString orientation READ orientation NOTIFY boardChanged)
    Q_PROPERTY(QString variant READ variant NOTIFY boardChanged)

public:
    explicit WorkspaceSession(QObject *parent = nullptr);
    ~WorkspaceSession() override;

    BoardModel *board() { return &m_board; }
    QString orientation() const { return m_orientation; }
    QString variant() const { return m_variant; }

    // Asks the core to describe the board it owns. Called once at startup so
    // the first frame is drawn from core-owned state.
    Q_INVOKABLE void describeBoard();

    // Player intent: swap which side is at the bottom.
    Q_INVOKABLE void flipBoard();

signals:
    void boardChanged();

private:
    // Submits a command and applies every event it produced.
    void submit(const QByteArray &commandJson);
    void applyEvent(const QByteArray &eventJson);

    OmachessSession *m_session = nullptr;
    BoardModel m_board;
    QString m_orientation;
    QString m_variant;
};
