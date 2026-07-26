#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

#include "TestChannel.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // The stable identity Omarchy uses for the launcher entry, window rules,
    // and the Wayland app ID. It must match packaging/*.desktop.
    QGuiApplication::setDesktopFileName(QStringLiteral("com.omachess.Omachess"));
    QGuiApplication::setApplicationName(QStringLiteral("Omachess"));
    QGuiApplication::setApplicationVersion(QStringLiteral(OMACHESS_VERSION));

    QQmlApplicationEngine engine;
    engine.loadFromModule("Omachess", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    // Journey tests drive the real application through this socket. Without
    // the variable the application has no control surface at all.
    const QString testChannelPath = qEnvironmentVariable("OMACHESS_TEST_CHANNEL");
    if (!testChannelPath.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        if (!window || !TestChannel::listen(testChannelPath, window, &app))
            return 1;
    }

    return app.exec();
}
