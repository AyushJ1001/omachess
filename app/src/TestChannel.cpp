#include "TestChannel.h"

#include "ThemeController.h"

#include <QAccessible>
#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

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

QString colorHex(const QColor &color)
{
    return color.name(QColor::HexRgb);
}

// The AT-SPI role names an assistive technology would read, for the roles the
// workspace actually uses. Anything else is reported by number, so a missing
// name is visible rather than silently "none".
QString roleName(QAccessible::Role role)
{
    switch (role) {
    case QAccessible::NoRole: return QStringLiteral("none");
    case QAccessible::Button: return QStringLiteral("button");
    case QAccessible::CheckBox: return QStringLiteral("checkbox");
    case QAccessible::ComboBox: return QStringLiteral("combobox");
    case QAccessible::Dialog: return QStringLiteral("dialog");
    case QAccessible::EditableText: return QStringLiteral("editabletext");
    case QAccessible::Grouping: return QStringLiteral("grouping");
    case QAccessible::Heading: return QStringLiteral("heading");
    case QAccessible::List: return QStringLiteral("list");
    case QAccessible::ListItem: return QStringLiteral("listitem");
    case QAccessible::PageTab: return QStringLiteral("pagetab");
    case QAccessible::PageTabList: return QStringLiteral("pagetablist");
    case QAccessible::Pane: return QStringLiteral("pane");
    case QAccessible::SpinBox: return QStringLiteral("spinbox");
    case QAccessible::StaticText: return QStringLiteral("statictext");
    case QAccessible::AlertMessage: return QStringLiteral("alert");
    case QAccessible::Graphic: return QStringLiteral("graphic");
    case QAccessible::Table: return QStringLiteral("table");
    case QAccessible::Cell: return QStringLiteral("cell");
    default: return QStringLiteral("role-%1").arg(static_cast<int>(role));
    }
}

// What an assistive technology can learn about one item.
//
// The accessible interface is the real seam, so it is asked first; the
// declared attached properties answer for items whose interface has not been
// realised yet. Either way the answer is the public semantics, never the item.
QJsonObject semanticsOf(QQuickItem *item)
{
    QString name;
    QString description;
    QString role;

    if (QAccessibleInterface *interface = QAccessible::queryAccessibleInterface(item)) {
        if (interface->role() != QAccessible::NoRole)
            role = roleName(interface->role());
        name = interface->text(QAccessible::Name);
        description = interface->text(QAccessible::Description);
    }
    if (role.isEmpty()) {
        const QVariant declared = QQmlProperty(item, QStringLiteral("Accessible.role"),
                                               qmlContext(item)).read();
        if (declared.isValid())
            role = roleName(static_cast<QAccessible::Role>(declared.toInt()));
    }
    if (name.isEmpty()) {
        name = QQmlProperty(item, QStringLiteral("Accessible.name"), qmlContext(item))
                   .read().toString();
    }
    if (description.isEmpty()) {
        description = QQmlProperty(item, QStringLiteral("Accessible.description"), qmlContext(item))
                          .read().toString();
    }

    QJsonObject semantics{
        {"role", role.isEmpty() ? QStringLiteral("none") : role},
        {"name", name},
        {"description", description},
        {"enabled", item->isEnabled()},
        {"visible", item->isVisible()},
        {"focusable", item->activeFocusOnTab()},
        {"focused", item->hasActiveFocus()},
    };
    const QVariant checked = item->property("checked");
    if (checked.isValid() && checked.canConvert<bool>())
        semantics.insert("checked", checked.toBool());
    return semantics;
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
    // An assistive technology switches accessibility on when it attaches. A
    // journey asserts what such a technology would find, so it attaches too.
    QAccessible::setActive(true);
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

void TestChannel::settle()
{
    // Layout and item creation are finished during a frame, not when the event
    // that caused them is delivered. Producing one frame therefore makes every
    // command act on the window a player would be looking at, rather than on a
    // half-arranged one.
    QCoreApplication::processEvents();
    m_window->grabWindow();
    QCoreApplication::processEvents();
}

QJsonObject TestChannel::handle(const QJsonObject &command)
{
    const QString name = command.value(QStringLiteral("command")).toString();

    if (name == QStringLiteral("snapshot")) {
        settle();
        return QJsonObject{{"ok", true}, {"snapshot", snapshot()}};
    }
    if (name == QStringLiteral("key")) {
        settle();
        const QString key = command.value(QStringLiteral("key")).toString();
        return QJsonObject{{"ok", sendKey(key)}};
    }
    if (name == QStringLiteral("click")) {
        settle();
        const QString target = command.value(QStringLiteral("target")).toString();
        return QJsonObject{{"ok", clickTarget(target)}};
    }
    if (name == QStringLiteral("enter_text")) {
        settle();
        return QJsonObject{{"ok", enterText(command.value(QStringLiteral("target")).toString(),
                                             command.value(QStringLiteral("text")).toString())}};
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
    if (name == QStringLiteral("set_text")) {
        settle();
        const QString target = command.value(QStringLiteral("target")).toString();
        QQuickItem *item = findItem(m_window, target);
        if (!item || !item->isVisible())
            return QJsonObject{{"ok", false}, {"error", QStringLiteral("missing text target")}};
        item->setProperty("text", command.value(QStringLiteral("text")).toString());
        QCoreApplication::processEvents();
        return QJsonObject{{"ok", true}};
    }
    if (name == QStringLiteral("select")) {
        settle();
        QQuickItem *item =
            findItem(m_window, command.value(QStringLiteral("target")).toString());
        const int index = command.value(QStringLiteral("index")).toInt(-1);
        if (!item || !item->isVisible() || index < 0)
            return QJsonObject{{"ok", false}, {"error", QStringLiteral("missing picker")}};
        item->setProperty("currentIndex", index);
        const bool invoked = QMetaObject::invokeMethod(item, "activated", Q_ARG(int, index));
        QCoreApplication::processEvents();
        return QJsonObject{{"ok", invoked}};
    }
    if (name == QStringLiteral("close_window")) {
        m_window->close();
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
            {"color", colorHex(item->property("color").value<QColor>())},
            // The marks a player can see on the square: where a picked-up
            // piece came from, where it may go, and the move just played.
            {"selected", item->property("selected").toBool()},
            {"target", item->property("target").toBool()},
            {"lastMove", item->property("lastMove").toBool()},
            {"footprint", item->property("footprint").toString()},
            // Whether the Piece Set artwork is loaded, and which file drew it.
            {"artworkReady", item->property("artworkReady").toBool()},
            {"artworkSource", item->property("artworkSource").toString()},
            {"x", topLeft.x()},
            {"y", topLeft.y()},
            {"size", item->width()},
            {"visible", item->isVisible()},
        });
    }

    // The text of every named item that shows any, so a journey can read the
    // status line and the move list the way a player reads them.
    QJsonObject labels;
    for (QQuickItem *item : items) {
        const QString name = item->objectName();
        if (name.isEmpty() || name.startsWith(squarePrefix))
            continue;
        const QVariant text = item->property("text");
        if (text.isValid() && text.canConvert<QString>() && item->isVisible())
            labels.insert(name, text.toString());
    }

    // What an assistive technology finds on every named chrome item: its role,
    // its name, and the states that decide whether it can be reached.
    QJsonObject semantics;
    for (QQuickItem *item : items) {
        const QString name = item->objectName();
        if (name.isEmpty())
            continue;
        semantics.insert(name, semanticsOf(item));
    }

    // Theme roles the workspace is currently painting. Journeys assert these
    // against the Quattro Palette fixture they installed, not against adapter
    // internals.
    ThemeController *activeTheme = ThemeController::instance();

    const QQuickItem *focused = keyboardTarget(m_window);

    const QString chromeBackground = activeTheme ? colorHex(activeTheme->background())
                                                 : colorHex(m_window->color());
    const QString chromeForeground =
        activeTheme ? colorHex(activeTheme->foreground()) : QStringLiteral("#000000");
    const QString lightSquare =
        activeTheme ? colorHex(activeTheme->lightSquare()) : QStringLiteral("#ebecd0");
    const QString darkSquare =
        activeTheme ? colorHex(activeTheme->darkSquare()) : QStringLiteral("#739552");
    const QString paletteSource =
        activeTheme ? activeTheme->paletteSource() : QStringLiteral("builtin");
    const QString themeName = activeTheme ? activeTheme->themeName() : QString();
    const QString boardThemeId =
        activeTheme ? activeTheme->boardThemeId() : QStringLiteral("follow");
    const QString pieceSetId =
        activeTheme ? activeTheme->pieceSetId() : QStringLiteral("cburnett");

    // The discrete announcements the workspace has made, in order, exactly as
    // an assistive technology would have heard them.
    QJsonArray announcements;
    for (const QVariant &announcement : m_window->property("announcements").toList())
        announcements.append(announcement.toString());

    QJsonObject contrast;
    if (activeTheme) {
        const QVariantMap report = activeTheme->contrastReport();
        for (auto entry = report.constBegin(); entry != report.constEnd(); ++entry)
            contrast.insert(entry.key(), entry.value().toDouble());
    }

    return QJsonObject{
        {"appId", QGuiApplication::desktopFileName()},
        {"title", m_window->title()},
        {"visible", m_window->isVisible()},
        {"width", m_window->width()},
        {"height", m_window->height()},
        {"devicePixelRatio", m_window->devicePixelRatio()},
        {"platform", QGuiApplication::platformName()},
        {"chromeBackground", chromeBackground},
        {"chromeForeground", chromeForeground},
        {"lightSquare", lightSquare},
        {"darkSquare", darkSquare},
        {"paletteSource", paletteSource},
        {"themeName", themeName},
        {"boardThemeId", boardThemeId},
        {"pieceSetId", pieceSetId},
        {"activeRecordId", m_window->property("activeRecordId").toString()},
        {"activeFocus", focused ? focused->objectName() : QString()},
        {"layoutMode", m_window->property("layoutMode").toString()},
        {"squares", squares},
        {"labels", labels},
        {"semantics", semantics},
        {"announcements", announcements},
        {"contrast", contrast},
    };
}

bool TestChannel::sendKey(const QString &key)
{
    const QKeySequence sequence = QKeySequence::fromString(key, QKeySequence::PortableText);
    if (sequence.isEmpty())
        return false;
    const QKeyCombination combination = sequence[0];
    const int code = combination.key();
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    m_window->requestActivate();
    QCoreApplication::processEvents();
    QTest::keyClick(m_window, static_cast<Qt::Key>(code), modifiers);
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

bool TestChannel::enterText(const QString &objectName, const QString &text)
{
    QQuickItem *item = findItem(m_window, objectName);
    if (!item || !item->isVisible() || !item->setProperty("text", text))
        return false;
    QCoreApplication::processEvents();
    return true;
}
