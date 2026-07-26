#pragma once

#include <QJsonObject>
#include <QLocalServer>
#include <QObject>

class QLocalSocket;
class QQuickWindow;

// A control socket that lets a journey test drive the real application.
//
// It exists only when OMACHESS_TEST_CHANNEL names a socket path, and it adds
// no behaviour of its own: it synthesises ordinary input events and reports
// what is actually on screen. Tests therefore assert observable outcomes of
// the running application rather than its QML structure.
//
// The protocol is newline-delimited JSON in both directions:
//   {"command":"snapshot"}                    -> window geometry, squares, labels
//   {"command":"key","key":"f"}               -> synthesised key press/release
//   {"command":"click","target":"flipButton"} -> click the centre of an item
//   {"command":"resize","width":W,"height":H} -> resize the window
//   {"command":"quit"}                        -> exit the application
class TestChannel : public QObject
{
    Q_OBJECT

public:
    // Starts a channel at `socketPath` reporting on `window`.
    // Returns nullptr when the socket cannot be created.
    static TestChannel *listen(const QString &socketPath, QQuickWindow *window, QObject *parent);

private:
    TestChannel(QQuickWindow *window, QObject *parent);

    void acceptConnection();
    void readCommands(QLocalSocket *socket);
    QJsonObject handle(const QJsonObject &command);

    // Produces a frame, so a command acts on a fully arranged window.
    void settle();

    QJsonObject snapshot() const;
    bool sendKey(const QString &key);
    bool clickTarget(const QString &objectName);

    QLocalServer m_server;
    QQuickWindow *m_window;
};
