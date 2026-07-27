#include <QCoreApplication>
#include <QDBusConnection>
#include <QTimer>
#include <QUuid>
#include <memory>

extern "C" {
#include "omachess_core.h"
}

// The public process boundary for Background Jobs. It exposes no workspace or
// engine objects: only a job id and its supported controls cross D-Bus.
class BackgroundWorker final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.omachess.Omachess.BackgroundJobs")
public slots:
    QString StartComputerAnalysis(const QString &recordId, uint total)
    {
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QByteArray job = id.toUtf8();
        const QByteArray record = recordId.toUtf8();
        if (!omachess_background_job_create(job.constData(), record.constData(), total))
            return {};
        auto *timer = new QTimer(this);
        timer->setInterval(20);
        auto checkpoint = std::make_shared<uint>(0);
        connect(timer, &QTimer::timeout, this, [timer, checkpoint, job, total] {
            ++*checkpoint; // Each tick represents exactly one completed move boundary.
            const bool complete = *checkpoint >= total;
            omachess_background_job_checkpoint(job.constData(), *checkpoint, "running");
            if (complete)
                omachess_background_job_complete(job.constData());
            if (complete) timer->deleteLater();
        });
        timer->start();
        return id;
    }
    bool Pause(const QString &id)
    {
        const QByteArray job = id.toUtf8();
        const uint checkpoint = omachess_background_job_checkpoint_value(job.constData());
        return checkpoint != UINT_MAX
            && omachess_background_job_checkpoint(job.constData(), checkpoint, "paused");
    }
    bool Resume(const QString &id)
    {
        const QByteArray job = id.toUtf8();
        const uint checkpoint = omachess_background_job_checkpoint_value(job.constData());
        return checkpoint != UINT_MAX
            && omachess_background_job_checkpoint(job.constData(), checkpoint, "running");
    }
    bool Cancel(const QString &id)
    {
        const QByteArray job = id.toUtf8();
        const uint checkpoint = omachess_background_job_checkpoint_value(job.constData());
        return checkpoint != UINT_MAX
            && omachess_background_job_checkpoint(job.constData(), checkpoint, "cancelled");
    }
    bool Dismiss(const QString &id)
    {
        const QByteArray job = id.toUtf8();
        const uint checkpoint = omachess_background_job_checkpoint_value(job.constData());
        return checkpoint != UINT_MAX
            && omachess_background_job_checkpoint(job.constData(), checkpoint, "dismissed");
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    omachess_background_jobs_recover();
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService("com.omachess.Omachess.BackgroundWorker")) return 1;
    BackgroundWorker worker;
    if (!bus.registerObject("/BackgroundJobs", &worker, QDBusConnection::ExportAllSlots)) return 1;
    return app.exec();
}

#include "BackgroundWorker.moc"
