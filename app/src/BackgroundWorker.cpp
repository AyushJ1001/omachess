#include <QCoreApplication>
#include <QDBusConnection>
#include <QHash>
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
        startRunner(id, 0, total);
        return id;
    }
    bool Pause(const QString &id)
    {
        const QByteArray job = id.toUtf8();
        const uint checkpoint = omachess_background_job_checkpoint_value(job.constData());
        if (checkpoint == UINT_MAX || !omachess_background_job_checkpoint(job.constData(), checkpoint, "paused"))
            return false;
        stopRunner(id);
        return true;
    }
    bool Resume(const QString &id)
    {
        const QByteArray job = id.toUtf8();
        const uint checkpoint = omachess_background_job_checkpoint_value(job.constData());
        const uint total = omachess_background_job_total_value(job.constData());
        if (checkpoint == UINT_MAX || total == UINT_MAX
            || !omachess_background_job_checkpoint(job.constData(), checkpoint, "running"))
            return false;
        startRunner(id, checkpoint, total);
        return true;
    }
    bool Cancel(const QString &id)
    {
        const QByteArray job = id.toUtf8();
        const uint checkpoint = omachess_background_job_checkpoint_value(job.constData());
        if (checkpoint == UINT_MAX || !omachess_background_job_checkpoint(job.constData(), checkpoint, "cancelled"))
            return false;
        stopRunner(id);
        return true;
    }
    bool Dismiss(const QString &id)
    {
        const QByteArray job = id.toUtf8();
        const uint checkpoint = omachess_background_job_checkpoint_value(job.constData());
        if (checkpoint == UINT_MAX || !omachess_background_job_checkpoint(job.constData(), checkpoint, "dismissed"))
            return false;
        stopRunner(id);
        return true;
    }
    QString Open(const QString &id)
    {
        const QByteArray job = id.toUtf8();
        // Opening is a workspace action. The worker validates the durable job
        // and returns its stable id so a newly launched workspace can attach.
        return omachess_background_job_checkpoint_value(job.constData()) == UINT_MAX ? QString() : id;
    }
private:
    void startRunner(const QString &id, uint checkpoint, uint total)
    {
        stopRunner(id);
        auto *timer = new QTimer(this);
        timer->setInterval(20);
        m_runners.insert(id, timer);
        const QByteArray job = id.toUtf8();
        auto progress = std::make_shared<uint>(checkpoint);
        connect(timer, &QTimer::timeout, this, [this, timer, id, job, progress, total] {
            ++*progress;
            const bool finalBoundary = *progress >= total;
            if (!omachess_background_job_checkpoint(job.constData(), *progress, "running")) {
                stopRunner(id);
                return;
            }
            if (finalBoundary) {
                omachess_background_job_complete(job.constData());
                stopRunner(id);
            }
        });
        timer->start();
    }
    void stopRunner(const QString &id)
    {
        if (auto *timer = m_runners.take(id)) {
            timer->stop();
            timer->deleteLater();
        }
    }
    QHash<QString, QTimer *> m_runners;
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
