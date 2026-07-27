#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#include "TestChannel.h"
extern "C" {
#include "omachess_core.h"
}

int main(int argc, char *argv[])
{
    if (argc == 3 && std::string_view(argv[1]) == "--variant-validation-worker") {
        const std::string fen{std::istreambuf_iterator<char>(std::cin),
                              std::istreambuf_iterator<char>()};
        return omachess_variant_validation_worker(argv[2], fen.c_str()) == 1 ? 0 : 1;
    }
#if defined(Q_OS_LINUX)
    // Qt's xdgdesktopportal platform theme routes native file choosers through
    // org.freedesktop.portal.FileChooser. Respect an explicit user override,
    // but make the portal the default desktop boundary on Omarchy/Linux.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME")
        && qEnvironmentVariableIsEmpty("OMACHESS_TEST_CHANNEL")) {
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
    }
#endif
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
