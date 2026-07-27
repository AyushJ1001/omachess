#include "WorkspaceSession.h"

#include <QCoreApplication>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QDBusInterface>
#include <QDBusReply>

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

QDBusInterface backgroundWorker()
{
    return QDBusInterface(QStringLiteral("com.omachess.Omachess.BackgroundWorker"),
                          QStringLiteral("/BackgroundJobs"),
                          QStringLiteral("com.omachess.Omachess.BackgroundJobs"),
                          QDBusConnection::sessionBus());
}

QString currentWorkerDataLocation()
{
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
}

QString currentWorkerConfigLocation()
{
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
}

bool startBundledBackgroundWorker()
{
    const QString workerPath =
        QCoreApplication::applicationDirPath() + QStringLiteral("/omachess-background-worker");
    if (!QFileInfo::exists(workerPath))
        return false;
    if (!QProcess::startDetached(workerPath, {}))
        return false;
    auto *bus = QDBusConnection::sessionBus().interface();
    if (!bus)
        return false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        const QDBusReply<bool> registered =
            bus->isServiceRegistered(QStringLiteral("com.omachess.Omachess.BackgroundWorker"));
        if (registered.isValid() && registered.value())
            return true;
        QThread::msleep(50);
    }
    return false;
}

bool waitUntilBackgroundWorkerUnregistered()
{
    auto *bus = QDBusConnection::sessionBus().interface();
    if (!bus)
        return false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        const QDBusReply<bool> registered =
            bus->isServiceRegistered(QStringLiteral("com.omachess.Omachess.BackgroundWorker"));
        if (registered.isValid() && !registered.value())
            return true;
        QThread::msleep(50);
    }
    return false;
}

bool workerMatchesCurrentContext(QDBusInterface &worker)
{
    QDBusReply<bool> reply =
        worker.call(QStringLiteral("MatchesContext"),
                    currentWorkerDataLocation(),
                    currentWorkerConfigLocation());
    return reply.isValid() && reply.value();
}

bool ensureBackgroundWorkerAvailable()
{
    QDBusInterface worker = backgroundWorker();
    if (worker.isValid() && workerMatchesCurrentContext(worker))
        return true;

    if (worker.isValid()) {
        QDBusReply<bool> quit =
            worker.call(QStringLiteral("QuitIfIdle"),
                        currentWorkerDataLocation(),
                        currentWorkerConfigLocation());
        if (!quit.isValid() || !quit.value() || !waitUntilBackgroundWorkerUnregistered())
            return false;
    }

    if (!startBundledBackgroundWorker())
        return false;
    QDBusInterface started = backgroundWorker();
    return started.isValid() && workerMatchesCurrentContext(started);
}

bool callBackgroundWorkerBool(const QString &method, const QVariantList &arguments)
{
    QDBusReply<bool> reply;
    if (ensureBackgroundWorkerAvailable()) {
        QDBusInterface worker = backgroundWorker();
        reply = worker.callWithArgumentList(QDBus::Block, method, arguments);
    }
    return reply.isValid() && reply.value();
}

QString callBackgroundWorkerString(const QString &method, const QVariantList &arguments = {})
{
    QDBusReply<QString> reply;
    if (ensureBackgroundWorkerAvailable()) {
        QDBusInterface worker = backgroundWorker();
        reply = worker.callWithArgumentList(QDBus::Block, method, arguments);
    }
    return reply.isValid() ? reply.value() : QString();
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

void WorkspaceSession::validateVariantDefinition()
{
    submit(command(QStringLiteral("validate_variant_definition")));
}

void WorkspaceSession::editVariantDefinition()
{
    submit(command(QStringLiteral("edit_variant_definition")));
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

QString WorkspaceSession::startBackgroundComputerAnalysis(const QString &searchSettings,
                                                          int searchTimeMs,
                                                          int lineLimit)
{
    if (m_activeRecordId.isEmpty() || !gameOver())
        return {};
    return callBackgroundWorkerString(QStringLiteral("StartComputerAnalysis"),
                                      {m_activeRecordId,
                                       QVariant::fromValue(static_cast<uint>(moveList().size() + 1)),
                                       searchSettings,
                                       searchTimeMs,
                                       lineLimit});
}

void WorkspaceSession::pauseBackgroundJob(const QString &id)
{
    if (!id.isEmpty())
        callBackgroundWorkerBool(QStringLiteral("Pause"), {id});
}

void WorkspaceSession::resumeBackgroundJob(const QString &id,
                                           const QString &searchSettings,
                                           int searchTimeMs,
                                           int lineLimit)
{
    if (!id.isEmpty())
        callBackgroundWorkerBool(QStringLiteral("Resume"),
                                 {id, searchSettings, searchTimeMs, lineLimit});
}

void WorkspaceSession::cancelBackgroundJob(const QString &id)
{
    if (!id.isEmpty())
        callBackgroundWorkerBool(QStringLiteral("Cancel"), {id});
}

void WorkspaceSession::dismissBackgroundJob(const QString &id)
{
    if (!id.isEmpty())
        callBackgroundWorkerBool(QStringLiteral("Dismiss"), {id});
}

QString WorkspaceSession::backgroundJob(const QString &id)
{
    if (id.isEmpty())
        return {};
    return callBackgroundWorkerString(QStringLiteral("Job"), {id});
}

QString WorkspaceSession::backgroundJobs()
{
    return callBackgroundWorkerString(QStringLiteral("Jobs"));
}

bool WorkspaceSession::importBackgroundComputerAnalysis(const QString &id)
{
    const QString encodedJob = backgroundJob(id);
    const QJsonDocument document = QJsonDocument::fromJson(encodedJob.toUtf8());
    if (!document.isObject())
        return false;
    const QJsonObject job = document.object();
    if (job.value(QStringLiteral("kind")).toString() != QStringLiteral("computer_analysis")
            || job.value(QStringLiteral("state")).toString() != QStringLiteral("complete"))
        return false;
    const QString recordId = job.value(QStringLiteral("recordId")).toString();
    const QString payload = job.value(QStringLiteral("payload")).toString();
    if (recordId.isEmpty() || payload.isEmpty())
        return false;
    if (m_activeRecordId != recordId)
        openRecord(recordId);
    if (m_activeRecordId != recordId)
        return false;
    if (!submitAndDrain(command(QStringLiteral("complete_computer_analysis"),
                                {{QStringLiteral("evaluations"), payload}})))
        return false;
    dismissBackgroundJob(id);
    return true;
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

void WorkspaceSession::archiveRecord(const QString &id)
{
    submit(command(QStringLiteral("archive_record"), {{QStringLiteral("id"), id}}));
}

void WorkspaceSession::unarchiveRecord(const QString &id)
{
    submit(command(QStringLiteral("unarchive_record"), {{QStringLiteral("id"), id}}));
}

void WorkspaceSession::setLibraryView(const QString &view)
{
    submit(command(QStringLiteral("set_library_view"), {{QStringLiteral("view"), view}}));
}

void WorkspaceSession::purgeRecord(const QString &id)
{
    submit(command(QStringLiteral("purge_record"),
                   {{QStringLiteral("id"), id},
                    {QStringLiteral("confirmation"), QStringLiteral("PERMANENTLY_PURGE")}}));
}

void WorkspaceSession::purgeStudy(const QString &studyId)
{
    submit(command(QStringLiteral("purge_study"),
                   {{QStringLiteral("study_id"), studyId},
                    {QStringLiteral("confirmation"), QStringLiteral("PERMANENTLY_PURGE")}}));
}

void WorkspaceSession::purgeVariantDefinition()
{
    submit(command(QStringLiteral("purge_variant_definition"),
                   {{QStringLiteral("confirmation"), QStringLiteral("PERMANENTLY_PURGE")}}));
}

void WorkspaceSession::createStudy(const QString &name)
{
    if (!name.trimmed().isEmpty())
        submit(command(QStringLiteral("create_study"), {{QStringLiteral("name"), name}}));
}

void WorkspaceSession::addStudyRecord(const QString &studyId, const QString &recordId)
{
    submit(command(QStringLiteral("add_study_record"),
                   {{QStringLiteral("study_id"), studyId},
                    {QStringLiteral("record_id"), recordId}}));
}

void WorkspaceSession::removeStudyRecord(const QString &studyId, const QString &recordId)
{
    submit(command(QStringLiteral("remove_study_record"),
                   {{QStringLiteral("study_id"), studyId},
                    {QStringLiteral("record_id"), recordId}}));
}

void WorkspaceSession::reorderStudyRecord(const QString &studyId, const QString &recordId,
                                          int position)
{
    submit(command(QStringLiteral("reorder_study_record"),
                   {{QStringLiteral("study_id"), studyId},
                    {QStringLiteral("record_id"), recordId},
                    {QStringLiteral("position"), QString::number(position)}}));
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

void WorkspaceSession::exportLibraryPackage()
{
    m_packageExportPath = qEnvironmentVariable("OMACHESS_TEST_EXPORT_PACKAGE");
    if (m_packageExportPath.isEmpty()) {
        m_packageExportPath = QFileDialog::getSaveFileName(
            nullptr, tr("Export Library Portability Package"),
            QStringLiteral("omachess-library.omalib"),
            tr("Library Portability Package (*.omalib)"));
    }
    if (m_packageExportPath.isEmpty())
        return;
    submit(command(QStringLiteral("export_library_package")));
}

void WorkspaceSession::restoreLibraryPackage()
{
    QString path = qEnvironmentVariable("OMACHESS_TEST_RESTORE_PACKAGE");
    if (path.isEmpty()) {
        path = QFileDialog::getOpenFileName(
            nullptr, tr("Restore Library Portability Package"), QString(),
            tr("Library Portability Package (*.omalib)"));
    }
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_libraryPackageMessage =
            tr("Could not read %1: %2. Nothing was changed.").arg(path, file.errorString());
        emit libraryPackageChanged();
        return;
    }
    m_pendingPackage = QString::fromUtf8(file.readAll());
    file.close();
    submit(command(QStringLiteral("restore_library_package"),
                   {{QStringLiteral("package"), m_pendingPackage}}));
}

void WorkspaceSession::confirmLibraryReplacement()
{
    if (m_pendingPackage.isEmpty())
        return;
    submit(command(QStringLiteral("restore_library_package"),
                   {{QStringLiteral("package"), m_pendingPackage},
                    {QStringLiteral("confirmation"), QStringLiteral("REPLACE_LIBRARY")}}));
}

void WorkspaceSession::cancelLibraryReplacement()
{
    m_pendingPackage.clear();
    m_libraryReplacementMessage.clear();
    m_libraryPackageMessage = tr("The library was left as it was.");
    emit libraryPackageChanged();
}

void WorkspaceSession::deriveAnalysisRecord()
{
    submit(command(QStringLiteral("derive_analysis_record")));
}

void WorkspaceSession::completeComputerAnalysis(const QString &evaluations)
{
    submit(command(QStringLiteral("complete_computer_analysis"),
                   {{QStringLiteral("evaluations"), evaluations}}));
}

void WorkspaceSession::designateDefaultAnalysis()
{
    submit(command(QStringLiteral("designate_default_analysis")));
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
    submitAndDrain(commandJson);
}

bool WorkspaceSession::submitAndDrain(const QByteArray &commandJson)
{
    if (!m_session) {
        qCWarning(lcSession) << "no Live Store session; ignoring" << commandJson;
        return false;
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
        return false;
    }

    while (char *event = omachess_session_poll_event(m_session)) {
        applyEvent(QByteArray(event));
        omachess_string_free(event);
    }
    return true;
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
                {QStringLiteral("archived"), record.value(QStringLiteral("archived")).toBool()},
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
        m_computerEvaluations.clear();
        for (const QJsonValue &value : event.value(QStringLiteral("computerEvaluations")).toArray())
            m_computerEvaluations.append(value.toObject().toVariantMap());
        m_computerAnalysisComplete =
            event.value(QStringLiteral("computerAnalysisComplete")).toBool();
        m_defaultAnalysis = event.value(QStringLiteral("defaultAnalysis")).toBool();
        emit analysisRecordChanged();
        return;
    }
    if (type == QStringLiteral("studies_changed")) {
        m_studies.clear();
        for (const QJsonValue &value : event.value(QStringLiteral("studies")).toArray()) {
            const QJsonObject study = value.toObject();
            m_studies.append(QVariantMap{
                {QStringLiteral("id"), study.value(QStringLiteral("id")).toString()},
                {QStringLiteral("name"), study.value(QStringLiteral("name")).toString()},
                {QStringLiteral("recordIds"),
                 study.value(QStringLiteral("recordIds")).toArray().toVariantList()},
            });
        }
        emit studiesChanged();
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
        if (event.value(QStringLiteral("removed")).toBool()) {
            for (int index = 0; index < m_libraryRecords.size(); ++index) {
                if (m_libraryRecords.at(index).toMap().value(QStringLiteral("id")).toString() == id) {
                    m_libraryRecords.removeAt(index);
                    emit libraryChanged();
                    return;
                }
            }
            return;
        }
        for (int index = 0; index < m_libraryRecords.size(); ++index) {
            QVariantMap record = m_libraryRecords.at(index).toMap();
            if (record.value(QStringLiteral("id")).toString() == id) {
                record.insert(QStringLiteral("variantPlayable"),
                              event.value(QStringLiteral("playable")).toBool());
                m_libraryRecords[index] = record;
                emit libraryChanged();
                return;
            }
        }
        m_libraryRecords.prepend(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("kind"), event.value(QStringLiteral("kind")).toString()},
            {QStringLiteral("title"), event.value(QStringLiteral("title")).toString()},
            {QStringLiteral("resultScore"), QString()},
            {QStringLiteral("plyCount"), 0},
            {QStringLiteral("variantPlayable"),
             event.value(QStringLiteral("playable")).toBool()},
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
        m_variantPlayable = event.value(QStringLiteral("playable")).toBool();
        m_variantValidationMessage =
            event.value(QStringLiteral("validationMessage")).toString();
        m_boardPresets.clear();
        for (const QJsonValue &value : event.value(QStringLiteral("presets")).toArray())
            m_boardPresets.append(value.toObject().toVariantMap());
        m_pieceCatalogue.clear();
        for (const QJsonValue &value : event.value(QStringLiteral("pieces")).toArray())
            m_pieceCatalogue.append(value.toObject().toVariantMap());
        emit workshopChanged();
        return;
    }
    if (type == QStringLiteral("variant_analysis_changed")) {
        m_variantAnalysisEvaluation = event.value(QStringLiteral("evaluation")).toString();
        m_variantAnalysisVariation = event.value(QStringLiteral("variation")).toString();
        m_variantAnalysisEvaluator = event.value(QStringLiteral("evaluator")).toString();
        m_variantAnalysisCaveat = event.value(QStringLiteral("caveat")).toString();
        emit variantAnalysisChanged();
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

    if (type == QStringLiteral("library_package_ready")) {
        QFile file(m_packageExportPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            m_libraryPackageMessage = tr("Could not write %1: %2.")
                                          .arg(m_packageExportPath, file.errorString());
        } else {
            file.write(event.value(QStringLiteral("package")).toString().toUtf8());
            file.close();
            m_libraryPackageMessage = tr("Exported %1 · %2")
                                          .arg(m_packageExportPath,
                                               event.value(QStringLiteral("summary")).toString());
        }
        m_packageExportPath.clear();
        emit libraryPackageChanged();
        return;
    }
    if (type == QStringLiteral("library_replacement_required")) {
        m_libraryReplacementMessage = event.value(QStringLiteral("message")).toString();
        m_libraryPackageMessage.clear();
        emit libraryPackageChanged();
        return;
    }
    if (type == QStringLiteral("library_package_restored")) {
        m_pendingPackage.clear();
        m_libraryReplacementMessage.clear();
        m_libraryPackageMessage = event.value(QStringLiteral("message")).toString();
        emit libraryPackageChanged();
        return;
    }
    if (type == QStringLiteral("library_package_rejected")) {
        m_pendingPackage.clear();
        m_libraryReplacementMessage.clear();
        m_libraryPackageMessage = event.value(QStringLiteral("message")).toString();
        emit libraryPackageChanged();
        return;
    }

    // Later tickets add event types; ignoring unknown ones keeps an older
    // workspace usable against a newer core.
    qCDebug(lcSession) << "ignoring unhandled core event" << type;
}
