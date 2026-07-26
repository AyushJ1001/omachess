#include "WorkspaceSession.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

extern "C" {
#include "omachess_core.h"
}

Q_LOGGING_CATEGORY(lcSession, "omachess.session")

WorkspaceSession::WorkspaceSession(QObject *parent)
    : QObject(parent)
    , m_session(omachess_session_new())
{
}

WorkspaceSession::~WorkspaceSession()
{
    omachess_session_free(m_session);
}

void WorkspaceSession::describeBoard()
{
    submit(QByteArrayLiteral(R"({"type":"describe_board"})"));
}

void WorkspaceSession::flipBoard()
{
    submit(QByteArrayLiteral(R"({"type":"flip_board"})"));
}

void WorkspaceSession::submit(const QByteArray &commandJson)
{
    const int32_t status = omachess_session_submit(m_session, commandJson.constData());
    if (status != OMACHESS_OK) {
        qCWarning(lcSession) << "core rejected command" << commandJson << "with status" << status;
        return;
    }

    while (char *event = omachess_session_poll_event(m_session)) {
        applyEvent(QByteArray(event));
        omachess_string_free(event);
    }
}

void WorkspaceSession::applyEvent(const QByteArray &eventJson)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(eventJson, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qCWarning(lcSession) << "unreadable core event:" << parseError.errorString();
        return;
    }

    const QJsonObject event = document.object();
    const QString type = event.value(QStringLiteral("type")).toString();
    if (type != QStringLiteral("board_changed")) {
        // Later tickets add event types; ignoring unknown ones keeps an older
        // workspace usable against a newer core.
        qCDebug(lcSession) << "ignoring unhandled core event" << type;
        return;
    }

    m_orientation = event.value(QStringLiteral("orientation")).toString();
    m_variant = event.value(QStringLiteral("variant")).toString();
    m_board.applySquares(event.value(QStringLiteral("squares")).toArray());
    emit boardChanged();
}
