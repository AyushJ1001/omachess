#include "WorkspaceSession.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QFile>
#include <QFileDialog>
#include <QStandardPaths>

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
    if (!m_session) {
        const char *message = omachess_last_error();
        m_storeError = message ? QString::fromUtf8(message)
                               : QStringLiteral("The Live Store could not be opened.");
        qCCritical(lcSession) << m_storeError;
    }
}

WorkspaceSession::~WorkspaceSession()
{
    if (m_session)
        omachess_session_free(m_session);
}

void WorkspaceSession::newVariantDefinition()
{
    submit(command(QStringLiteral("new_variant_definition")));
}

void WorkspaceSession::selectBoardPreset(const QString &id)
{
    submit(command(QStringLiteral("select_board_preset"), {{QStringLiteral("id"), id}}));
}

void WorkspaceSession::setWorkshopStep(int step)
{
    submit(command(QStringLiteral("set_workshop_step"),
                   {{QStringLiteral("step"), QString::number(step)}}));
}

void WorkspaceSession::placeWorkshopPiece(const QString &square, const QString &piece)
{
    submit(command(QStringLiteral("place_workshop_piece"),
                   {{QStringLiteral("square"), square}, {QStringLiteral("piece"), piece}}));
}

void WorkspaceSession::toggleVariantRule(const QString &rule)
{
    submit(command(QStringLiteral("toggle_variant_rule"),
                   {{QStringLiteral("rule"), rule}}));
}

void WorkspaceSession::toggleBuiltinPiece(const QString &code)
{
    submit(command(QStringLiteral("toggle_builtin_piece"), {{QStringLiteral("code"), code}}));
}

void WorkspaceSession::setCustomPiece(const QString &name, const QString &letter,
                                      const QString &betza)
{
    submit(command(QStringLiteral("set_custom_piece"),
                   {{QStringLiteral("name"), name}, {QStringLiteral("letter"), letter},
                    {QStringLiteral("betza"), betza}}));
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

void WorkspaceSession::restoreRecord()
{
    submit(command(QStringLiteral("restore_record")));
}

void WorkspaceSession::suspendGame()
{
    submit(command(QStringLiteral("suspend_game")));
}

void WorkspaceSession::resumeGame()
{
    submit(command(QStringLiteral("resume_game")));
}

void WorkspaceSession::dismissRestore()
{
    submit(command(QStringLiteral("dismiss_restore")));
}

void WorkspaceSession::newGame()
{
    submit(command(QStringLiteral("new_game")));
}

void WorkspaceSession::openRecord(const QString &id)
{
    submit(command(QStringLiteral("open_record"), {{QStringLiteral("id"), id}}));
}

void WorkspaceSession::closeTab(const QString &id)
{
    submit(command(QStringLiteral("close_tab"), {{QStringLiteral("id"), id}}));
}

void WorkspaceSession::setSaveMode(const QString &mode)
{
    submit(command(QStringLiteral("set_save_mode"), {{QStringLiteral("mode"), mode}}));
}

void WorkspaceSession::saveRecord()
{
    submit(command(QStringLiteral("save_record")));
}

void WorkspaceSession::discardChanges()
{
    submit(command(QStringLiteral("discard_changes")));
}

void WorkspaceSession::configureClock(int milliseconds)
{
    submit(command(QStringLiteral("configure_clock"),
                   {{QStringLiteral("milliseconds"), QString::number(milliseconds)}}));
}

void WorkspaceSession::tickClock()
{
    submit(command(QStringLiteral("tick_clock")));
}

void WorkspaceSession::updateMetadata(const QString &white, const QString &black,
                                      const QString &event, const QString &date,
                                      const QString &title, const QString &tags)
{
    submit(command(QStringLiteral("update_metadata"),
                   {{QStringLiteral("white"), white},
                    {QStringLiteral("black"), black},
                    {QStringLiteral("event"), event},
                    {QStringLiteral("date"), date},
                    {QStringLiteral("title"), title},
                    {QStringLiteral("tags"), tags}}));
}

void WorkspaceSession::beginPositionSetup()
{
    submit(command(QStringLiteral("begin_position_setup")));
}

void WorkspaceSession::setSetupFen(const QString &fen)
{
    submit(command(QStringLiteral("set_setup_fen"), {{QStringLiteral("fen"), fen}}));
}

void WorkspaceSession::placeSetupPiece(const QString &square, const QString &piece)
{
    submit(command(QStringLiteral("place_setup_piece"),
                   {{QStringLiteral("square"), square}, {QStringLiteral("piece"), piece}}));
}

void WorkspaceSession::relocateSetupPiece(const QString &from, const QString &to)
{
    submit(command(QStringLiteral("relocate_setup_piece"),
                   {{QStringLiteral("from"), from}, {QStringLiteral("to"), to}}));
}

void WorkspaceSession::startSetupGame()
{
    submit(command(QStringLiteral("start_setup_game")));
}

void WorkspaceSession::importPgn()
{
    QString path = qEnvironmentVariable("OMACHESS_TEST_IMPORT_PGN");
    if (path.isEmpty()) {
        path = QFileDialog::getOpenFileName(nullptr, tr("Import PGN"), QString(),
                                            tr("Portable Game Notation (*.pgn)"));
    }
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcSession) << "cannot read PGN" << path << file.errorString();
        return;
    }
    submit(command(QStringLiteral("import_pgn"),
                   {{QStringLiteral("pgn"), QString::fromUtf8(file.readAll())}}));
}

void WorkspaceSession::exportPgn(const QStringList &recordIds)
{
    if (recordIds.isEmpty())
        return;
    m_exportPath = qEnvironmentVariable("OMACHESS_TEST_EXPORT_PGN");
    if (m_exportPath.isEmpty()) {
        m_exportPath = QFileDialog::getSaveFileName(nullptr, tr("Export PGN"),
                                                    QStringLiteral("omachess.pgn"),
                                                    tr("Portable Game Notation (*.pgn)"));
    }
    if (m_exportPath.isEmpty())
        return;
    submit(command(QStringLiteral("export_pgn"),
                   {{QStringLiteral("ids"), recordIds.join(',')}}));
}

void WorkspaceSession::deriveAnalysisRecord()
{
    submit(command(QStringLiteral("derive_analysis_record")));
}

void WorkspaceSession::addAnalysisAnnotation(int ply, const QString &text)
{
    if (!text.trimmed().isEmpty())
        submit(command(QStringLiteral("add_analysis_annotation"),
                       {{QStringLiteral("ply"), QString::number(ply)},
                        {QStringLiteral("text"), text}}));
}

void WorkspaceSession::addAnalysisSideline(int afterPly, const QString &variation)
{
    if (!variation.trimmed().isEmpty())
        submit(command(QStringLiteral("add_analysis_sideline"),
                       {{QStringLiteral("after_ply"), QString::number(afterPly)},
                        {QStringLiteral("variation"), variation}}));
}

void WorkspaceSession::pinEngineLine(const QString &positionFen, const QString &evaluation,
                                     const QString &variation, const QString &engine,
                                     const QString &searchContext)
{
    submit(command(QStringLiteral("pin_engine_line"),
                   {{QStringLiteral("position_fen"), positionFen},
                    {QStringLiteral("evaluation"), evaluation},
                    {QStringLiteral("variation"), variation},
                    {QStringLiteral("engine"), engine},
                    {QStringLiteral("search_context"), searchContext}}));
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

QString WorkspaceSession::uciMoves() const
{
    QStringList moves;
    for (const QJsonValue &value : m_state.value(QStringLiteral("moveList")).toArray())
        moves.append(value.toObject().value(QStringLiteral("uci")).toString());
    return moves.join(QLatin1Char(' '));
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

QJsonArray WorkspaceSession::movesOffered() const
{
    return m_state.value(QStringLiteral("moves")).toArray();
}

bool WorkspaceSession::canPickUp(const QString &square) const
{
    for (const QJsonValue &value : movesOffered()) {
        if (value.toObject().value(QStringLiteral("from")).toString() == square)
            return true;
    }
    return false;
}

QStringList WorkspaceSession::destinationsFrom(const QString &from) const
{
    QStringList destinations;
    for (const QJsonValue &value : movesOffered()) {
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
    for (const QJsonValue &value : movesOffered()) {
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

QJsonValue WorkspaceSession::clockField(const QString &name) const
{
    return m_state.value(QStringLiteral("clock")).toObject().value(name);
}

QString WorkspaceSession::metadataField(const QString &name) const
{
    return m_state.value(QStringLiteral("metadata")).toObject().value(name).toString();
}

void WorkspaceSession::submit(const QByteArray &commandJson)
{
    if (!m_session) {
        qCWarning(lcSession) << "no Live Store session; ignoring" << commandJson;
        return;
    }

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
    if (type == QStringLiteral("board_changed")) {
        m_state = event;
        m_board.applySquares(event.value(QStringLiteral("squares")).toArray());
        emit boardChanged();
        return;
    }
    if (type == QStringLiteral("library_changed")) {
        m_libraryRecords.clear();
        const QJsonArray records = event.value(QStringLiteral("records")).toArray();
        for (const QJsonValue &value : records) {
            const QJsonObject record = value.toObject();
            QVariantMap entry{
                {QStringLiteral("id"), record.value(QStringLiteral("id")).toString()},
                {QStringLiteral("kind"), record.value(QStringLiteral("kind")).toString()},
                {QStringLiteral("title"), record.value(QStringLiteral("title")).toString()},
                {QStringLiteral("plyCount"), record.value(QStringLiteral("plyCount")).toInt()},
            };
            const QJsonValue score = record.value(QStringLiteral("resultScore"));
            entry.insert(QStringLiteral("resultScore"),
                         score.isNull() ? QString() : score.toString());
            m_libraryRecords.append(entry);
        }
        emit libraryChanged();
        return;
    }
    if (type == QStringLiteral("analysis_record_changed")) {
        m_sourceSnapshot = event.value(QStringLiteral("sourceSnapshot")).toObject().toVariantMap();
        m_recordSources.clear();
        for (const QJsonValue &value : event.value(QStringLiteral("sources")).toArray())
            m_recordSources.append(value.toString());
        m_recordDerivations.clear();
        for (const QJsonValue &value : event.value(QStringLiteral("derivations")).toArray())
            m_recordDerivations.append(value.toString());
        m_analysisMainLinePly = event.value(QStringLiteral("mainLinePly")).toInt();
        m_analysisSidelineCount = event.value(QStringLiteral("sidelineCount")).toInt();
        m_analysisAnnotationCount = event.value(QStringLiteral("annotationCount")).toInt();
        m_analysisAnnotations.clear();
        for (const QJsonValue &value : event.value(QStringLiteral("annotations")).toArray())
            m_analysisAnnotations.append(value.toObject().toVariantMap());
        m_analysisSidelines.clear();
        for (const QJsonValue &value : event.value(QStringLiteral("sidelines")).toArray())
            m_analysisSidelines.append(value.toObject().toVariantMap());
        m_pinnedEngineLines.clear();
        for (const QJsonValue &value : event.value(QStringLiteral("pinnedLines")).toArray())
            m_pinnedEngineLines.append(value.toObject().toVariantMap());
        emit analysisRecordChanged();
        return;
    }
    if (type == QStringLiteral("record_graph_changed")) {
        m_recordSources.clear();
        for (const QJsonValue &value : event.value(QStringLiteral("sources")).toArray())
            m_recordSources.append(value.toString());
        m_recordDerivations.clear();
        for (const QJsonValue &value : event.value(QStringLiteral("derivations")).toArray())
            m_recordDerivations.append(value.toString());
        emit analysisRecordChanged();
        return;
    }
    if (type == QStringLiteral("variant_library_changed")) {
        const QString id = event.value(QStringLiteral("id")).toString();
        for (const QVariant &record : std::as_const(m_libraryRecords))
            if (record.toMap().value(QStringLiteral("id")).toString() == id)
                return;
        m_libraryRecords.prepend(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("kind"), event.value(QStringLiteral("kind")).toString()},
            {QStringLiteral("title"), event.value(QStringLiteral("title")).toString()},
            {QStringLiteral("resultScore"), QString()},
            {QStringLiteral("plyCount"), 0},
        });
        emit libraryChanged();
        return;
    }
    if (type == QStringLiteral("workshop_changed")) {
        m_workshopActive = event.value(QStringLiteral("active")).toBool();
        m_workshopStep = event.value(QStringLiteral("step")).toInt();
        m_boardFiles = event.value(QStringLiteral("files")).toInt();
        m_boardRanks = event.value(QStringLiteral("ranks")).toInt();
        m_selectedPieces.clear();
        for (QChar code : event.value(QStringLiteral("selectedPieces")).toString())
            m_selectedPieces.append(code);
        m_customPieceName = event.value(QStringLiteral("customName")).toString();
        m_customPieceLetter = event.value(QStringLiteral("customLetter")).toString();
        m_customPieceBetza = event.value(QStringLiteral("customBetza")).toString();
        m_betzaError = event.value(QStringLiteral("error")).toString();
        m_variantFen = event.value(QStringLiteral("fen")).toString();
        m_workshopPositionRuleValid = event.value(QStringLiteral("ruleValid")).toBool();
        m_variantRules = event.value(QStringLiteral("rules")).toObject().toVariantMap();
        m_ruleConflict = event.value(QStringLiteral("ruleConflict")).toString();
        m_boardPresets.clear();
        for (const QJsonValue &value : event.value(QStringLiteral("presets")).toArray())
            m_boardPresets.append(value.toObject().toVariantMap());
        m_pieceCatalogue.clear();
        for (const QJsonValue &value : event.value(QStringLiteral("pieces")).toArray())
            m_pieceCatalogue.append(value.toObject().toVariantMap());
        emit workshopChanged();
        return;
    }
    if (type == QStringLiteral("tabs_changed")) {
        m_openTabs.clear();
        const QJsonArray tabs = event.value(QStringLiteral("openTabs")).toArray();
        for (const QJsonValue &value : tabs) {
            const QJsonObject tab = value.toObject();
            m_openTabs.append(QVariantMap{
                {QStringLiteral("id"), tab.value(QStringLiteral("id")).toString()},
                {QStringLiteral("title"), tab.value(QStringLiteral("title")).toString()},
            });
        }
        const QJsonValue active = event.value(QStringLiteral("activeId"));
        m_activeRecordId = active.isNull() ? QString() : active.toString();
        emit tabsChanged();
        return;
    }
    if (type == QStringLiteral("restore_available")) {
        m_restoreAvailable = true;
        m_restoreLabel = event.value(QStringLiteral("label")).toString();
        emit restoreChanged();
        return;
    }
    if (type == QStringLiteral("restore_cleared")) {
        m_restoreAvailable = false;
        m_restoreLabel.clear();
        emit restoreChanged();
        return;
    }
    if (type == QStringLiteral("pgn_import_results")) {
        m_pgnImportResults.clear();
        for (const QJsonValue &value : event.value(QStringLiteral("entries")).toArray())
            m_pgnImportResults.append(value.toObject().toVariantMap());
        emit pgnImportResultsChanged();
        return;
    }
    if (type == QStringLiteral("pgn_export_ready")) {
        QFile file(m_exportPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCWarning(lcSession) << "cannot write PGN" << m_exportPath << file.errorString();
            return;
        }
        file.write(event.value(QStringLiteral("pgn")).toString().toUtf8());
        file.close();
        m_exportPath.clear();
        return;
    }

    // Later tickets add event types; ignoring unknown ones keeps an older
    // workspace usable against a newer core.
    qCDebug(lcSession) << "ignoring unhandled core event" << type;
}
