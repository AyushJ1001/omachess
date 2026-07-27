#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "BoardModel.h"

struct OmachessSession;

// The workspace side of the command-and-event C ABI.
//
// QML calls the invokable methods to express player intent; every visible
// property here changes only when a core event says so. Nothing in the
// workspace decides what the board looks like, which moves exist, or what a
// game's result is — it reads the answers the core sent and draws them.
class WorkspaceSession : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(BoardModel *board READ board CONSTANT)
    Q_PROPERTY(QString orientation READ orientation NOTIFY boardChanged)
    Q_PROPERTY(QString variant READ variant NOTIFY boardChanged)
    Q_PROPERTY(QString sideToMove READ sideToMove NOTIFY boardChanged)
    Q_PROPERTY(bool inCheck READ inCheck NOTIFY boardChanged)

    // The played moves, as {number, side, san} entries in playing order.
    Q_PROPERTY(QVariantList moveList READ moveList NOTIFY boardChanged)
    Q_PROPERTY(QString uciMoves READ uciMoves NOTIFY boardChanged)
    // How many of those moves the Displayed Position includes.
    Q_PROPERTY(int cursor READ cursor NOTIFY boardChanged)
    Q_PROPERTY(bool reviewing READ reviewing NOTIFY boardChanged)

    // The squares of the move that produced the Displayed Position, or empty
    // strings when the position is the start of the game.
    Q_PROPERTY(QString lastMoveFrom READ lastMoveFrom NOTIFY boardChanged)
    Q_PROPERTY(QString lastMoveTo READ lastMoveTo NOTIFY boardChanged)

    Q_PROPERTY(QString resultLabel READ resultLabel NOTIFY boardChanged)
    Q_PROPERTY(QString resultStatus READ resultStatus NOTIFY boardChanged)
    Q_PROPERTY(QString resultScore READ resultScore NOTIFY boardChanged)
    Q_PROPERTY(bool gameOver READ gameOver NOTIFY boardChanged)
    Q_PROPERTY(bool clockEnabled READ clockEnabled NOTIFY boardChanged)
    Q_PROPERTY(bool clockRunning READ clockRunning NOTIFY boardChanged)
    Q_PROPERTY(bool gameSuspended READ gameSuspended NOTIFY boardChanged)
    Q_PROPERTY(bool canSuspendGame READ canSuspendGame NOTIFY boardChanged)
    Q_PROPERTY(int whiteClockMs READ whiteClockMs NOTIFY boardChanged)
    Q_PROPERTY(int blackClockMs READ blackClockMs NOTIFY boardChanged)
    Q_PROPERTY(QString whitePlayer READ whitePlayer NOTIFY boardChanged)
    Q_PROPERTY(QString blackPlayer READ blackPlayer NOTIFY boardChanged)
    Q_PROPERTY(QString gameEvent READ gameEvent NOTIFY boardChanged)
    Q_PROPERTY(QString gameDate READ gameDate NOTIFY boardChanged)
    Q_PROPERTY(QString gameTitle READ gameTitle NOTIFY boardChanged)
    Q_PROPERTY(QString gameTags READ gameTags NOTIFY boardChanged)
    Q_PROPERTY(bool positionSetup READ positionSetup NOTIFY boardChanged)
    Q_PROPERTY(QString positionClass READ positionClass NOTIFY boardChanged)
    Q_PROPERTY(QString setupFen READ setupFen NOTIFY boardChanged)
    Q_PROPERTY(QString setupError READ setupError NOTIFY boardChanged)
    Q_PROPERTY(QString positionCapabilities READ positionCapabilities NOTIFY boardChanged)
    Q_PROPERTY(QString displayedFen READ displayedFen NOTIFY boardChanged)
    Q_PROPERTY(bool displayedPositionRuleValid READ displayedPositionRuleValid NOTIFY boardChanged)
    Q_PROPERTY(QString activity READ activity NOTIFY boardChanged)
    Q_PROPERTY(QVariantMap sourceSnapshot READ sourceSnapshot NOTIFY analysisRecordChanged)
    Q_PROPERTY(QStringList recordSources READ recordSources NOTIFY analysisRecordChanged)
    Q_PROPERTY(QStringList recordDerivations READ recordDerivations NOTIFY analysisRecordChanged)
    Q_PROPERTY(int analysisMainLinePly READ analysisMainLinePly NOTIFY analysisRecordChanged)
    Q_PROPERTY(int analysisSidelineCount READ analysisSidelineCount NOTIFY analysisRecordChanged)
    Q_PROPERTY(int analysisAnnotationCount READ analysisAnnotationCount NOTIFY analysisRecordChanged)
    Q_PROPERTY(QVariantList analysisAnnotations READ analysisAnnotations NOTIFY analysisRecordChanged)
    Q_PROPERTY(QVariantList analysisSidelines READ analysisSidelines NOTIFY analysisRecordChanged)
    Q_PROPERTY(QVariantList pinnedEngineLines READ pinnedEngineLines NOTIFY analysisRecordChanged)
    Q_PROPERTY(QVariantList computerEvaluations READ computerEvaluations NOTIFY analysisRecordChanged)
    Q_PROPERTY(bool computerAnalysisComplete READ computerAnalysisComplete NOTIFY analysisRecordChanged)
    Q_PROPERTY(bool defaultAnalysis READ defaultAnalysis NOTIFY analysisRecordChanged)

    // Personal Library summaries from the Live Store.
    Q_PROPERTY(QVariantList libraryRecords READ libraryRecords NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList studies READ studies NOTIFY studiesChanged)
    // Open record tabs and the active Game Record id.
    Q_PROPERTY(QVariantList openTabs READ openTabs NOTIFY tabsChanged)
    Q_PROPERTY(QString activeRecordId READ activeRecordId NOTIFY tabsChanged)
    Q_PROPERTY(QString saveMode READ saveMode NOTIFY boardChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY boardChanged)
    Q_PROPERTY(bool needsUnsavedDecision READ needsUnsavedDecision NOTIFY boardChanged)

    // Shown when a prior Game Record can be restored after restart.
    Q_PROPERTY(bool restoreAvailable READ restoreAvailable NOTIFY restoreChanged)
    Q_PROPERTY(QString restoreLabel READ restoreLabel NOTIFY restoreChanged)
    Q_PROPERTY(QString storeError READ storeError CONSTANT)
    Q_PROPERTY(bool workshopActive READ workshopActive NOTIFY workshopChanged)
    Q_PROPERTY(int workshopStep READ workshopStep NOTIFY workshopChanged)
    Q_PROPERTY(int boardFiles READ boardFiles NOTIFY workshopChanged)
    Q_PROPERTY(int boardRanks READ boardRanks NOTIFY workshopChanged)
    Q_PROPERTY(QVariantList boardPresets READ boardPresets NOTIFY workshopChanged)
    Q_PROPERTY(QVariantList pieceCatalogue READ pieceCatalogue NOTIFY workshopChanged)
    Q_PROPERTY(QStringList selectedPieces READ selectedPieces NOTIFY workshopChanged)
    Q_PROPERTY(QString customPieceName READ customPieceName NOTIFY workshopChanged)
    Q_PROPERTY(QString customPieceLetter READ customPieceLetter NOTIFY workshopChanged)
    Q_PROPERTY(QString customPieceBetza READ customPieceBetza NOTIFY workshopChanged)
    Q_PROPERTY(QString betzaError READ betzaError NOTIFY workshopChanged)
    Q_PROPERTY(QString variantFen READ variantFen NOTIFY workshopChanged)
    Q_PROPERTY(bool workshopPositionRuleValid READ workshopPositionRuleValid NOTIFY workshopChanged)
    Q_PROPERTY(QVariantMap variantRules READ variantRules NOTIFY workshopChanged)
    Q_PROPERTY(QString ruleConflict READ ruleConflict NOTIFY workshopChanged)
    Q_PROPERTY(bool variantPlayable READ variantPlayable NOTIFY workshopChanged)
    Q_PROPERTY(QString variantValidationMessage READ variantValidationMessage NOTIFY workshopChanged)
    Q_PROPERTY(QString variantAnalysisEvaluation READ variantAnalysisEvaluation NOTIFY variantAnalysisChanged)
    Q_PROPERTY(QString variantAnalysisVariation READ variantAnalysisVariation NOTIFY variantAnalysisChanged)
    Q_PROPERTY(QString variantAnalysisEvaluator READ variantAnalysisEvaluator NOTIFY variantAnalysisChanged)
    Q_PROPERTY(QString variantAnalysisCaveat READ variantAnalysisCaveat NOTIFY variantAnalysisChanged)
    Q_PROPERTY(QVariantList pgnImportResults READ pgnImportResults NOTIFY pgnImportResultsChanged)

    // Library Portability Package: what the last export or restore said, and
    // the replacement a restore into a non-empty library is waiting on.
    Q_PROPERTY(QString libraryPackageMessage READ libraryPackageMessage
                   NOTIFY libraryPackageChanged)
    Q_PROPERTY(bool libraryReplacementPending READ libraryReplacementPending
                   NOTIFY libraryPackageChanged)
    Q_PROPERTY(QString libraryReplacementMessage READ libraryReplacementMessage
                   NOTIFY libraryPackageChanged)

public:
    explicit WorkspaceSession(QObject *parent = nullptr);
    ~WorkspaceSession() override;

    BoardModel *board() { return &m_board; }
    QString orientation() const { return field(QStringLiteral("orientation")); }
    QString variant() const { return field(QStringLiteral("variant")); }
    QString sideToMove() const { return field(QStringLiteral("sideToMove")); }
    bool inCheck() const { return m_state.value(QStringLiteral("inCheck")).toBool(); }

    QVariantList moveList() const;
    QString uciMoves() const;
    int cursor() const { return m_state.value(QStringLiteral("cursor")).toInt(); }
    bool reviewing() const { return m_state.value(QStringLiteral("reviewing")).toBool(); }

    QString lastMoveFrom() const { return lastMoveSquare(QStringLiteral("from")); }
    QString lastMoveTo() const { return lastMoveSquare(QStringLiteral("to")); }

    QString resultLabel() const { return result(QStringLiteral("label")); }
    QString resultStatus() const { return result(QStringLiteral("status")); }
    QString resultScore() const { return result(QStringLiteral("score")); }
    bool gameOver() const;
    bool clockEnabled() const { return clockField(QStringLiteral("enabled")).toBool(); }
    bool clockRunning() const { return clockField(QStringLiteral("running")).toBool(); }
    bool gameSuspended() const { return m_state.value(QStringLiteral("suspended")).toBool(); }
    bool canSuspendGame() const { return m_state.value(QStringLiteral("canSuspend")).toBool(); }
    int whiteClockMs() const { return clockField(QStringLiteral("whiteMs")).toInt(); }
    int blackClockMs() const { return clockField(QStringLiteral("blackMs")).toInt(); }
    QString whitePlayer() const { return metadataField(QStringLiteral("white")); }
    QString blackPlayer() const { return metadataField(QStringLiteral("black")); }
    QString gameEvent() const { return metadataField(QStringLiteral("event")); }
    QString gameDate() const { return metadataField(QStringLiteral("date")); }
    QString gameTitle() const { return metadataField(QStringLiteral("title")); }
    QString gameTags() const { return metadataField(QStringLiteral("tags")); }
    bool positionSetup() const { return field(QStringLiteral("activity")) == QStringLiteral("position_setup"); }
    QString positionClass() const { return field(QStringLiteral("positionClass")); }
    QString setupFen() const { return field(QStringLiteral("setupFen")); }
    QString setupError() const { return field(QStringLiteral("setupError")); }
    QString positionCapabilities() const { return field(QStringLiteral("positionCapabilities")); }
    QString displayedFen() const { return field(QStringLiteral("displayedFen")); }
    bool displayedPositionRuleValid() const
    {
        return m_state.value(QStringLiteral("displayedPositionRuleValid")).toBool();
    }
    QString activity() const { return field(QStringLiteral("activity")); }
    QVariantMap sourceSnapshot() const { return m_sourceSnapshot; }
    QStringList recordSources() const { return m_recordSources; }
    QStringList recordDerivations() const { return m_recordDerivations; }
    int analysisMainLinePly() const { return m_analysisMainLinePly; }
    int analysisSidelineCount() const { return m_analysisSidelineCount; }
    int analysisAnnotationCount() const { return m_analysisAnnotationCount; }
    QVariantList analysisAnnotations() const { return m_analysisAnnotations; }
    QVariantList analysisSidelines() const { return m_analysisSidelines; }
    QVariantList pinnedEngineLines() const { return m_pinnedEngineLines; }
    QVariantList computerEvaluations() const { return m_computerEvaluations; }
    bool computerAnalysisComplete() const { return m_computerAnalysisComplete; }
    bool defaultAnalysis() const { return m_defaultAnalysis; }

    QVariantList libraryRecords() const { return m_libraryRecords; }
    QVariantList studies() const { return m_studies; }
    QVariantList openTabs() const { return m_openTabs; }
    QString activeRecordId() const { return m_activeRecordId; }
    QString saveMode() const { return field(QStringLiteral("saveMode")); }
    bool dirty() const { return m_state.value(QStringLiteral("dirty")).toBool(); }
    bool needsUnsavedDecision() const
    {
        return m_state.value(QStringLiteral("needsUnsavedDecision")).toBool();
    }

    bool restoreAvailable() const { return m_restoreAvailable; }
    QString restoreLabel() const { return m_restoreLabel; }
    QString storeError() const { return m_storeError; }
    bool workshopActive() const { return m_workshopActive; }
    int workshopStep() const { return m_workshopStep; }
    int boardFiles() const { return m_boardFiles; }
    int boardRanks() const { return m_boardRanks; }
    QVariantList boardPresets() const { return m_boardPresets; }
    QVariantList pieceCatalogue() const { return m_pieceCatalogue; }
    QStringList selectedPieces() const { return m_selectedPieces; }
    QString customPieceName() const { return m_customPieceName; }
    QString customPieceLetter() const { return m_customPieceLetter; }
    QString customPieceBetza() const { return m_customPieceBetza; }
    QString betzaError() const { return m_betzaError; }
    QString variantFen() const { return m_variantFen; }
    bool workshopPositionRuleValid() const { return m_workshopPositionRuleValid; }
    QVariantMap variantRules() const { return m_variantRules; }
    bool variantPlayable() const { return m_variantPlayable; }
    QString variantValidationMessage() const { return m_variantValidationMessage; }
    QString variantAnalysisEvaluation() const { return m_variantAnalysisEvaluation; }
    QString variantAnalysisVariation() const { return m_variantAnalysisVariation; }
    QString variantAnalysisEvaluator() const { return m_variantAnalysisEvaluator; }
    QString variantAnalysisCaveat() const { return m_variantAnalysisCaveat; }
    QString ruleConflict() const { return m_ruleConflict; }
    QVariantList pgnImportResults() const { return m_pgnImportResults; }
    QString libraryPackageMessage() const { return m_libraryPackageMessage; }
    bool libraryReplacementPending() const { return !m_libraryReplacementMessage.isEmpty(); }
    QString libraryReplacementMessage() const { return m_libraryReplacementMessage; }

    // Asks the core to describe the board it owns. Called once at startup so
    // the first frame is drawn from core-owned state.
    Q_INVOKABLE void describeBoard();

    // Player intent: swap which side is at the bottom.
    Q_INVOKABLE void flipBoard();

    // Player intent: play a move. `promotion` names the piece a promoting
    // pawn becomes and is empty for every other move. The core decides
    // whether the move actually happens.
    Q_INVOKABLE void playMove(const QString &from, const QString &to,
                              const QString &promotion = QString());

    // Player intent: show a different position of this game. `destination` is
    // "backward", "forward", "start", or "end".
    Q_INVOKABLE void navigate(const QString &destination);

    // Player intent: restore the Game Record offered after restart.
    Q_INVOKABLE void restoreRecord();
    Q_INVOKABLE void suspendGame();
    Q_INVOKABLE void resumeGame();

    // Player intent: decline the restore offer and keep the fresh board.
    Q_INVOKABLE void dismissRestore();

    // Player intent: clear the board so the next move starts a new Game Record.
    Q_INVOKABLE void newGame();

    // Player intent: open a Personal Library record in a tab (or focus it).
    Q_INVOKABLE void openRecord(const QString &id);

    // Player intent: close a tab without removing the record from the library.
    Q_INVOKABLE void closeTab(const QString &id);
    Q_INVOKABLE void archiveRecord(const QString &id);
    Q_INVOKABLE void unarchiveRecord(const QString &id);
    Q_INVOKABLE void setLibraryView(const QString &view);
    Q_INVOKABLE void purgeRecord(const QString &id);
    Q_INVOKABLE void purgeStudy(const QString &studyId);
    Q_INVOKABLE void purgeVariantDefinition();
    Q_INVOKABLE void createStudy(const QString &name);
    Q_INVOKABLE void addStudyRecord(const QString &studyId, const QString &recordId);
    Q_INVOKABLE void removeStudyRecord(const QString &studyId, const QString &recordId);
    Q_INVOKABLE void reorderStudyRecord(const QString &studyId, const QString &recordId,
                                        int position);
    Q_INVOKABLE void setSaveMode(const QString &mode);
    Q_INVOKABLE void saveRecord();
    Q_INVOKABLE void discardChanges();
    Q_INVOKABLE void configureClock(int milliseconds);
    Q_INVOKABLE void tickClock();
    Q_INVOKABLE void updateMetadata(const QString &white, const QString &black,
                                    const QString &event, const QString &date,
                                    const QString &title, const QString &tags);
    Q_INVOKABLE void beginPositionSetup();
    Q_INVOKABLE void setSetupFen(const QString &fen);
    Q_INVOKABLE void placeSetupPiece(const QString &square, const QString &piece);
    Q_INVOKABLE void relocateSetupPiece(const QString &from, const QString &to);
    Q_INVOKABLE void startSetupGame();
    Q_INVOKABLE void newVariantDefinition();
    Q_INVOKABLE void selectBoardPreset(const QString &id);
    Q_INVOKABLE void setWorkshopStep(int step);
    Q_INVOKABLE void toggleBuiltinPiece(const QString &code);
    Q_INVOKABLE void setCustomPiece(const QString &name, const QString &letter,
                                    const QString &betza);
    Q_INVOKABLE void placeWorkshopPiece(const QString &square, const QString &piece);
    Q_INVOKABLE void toggleVariantRule(const QString &rule);
    Q_INVOKABLE void validateVariantDefinition();
    Q_INVOKABLE void editVariantDefinition();
    Q_INVOKABLE void importPgn();

    // Player intent: take the whole library away as a Library Portability
    // Package, or bring one back. Both choose their file through the portal.
    Q_INVOKABLE void exportLibraryPackage();
    Q_INVOKABLE void restoreLibraryPackage();

    // Player intent: answer the replacement a restore into a non-empty
    // library asked for.
    Q_INVOKABLE void confirmLibraryReplacement();
    Q_INVOKABLE void cancelLibraryReplacement();

    Q_INVOKABLE void exportPgn(const QStringList &recordIds);
    Q_INVOKABLE void deriveAnalysisRecord();
    Q_INVOKABLE void completeComputerAnalysis(const QString &evaluations);
    // Starts the worker-owned finite pass and returns the durable Background Job id.
    // An empty id means no compatible worker boundary is available.
    Q_INVOKABLE QString startBackgroundComputerAnalysis(const QString &searchSettings,
                                                        int searchTimeMs,
                                                        int lineLimit);
    Q_INVOKABLE void pauseBackgroundJob(const QString &id);
    Q_INVOKABLE void resumeBackgroundJob(const QString &id,
                                         const QString &searchSettings,
                                         int searchTimeMs,
                                         int lineLimit);
    Q_INVOKABLE void cancelBackgroundJob(const QString &id);
    Q_INVOKABLE void dismissBackgroundJob(const QString &id);
    Q_INVOKABLE QString backgroundJob(const QString &id);
    Q_INVOKABLE QString backgroundJobs();
    Q_INVOKABLE bool importBackgroundComputerAnalysis(const QString &id);
    Q_INVOKABLE void designateDefaultAnalysis();
    Q_INVOKABLE void addAnalysisAnnotation(int ply, const QString &text);
    Q_INVOKABLE void addAnalysisSideline(int afterPly, const QString &variation);
    Q_INVOKABLE void pinEngineLine(const QString &positionFen, const QString &evaluation,
                                   const QString &variation, const QString &engine,
                                   const QString &searchContext);

    // --- What the core said a player may do -------------------------------
    //
    // These read the moves the last event carried. They are how the board
    // knows what a player may pick up and where they may drop it; the core
    // still refuses anything else that reaches it.

    // The coordinate of the square the core placed at display position
    // `index`, or "" when there is no such square. Display position 0 is the
    // top-left square of the board as it is currently drawn.
    Q_INVOKABLE QString squareNameAt(int index) const;

    // The piece the core placed on `square`, or "" when it is empty.
    Q_INVOKABLE QString pieceOn(const QString &square) const;

    // Whether a piece on `square` has anywhere to go.
    Q_INVOKABLE bool canPickUp(const QString &square) const;

    // The squares a piece on `from` may be dropped on.
    Q_INVOKABLE QStringList destinationsFrom(const QString &from) const;

    // The pieces a pawn moving from `from` to `to` may become, or an empty
    // list when that move is not a promotion.
    Q_INVOKABLE QStringList promotionsFor(const QString &from, const QString &to) const;

signals:
    void boardChanged();
    void libraryChanged();
    void tabsChanged();
    void restoreChanged();
    void workshopChanged();
    void variantAnalysisChanged();
    void pgnImportResultsChanged();
    void libraryPackageChanged();
    void analysisRecordChanged();
    void studiesChanged();

private:
    // Submits a command and applies every event it produced.
    void submit(const QByteArray &commandJson);
    bool submitAndDrain(const QByteArray &commandJson);
    void applyEvent(const QByteArray &eventJson);

    // The moves the last event said a player may make.
    QJsonArray movesOffered() const;

    QString field(const QString &name) const;
    QString result(const QString &name) const;
    QString lastMoveSquare(const QString &name) const;
    QJsonValue clockField(const QString &name) const;
    QString metadataField(const QString &name) const;

    OmachessSession *m_session = nullptr;
    BoardModel m_board;
    // The last board_changed event, kept whole so every property answers from
    // one core-owned snapshot.
    QJsonObject m_state;
    QVariantList m_libraryRecords;
    QVariantList m_studies;
    QVariantList m_boardPresets;
    QVariantList m_pieceCatalogue;
    QVariantList m_openTabs;
    QString m_activeRecordId;
    bool m_restoreAvailable = false;
    QString m_restoreLabel;
    QString m_storeError;
    bool m_workshopActive = false;
    int m_workshopStep = 1;
    int m_boardFiles = 8;
    int m_boardRanks = 8;
    QString m_boardPresetId = QStringLiteral("standard-8x8");
    QStringList m_selectedPieces{QStringLiteral("K"), QStringLiteral("Q"),
                                 QStringLiteral("R"), QStringLiteral("B"),
                                 QStringLiteral("N"), QStringLiteral("P")};
    QString m_customPieceName;
    QString m_customPieceLetter;
    QString m_customPieceBetza;
    QString m_betzaError;
    QString m_variantFen;
    bool m_workshopPositionRuleValid = false;
    QVariantMap m_variantRules;
    QString m_ruleConflict;
    bool m_variantPlayable = false;
    QString m_variantValidationMessage;
    QString m_variantAnalysisEvaluation;
    QString m_variantAnalysisVariation;
    QString m_variantAnalysisEvaluator;
    QString m_variantAnalysisCaveat;
    QVariantList m_pgnImportResults;
    QString m_exportPath;
    QString m_packageExportPath;
    // The package a restore is holding until the player confirms replacement.
    QString m_pendingPackage;
    QString m_libraryReplacementMessage;
    QString m_libraryPackageMessage;
    QVariantMap m_sourceSnapshot;
    QStringList m_recordSources;
    QStringList m_recordDerivations;
    int m_analysisMainLinePly = 0;
    int m_analysisSidelineCount = 0;
    int m_analysisAnnotationCount = 0;
    QVariantList m_analysisAnnotations;
    QVariantList m_analysisSidelines;
    QVariantList m_pinnedEngineLines;
    QVariantList m_computerEvaluations;
    bool m_computerAnalysisComplete = false;
    bool m_defaultAnalysis = false;
};
