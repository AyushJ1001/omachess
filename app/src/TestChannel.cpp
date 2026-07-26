#include "TestChannel.h"

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>

Q_LOGGING_CATEGORY(lcTestChannel, "omachess.testchannel")

namespace {

// Items the workspace marks as squares are named "square:<coordinate>".
constexpr QLatin1String squarePrefix("square:");

// Every item currently in the window, found by walking what is drawn.
//
// The visual tree is the right tree to walk: an item declared in QML keeps its
// declaring scope as its QObject parent, so an object-tree search misses
// delegates and window decoration.
void collectItems(QQuickItem *item, QList<QQuickItem *> &into)
{
    into.append(item);
    const auto children = item->childItems();
    for (QQuickItem *child : children)
        collectItems(child, into);
}

QList<QQuickItem *> itemsIn(QQuickWindow *window)
{
    QList<QQuickItem *> items;
    collectItems(window->contentItem(), items);
    return items;
}

// The item that handles keyboard input.
//
// Qt Quick only grants *active* focus while the compositor considers the
// window active, which an automated session cannot rely on. Following the
// scoped-focus chain finds the same item a real keystroke would reach in a
// focused window, so keyboard journeys stay meaningful either way.
QQuickItem *keyboardTarget(QQuickWindow *window)
{
    if (QQuickItem *active = window->activeFocusItem())
        return active;

    QQuickItem *item = window->contentItem();
    while (QQuickItem *scoped = item->scopedFocusItem())
        item = scoped;
    return item;
}

QQuickItem *findItem(QQuickWindow *window, const QString &objectName)
{
    const auto items = itemsIn(window);
    for (QQuickItem *item : items) {
        if (item->objectName() == objectName)
            return item;
    }
    return nullptr;
}

} // namespace

TestChannel *TestChannel::listen(const QString &socketPath, QQuickWindow *window, QObject *parent)
{
    auto *channel = new TestChannel(window, parent);
    QLocalServer::removeServer(socketPath);
    if (!channel->m_server.listen(socketPath)) {
        qCWarning(lcTestChannel) << "cannot listen on" << socketPath
                                 << ":" << channel->m_server.errorString();
        delete channel;
        return nullptr;
    }
    return channel;
}

TestChannel::TestChannel(QQuickWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
    connect(&m_server, &QLocalServer::newConnection, this, &TestChannel::acceptConnection);
}

void TestChannel::acceptConnection()
{
    while (QLocalSocket *socket = m_server.nextPendingConnection()) {
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readCommands(socket); });
        connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
    }
}

void TestChannel::readCommands(QLocalSocket *socket)
{
    while (socket->canReadLine()) {
        const QByteArray line = socket->readLine().trimmed();
        if (line.isEmpty())
            continue;

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        QJsonObject reply;
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            reply = QJsonObject{{"ok", false}, {"error", parseError.errorString()}};
        } else {
            reply = handle(document.object());
        }

        socket->write(QJsonDocument(reply).toJson(QJsonDocument::Compact));
        socket->write("\n");
        socket->flush();
    }
}

QJsonObject TestChannel::handle(const QJsonObject &command)
{
    const QString name = command.value(QStringLiteral("command")).toString();

    if (name == QStringLiteral("snapshot")) {
        // Let pending layout and input work settle so the reply describes what
        // a player would now see.
        QCoreApplication::processEvents();
        return QJsonObject{{"ok", true}, {"snapshot", snapshot()}};
    }
    if (name == QStringLiteral("key")) {
        const QString key = command.value(QStringLiteral("key")).toString();
        return QJsonObject{{"ok", sendKey(key)}};
    }
    if (name == QStringLiteral("click")) {
        const QString target = command.value(QStringLiteral("target")).toString();
        return QJsonObject{{"ok", clickTarget(target)}};
    }
    if (name == QStringLiteral("resize")) {
        const int width = command.value(QStringLiteral("width")).toInt();
        const int height = command.value(QStringLiteral("height")).toInt();
        if (width <= 0 || height <= 0)
            return QJsonObject{{"ok", false}, {"error", QStringLiteral("bad size")}};
        m_window->resize(width, height);
        QCoreApplication::processEvents();
        return QJsonObject{{"ok", true}};
    }
    if (name == QStringLiteral("quit")) {
        QCoreApplication::quit();
        return QJsonObject{{"ok", true}};
    }

    return QJsonObject{{"ok", false}, {"error", QStringLiteral("unknown command: ") + name}};
}

QJsonObject TestChannel::snapshot() const
{
    QJsonArray squares;
    const auto items = itemsIn(m_window);
    for (QQuickItem *item : items) {
        if (!item->objectName().startsWith(squarePrefix))
            continue;
        const QPointF topLeft = item->mapToScene(QPointF(0, 0));
        squares.append(QJsonObject{
            {"name", item->objectName().mid(squarePrefix.size())},
            {"piece", item->property("piece").toString()},
            {"light", item->property("light").toBool()},
            {"x", topLeft.x()},
            {"y", topLeft.y()},
            {"size", item->width()},
            {"visible", item->isVisible()},
        });
    }

    return QJsonObject{
        {"appId", QGuiApplication::desktopFileName()},
        {"title", m_window->title()},
        {"visible", m_window->isVisible()},
        {"width", m_window->width()},
        {"height", m_window->height()},
        {"devicePixelRatio", m_window->devicePixelRatio()},
        {"platform", QGuiApplication::platformName()},
        {"squares", squares},
    };
}

bool TestChannel::sendKey(const QString &key)
{
    if (key.size() != 1)
        return false;

    const QChar character = key.at(0).toLower();
    if (character < QLatin1Char('a') || character > QLatin1Char('z'))
        return false;
    const int code = Qt::Key_A + (character.unicode() - u'a');

    QObject *target = keyboardTarget(m_window);
    QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier, QString(character));
    QCoreApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, code, Qt::NoModifier, QString(character));
    QCoreApplication::sendEvent(target, &release);
    QCoreApplication::processEvents();
    return true;
}

bool TestChannel::clickTarget(const QString &objectName)
{
    QQuickItem *item = findItem(m_window, objectName);
    if (!item || !item->isVisible())
        return false;

    const QPointF centre = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    const QPointF global = m_window->mapToGlobal(centre.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, centre, global, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(m_window, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, centre, global, Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QCoreApplication::sendEvent(m_window, &release);
    QCoreApplication::processEvents();
    return true;
}
