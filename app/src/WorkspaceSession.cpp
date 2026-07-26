#include "WorkspaceSession.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

extern "C" {
#include "omachess_core.h"
}

Q_LOGGING_CATEGORY(lcSession, "omachess.session")

namespace {

// Builds a command with string members only, which is the whole command
// vocabulary of the C ABI.
QByteArray command(const QString &type, const QVariantMap &members = {})
{
    QJsonObject object{{QStringLiteral("type"), type}};
    for (auto member = members.cbegin(); member != members.cend(); ++member)
        object.insert(member.key(), member.value().toString());
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

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
    submit(command(QStringLiteral("describe_board")));
}

void WorkspaceSession::flipBoard()
{
    submit(command(QStringLiteral("flip_board")));
}

void WorkspaceSession::playMove(const QString &from, const QString &to, const QString &promotion)
{
    QVariantMap members{{QStringLiteral("from"), from}, {QStringLiteral("to"), to}};
    if (!promotion.isEmpty())
        members.insert(QStringLiteral("promotion"), promotion);
    submit(command(QStringLiteral("play_move"), members));
}

void WorkspaceSession::navigate(const QString &destination)
{
    submit(command(QStringLiteral("navigate"), {{QStringLiteral("to"), destination}}));
}

QVariantList WorkspaceSession::moveList() const
{
    QVariantList moves;
    const QJsonArray played = m_state.value(QStringLiteral("moveList")).toArray();
    for (const QJsonValue &value : played) {
        const QJsonObject move = value.toObject();
        moves.append(QVariantMap{
            {QStringLiteral("number"), move.value(QStringLiteral("number")).toInt()},
            {QStringLiteral("side"), move.value(QStringLiteral("side")).toString()},
            {QStringLiteral("san"), move.value(QStringLiteral("san")).toString()},
        });
    }
    return moves;
}

bool WorkspaceSession::gameOver() const
{
    return m_state.value(QStringLiteral("result"))
        .toObject()
        .value(QStringLiteral("over"))
        .toBool();
}

QString WorkspaceSession::squareNameAt(int index) const
{
    const QJsonArray squares = m_state.value(QStringLiteral("squares")).toArray();
    if (index < 0 || index >= squares.size())
        return {};
    return squares.at(index).toObject().value(QStringLiteral("name")).toString();
}

QString WorkspaceSession::pieceOn(const QString &square) const
{
    const QJsonArray squares = m_state.value(QStringLiteral("squares")).toArray();
    for (const QJsonValue &value : squares) {
        const QJsonObject placed = value.toObject();
        if (placed.value(QStringLiteral("name")).toString() == square)
            return placed.value(QStringLiteral("piece")).toString();
    }
    return {};
}

bool WorkspaceSession::canPickUp(const QString &square) const
{
    const QJsonArray moves = m_state.value(QStringLiteral("moves")).toArray();
    for (const QJsonValue &value : moves) {
        if (value.toObject().value(QStringLiteral("from")).toString() == square)
            return true;
    }
    return false;
}

QStringList WorkspaceSession::destinationsFrom(const QString &from) const
{
    QStringList destinations;
    const QJsonArray moves = m_state.value(QStringLiteral("moves")).toArray();
    for (const QJsonValue &value : moves) {
        const QJsonObject move = value.toObject();
        if (move.value(QStringLiteral("from")).toString() != from)
            continue;
        const QString to = move.value(QStringLiteral("to")).toString();
        if (!destinations.contains(to))
            destinations.append(to);
    }
    return destinations;
}

QStringList WorkspaceSession::promotionsFor(const QString &from, const QString &to) const
{
    const QJsonArray moves = m_state.value(QStringLiteral("moves")).toArray();
    for (const QJsonValue &value : moves) {
        const QJsonObject move = value.toObject();
        if (move.value(QStringLiteral("from")).toString() != from
            || move.value(QStringLiteral("to")).toString() != to) {
            continue;
        }
        QStringList promotions;
        const QJsonArray offered = move.value(QStringLiteral("promotions")).toArray();
        for (const QJsonValue &role : offered)
            promotions.append(role.toString());
        return promotions;
    }
    return {};
}

QString WorkspaceSession::field(const QString &name) const
{
    return m_state.value(name).toString();
}

QString WorkspaceSession::result(const QString &name) const
{
    return m_state.value(QStringLiteral("result")).toObject().value(name).toString();
}

QString WorkspaceSession::lastMoveSquare(const QString &name) const
{
    return m_state.value(QStringLiteral("lastMove")).toObject().value(name).toString();
}

void WorkspaceSession::submit(const QByteArray &commandJson)
{
    const int32_t status = omachess_session_submit(m_session, commandJson.constData());
    if (status != OMACHESS_OK) {
        // A refused move is an ordinary answer rather than a fault: the player
        // tried something the game does not allow, and the board simply does
        // not change.
        if (status == OMACHESS_ERR_REJECTED_MOVE)
            qCDebug(lcSession) << "the game refused" << commandJson;
        else
            qCWarning(lcSession) << "core rejected command" << commandJson << "with status"
                                 << status;
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

    m_state = event;
    m_board.applySquares(event.value(QStringLiteral("squares")).toArray());
    emit boardChanged();
}
