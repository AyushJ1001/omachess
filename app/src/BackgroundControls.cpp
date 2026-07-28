#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStringList>
#include <QVariantMap>
#include <cstdio>

namespace {

QDBusInterface worker()
{
    return QDBusInterface(QStringLiteral("com.omachess.Omachess.BackgroundWorker"),
                          QStringLiteral("/BackgroundJobs"),
                          QStringLiteral("com.omachess.Omachess.BackgroundJobs"),
                          QDBusConnection::sessionBus());
}

int usage(const QString &error = {})
{
    if (!error.isEmpty())
        qCritical().noquote() << error;
    qCritical().noquote() << "usage: omachess-background-control jobs"
                              " | control <pause|resume|cancel|dismiss> <job-id>"
                              " | notify <complete|failed> <job-id> <updated-at> <title> <body>";
    return 2;
}

int listJobs()
{
    QDBusInterface interface = worker();
    const QDBusReply<QString> reply = interface.call(QStringLiteral("Jobs"));
    if (!reply.isValid() || reply.value().isEmpty()) {
        qCritical().noquote() << "The Omachess background worker is unavailable:" << reply.error().message();
        return 1;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(reply.value().toUtf8(), &error);
    if (!document.isArray()) {
        qCritical().noquote() << "The background worker returned invalid job data:" << error.errorString();
        return 1;
    }
    const QByteArray output = QJsonDocument(document.array()).toJson(QJsonDocument::Compact);
    const auto written = fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout);
    if (written != static_cast<size_t>(output.size()))
        return 1;
    fputc('\n', stdout);
    return 0;
}

int control(const QString &action, const QString &id)
{
    const QHash<QString, QString> methods{
        {QStringLiteral("pause"), QStringLiteral("Pause")},
        {QStringLiteral("resume"), QStringLiteral("ResumeDefault")},
        {QStringLiteral("cancel"), QStringLiteral("Cancel")},
        {QStringLiteral("dismiss"), QStringLiteral("Dismiss")},
    };
    if (!methods.contains(action) || id.isEmpty())
        return usage(QStringLiteral("Unknown or incomplete background-job control."));

    QDBusInterface interface = worker();
    QDBusReply<bool> reply;
    // ResumeDefault restores the execution settings captured by the worker at
    // job creation. The shell never supplies or stores execution state.
    reply = interface.call(methods.value(action), id);
    if (!reply.isValid() || !reply.value()) {
        qCritical().noquote() << "Background-job control failed:" << reply.error().message();
        return 1;
    }
    return 0;
}

int notify(const QString &state,
           const QString &id,
           const QString &updatedAt,
           const QString &title,
           const QString &body)
{
    QSettings notificationState;
    notificationState.beginGroup(QStringLiteral("backgroundControls/notifications"));
    if (!id.isEmpty() && !updatedAt.isEmpty()
        && notificationState.value(id).toString() == updatedAt) {
        notificationState.endGroup();
        return 0;
    }
    notificationState.endGroup();

    QDBusInterface interface(QStringLiteral("org.freedesktop.Notifications"),
                             QStringLiteral("/org/freedesktop/Notifications"),
                             QStringLiteral("org.freedesktop.Notifications"),
                             QDBusConnection::sessionBus());
    if (!interface.isValid()) {
        qCritical().noquote() << "The desktop notification service is unavailable.";
        return 1;
    }

    // Query capabilities before sending. The plugin uses only the baseline
    // Notify call, so optional actions/persistence never become a dependency.
    const QDBusReply<QStringList> capabilities = interface.call(QStringLiteral("GetCapabilities"));
    if (!capabilities.isValid())
        qWarning().noquote() << "Notification capabilities could not be queried:" << capabilities.error().message();

    QVariantMap hints;
    hints.insert(QStringLiteral("desktop-entry"), QStringLiteral("com.omachess.Omachess"));
    hints.insert(QStringLiteral("urgency"), static_cast<uchar>(state == QStringLiteral("failed") ? 2 : 0));
    const QDBusReply<uint> reply = interface.call(QStringLiteral("Notify"),
                                                  QStringLiteral("Omachess"),
                                                  0u,
                                                  QStringLiteral("com.omachess.Omachess"),
                                                  title,
                                                  body,
                                                  QStringList(),
                                                  hints,
                                                  5000);
    if (!reply.isValid()) {
        qCritical().noquote() << "The desktop notification could not be sent:" << reply.error().message();
        return 1;
    }
    if (!id.isEmpty() && !updatedAt.isEmpty()) {
        notificationState.beginGroup(QStringLiteral("backgroundControls/notifications"));
        notificationState.setValue(id, updatedAt);
        notificationState.endGroup();
        notificationState.sync();
    }
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() < 2)
        return usage();
    if (arguments[1] == QStringLiteral("jobs") && arguments.size() == 2)
        return listJobs();
    if (arguments[1] == QStringLiteral("control") && arguments.size() == 4)
        return control(arguments[2], arguments[3]);
    if (arguments[1] == QStringLiteral("notify") && arguments.size() == 7)
        return notify(arguments[2], arguments[3], arguments[4], arguments[5], arguments[6]);
    return usage(QStringLiteral("Invalid background-control arguments."));
}
