//! The Live Store: the SQLite source of truth for the Personal Library.
//!
//! Both the workspace and the later background worker open this store
//! directly. Writes are partitioned by table so the two processes share one
//! schema without redesign.
//!
//! The on-disk schema is internal. What is promised publicly is the XDG
//! location, the schema version, fail-closed migration, and backup guidance
//! in `docs/backup.md`.

use std::path::{Path, PathBuf};

use rusqlite::{Connection, OpenFlags};
use serde::{Deserialize, Serialize};

/// Schema version this build of Omachess understands and writes.
pub const SCHEMA_VERSION: u32 = 4;

/// Why the Live Store could not be opened for use.
#[derive(Debug)]
pub enum OpenError {
    CreateDirectory {
        path: PathBuf,
        source: std::io::Error,
    },
    OpenFile {
        path: PathBuf,
        source: rusqlite::Error,
    },
    Configure {
        path: PathBuf,
        source: rusqlite::Error,
    },
    /// The launch migration failed. The previous store is untouched.
    Migration {
        path: PathBuf,
        detail: String,
    },
}

impl std::fmt::Display for OpenError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            OpenError::CreateDirectory { path, source } => {
                write!(
                    f,
                    "could not create Live Store directory {}: {source}",
                    path.display()
                )
            }
            OpenError::OpenFile { path, source } => {
                write!(f, "could not open Live Store at {}: {source}", path.display())
            }
            OpenError::Configure { path, source } => {
                write!(
                    f,
                    "could not configure Live Store at {}: {source}",
                    path.display()
                )
            }
            OpenError::Migration { path, detail } => {
                write!(
                    f,
                    "Live Store migration failed for {}: {}. The previous store was left untouched.",
                    path.display(),
                    detail
                )
            }
        }
    }
}

impl std::error::Error for OpenError {}

/// Failures using an open Live Store.
#[derive(Debug)]
pub enum StoreError {
    Sqlite(rusqlite::Error),
    Message(String),
}

impl std::fmt::Display for StoreError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            StoreError::Sqlite(error) => write!(f, "{error}"),
            StoreError::Message(message) => write!(f, "{message}"),
        }
    }
}

impl std::error::Error for StoreError {}

impl From<rusqlite::Error> for StoreError {
    fn from(error: rusqlite::Error) -> Self {
        StoreError::Sqlite(error)
    }
}

/// The open Live Store.
///
/// Opening always runs the launch migration in one transaction and fails
/// closed: a failure leaves the previous store untouched and returns a clear
/// error.
pub struct LiveStore {
    conn: Connection,
    path: PathBuf,
}

/// Whether a Game Record is a Played Game or an Analysis Record.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum GameRecordKind {
    Played,
    Analysis,
}

impl GameRecordKind {
    pub fn as_str(self) -> &'static str {
        match self {
            GameRecordKind::Played => "played",
            GameRecordKind::Analysis => "analysis",
        }
    }

    pub fn parse(name: &str) -> Option<Self> {
        match name {
            "played" => Some(GameRecordKind::Played),
            "analysis" => Some(GameRecordKind::Analysis),
            _ => None,
        }
    }
}

/// One move as a Game Record keeps it.
#[derive(Clone, PartialEq, Eq, Debug, Serialize, Deserialize)]
pub struct MoveEntry {
    pub uci: String,
    pub san: String,
    pub number: u32,
    pub side: String,
}

/// The result of a Game Record, present only when the game has ended.
#[derive(Clone, PartialEq, Eq, Debug, Serialize, Deserialize)]
pub struct RecordResult {
    pub status: String,
    pub termination: String,
    pub score: String,
}

/// The common history object: starting position plus move tree.
///
/// Participation, clock, and result fields are present only when applicable.
#[derive(Clone, PartialEq, Eq, Debug)]
pub struct GameRecordPayload {
    pub variant: String,
    pub start_fen: String,
    pub moves: Vec<MoveEntry>,
    pub result: Option<RecordResult>,
    pub participation: Option<String>,
    pub clock: Option<String>,
}

impl GameRecordPayload {
    pub const STANDARD_START: &'static str =
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    /// Payload format version written with every Game Record blob.
    pub const VERSION: u32 = 1;

    pub fn empty_standard() -> Self {
        GameRecordPayload {
            variant: "standard".into(),
            start_fen: Self::STANDARD_START.to_owned(),
            moves: Vec::new(),
            result: None,
            participation: None,
            clock: None,
        }
    }
}

/// A Game Record as the Live Store holds it.
#[derive(Clone, PartialEq, Eq, Debug)]
pub struct GameRecord {
    pub id: String,
    pub kind: GameRecordKind,
    pub title: Option<String>,
    pub result_score: Option<String>,
    pub ply_count: u32,
    pub archived: bool,
    pub created_at: String,
    pub updated_at: String,
    pub payload: GameRecordPayload,
}

/// Immutable chess content and metadata copied when an Analysis Record is derived.
#[derive(Clone, PartialEq, Eq, Debug, Serialize, Deserialize)]
pub struct SourceSnapshot {
    pub source_id: String,
    pub variant: String,
    pub start_fen: String,
    pub moves: Vec<MoveEntry>,
    pub result: Option<RecordResult>,
    pub metadata: Option<String>,
}

/// A named alternative continuation owned by an Analysis Record.
#[derive(Clone, PartialEq, Eq, Debug, Serialize, Deserialize)]
pub struct AnalysisSideline {
    pub after_ply: u32,
    pub moves: Vec<MoveEntry>,
}

/// Durable prose attached to a position in an Analysis Record.
#[derive(Clone, PartialEq, Eq, Debug, Serialize, Deserialize)]
pub struct AnalysisAnnotation {
    pub ply: u32,
    pub text: String,
}

/// One explicitly preserved principal variation from Live Position Analysis.
#[derive(Clone, PartialEq, Eq, Debug, Serialize, Deserialize)]
pub struct PinnedEngineLine {
    pub position_fen: String,
    pub evaluation: String,
    pub variation: String,
    pub engine: String,
    pub search_context: String,
}

/// Engine review material for one position in a completed finite pass.
#[derive(Clone, PartialEq, Eq, Debug, Serialize, Deserialize)]
pub struct ComputerEvaluation {
    pub ply: u32,
    pub position_fen: String,
    pub evaluation: String,
    pub glyph: String,
    pub better_line: Option<String>,
}

/// Analysis-owned content. None of it is shared with the source or sibling derivations.
#[derive(Clone, PartialEq, Eq, Debug, Serialize, Deserialize)]
pub struct AnalysisRecordData {
    pub source_snapshot: SourceSnapshot,
    pub main_line: Vec<MoveEntry>,
    pub sidelines: Vec<AnalysisSideline>,
    pub annotations: Vec<AnalysisAnnotation>,
    pub pinned_lines: Vec<PinnedEngineLine>,
    #[serde(default)]
    pub computer_evaluations: Vec<ComputerEvaluation>,
    #[serde(default)]
    pub computer_analysis_complete: bool,
    #[serde(default)]
    pub default_analysis: bool,
}

#[derive(Clone, PartialEq, Eq, Debug)]
pub struct Study {
    pub id: String,
    pub name: String,
    pub record_ids: Vec<String>,
}

/// The durable lifecycle of a Background Job. A worker marks in-flight work
/// interrupted when it starts; resumption is therefore always an explicit
/// request and never an accidental restart after a crash or logout.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum BackgroundJobState {
    Queued,
    Running,
    Paused,
    Interrupted,
    Complete,
    Cancelled,
    Failed,
    Dismissed,
}

impl BackgroundJobState {
    fn as_str(self) -> &'static str {
        match self {
            Self::Queued => "queued", Self::Running => "running", Self::Paused => "paused",
            Self::Interrupted => "interrupted", Self::Complete => "complete",
            Self::Cancelled => "cancelled", Self::Failed => "failed", Self::Dismissed => "dismissed",
        }
    }
    fn parse(value: &str) -> Option<Self> {
        Some(match value {
            "queued" => Self::Queued, "running" => Self::Running, "paused" => Self::Paused,
            "interrupted" => Self::Interrupted, "complete" => Self::Complete,
            "cancelled" => Self::Cancelled, "failed" => Self::Failed, "dismissed" => Self::Dismissed, _ => return None,
        })
    }

    /// Parse the stable D-Bus / C-ABI spelling of a lifecycle state.
    pub fn parse_public(value: &str) -> Option<Self> { Self::parse(value) }

    fn can_transition_to(self, next: Self) -> bool {
        matches!((self, next),
            (Self::Queued, Self::Running)
            | (Self::Running, Self::Paused | Self::Interrupted | Self::Complete | Self::Cancelled | Self::Failed)
            | (Self::Paused, Self::Running | Self::Cancelled)
            | (Self::Interrupted, Self::Running | Self::Cancelled | Self::Dismissed))
    }
}

/// Worker-owned durable facts. `checkpoint` is a completed move boundary.
#[derive(Clone, PartialEq, Eq, Debug)]
pub struct BackgroundJob {
    pub id: String,
    pub kind: String,
    pub state: BackgroundJobState,
    pub record_id: String,
    pub checkpoint: u32,
    pub total: u32,
    pub controls: Vec<String>,
    pub payload: String,
    pub updated_at: String,
}

/// Workspace write partition: Game Records, residue, and other library tables.
pub struct WorkspaceWriter<'a> {
    conn: &'a Connection,
}

impl<'a> WorkspaceWriter<'a> {
    fn new(conn: &'a Connection) -> Self {
        WorkspaceWriter { conn }
    }

    pub fn upsert_game_record(&self, record: &GameRecord) -> Result<(), StoreError> {
        let payload = encode_payload(&record.payload)?;
        self.conn.execute(
            "
            INSERT INTO game_records (
                id, kind, title, result_score, ply_count, archived,
                created_at, updated_at, payload_version, payload
            ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)
            ON CONFLICT(id) DO UPDATE SET
                kind = excluded.kind,
                title = excluded.title,
                result_score = excluded.result_score,
                ply_count = excluded.ply_count,
                archived = excluded.archived,
                updated_at = excluded.updated_at,
                payload_version = excluded.payload_version,
                payload = excluded.payload
            ",
            rusqlite::params![
                record.id,
                record.kind.as_str(),
                record.title,
                record.result_score,
                record.ply_count,
                if record.archived { 1 } else { 0 },
                record.created_at,
                record.updated_at,
                GameRecordPayload::VERSION,
                payload,
            ],
        )?;
        Ok(())
    }

    pub fn get_game_record(&self, id: &str) -> Result<Option<GameRecord>, StoreError> {
        let mut statement = self.conn.prepare(
            "
            SELECT id, kind, title, result_score, ply_count, archived,
                   created_at, updated_at, payload_version, payload
            FROM game_records
            WHERE id = ?1
            ",
        )?;
        let mut rows = statement.query(rusqlite::params![id])?;
        let Some(row) = rows.next()? else {
            return Ok(None);
        };
        Ok(Some(row_to_record(row)?))
    }

    pub fn derive_analysis_record(
        &self,
        source_id: &str,
        derived_id: &str,
        created_at: &str,
    ) -> Result<AnalysisRecordData, StoreError> {
        let source = self
            .get_game_record(source_id)?
            .ok_or_else(|| StoreError::Message("source Game Record is unavailable".into()))?;
        if source.kind == GameRecordKind::Played && source.payload.result.is_none() {
            return Err(StoreError::Message(
                "only a Completed Game or Analysis Record can produce an Analysis Record".into(),
            ));
        }
        let snapshot = SourceSnapshot {
            source_id: source.id.clone(),
            variant: source.payload.variant.clone(),
            start_fen: source.payload.start_fen.clone(),
            moves: source.payload.moves.clone(),
            result: source.payload.result.clone(),
            metadata: source.payload.participation.clone(),
        };
        let data = AnalysisRecordData {
            source_snapshot: snapshot,
            main_line: source.payload.moves.clone(),
            sidelines: Vec::new(),
            annotations: Vec::new(),
            pinned_lines: Vec::new(),
            computer_evaluations: Vec::new(),
            computer_analysis_complete: false,
            default_analysis: false,
        };
        let mut payload = source.payload.clone();
        payload.result = None;
        payload.clock = None;
        let record = GameRecord {
            id: derived_id.to_owned(),
            kind: GameRecordKind::Analysis,
            title: source
                .title
                .as_ref()
                .map(|title| format!("Analysis of {title}"))
                .or_else(|| Some("Analysis Record".into())),
            result_score: None,
            ply_count: payload.moves.len() as u32,
            archived: false,
            created_at: created_at.to_owned(),
            updated_at: created_at.to_owned(),
            payload,
        };
        let transaction = self.conn.unchecked_transaction()?;
        {
            let writer = WorkspaceWriter::new(&transaction);
            writer.upsert_game_record(&record)?;
        }
        transaction.execute(
            "INSERT INTO analysis_records (record_id, content) VALUES (?1, ?2)",
            rusqlite::params![derived_id, serde_json::to_string(&data).map_err(json_error)?],
        )?;
        transaction.execute(
            "INSERT INTO record_edges (source_id, derived_id, edge_type) VALUES (?1, ?2, 'derived_from')",
            rusqlite::params![source_id, derived_id],
        )?;
        transaction.commit()?;
        Ok(data)
    }

    pub fn analysis_record(&self, id: &str) -> Result<Option<AnalysisRecordData>, StoreError> {
        match self.conn.query_row(
            "SELECT content FROM analysis_records WHERE record_id = ?1",
            [id],
            |row| row.get::<_, String>(0),
        ) {
            Ok(content) => serde_json::from_str(&content).map(Some).map_err(json_error),
            Err(rusqlite::Error::QueryReturnedNoRows) => Ok(None),
            Err(error) => Err(error.into()),
        }
    }

    fn update_analysis(
        &self,
        id: &str,
        change: impl FnOnce(&mut AnalysisRecordData),
    ) -> Result<(), StoreError> {
        let mut data = self
            .analysis_record(id)?
            .ok_or_else(|| StoreError::Message("Analysis Record is unavailable".into()))?;
        change(&mut data);
        self.conn.execute(
            "UPDATE analysis_records SET content = ?2 WHERE record_id = ?1",
            rusqlite::params![id, serde_json::to_string(&data).map_err(json_error)?],
        )?;
        Ok(())
    }

    pub fn add_annotation(&self, id: &str, ply: u32, text: &str) -> Result<(), StoreError> {
        self.update_analysis(id, |data| {
            data.annotations.push(AnalysisAnnotation {
                ply,
                text: text.to_owned(),
            })
        })
    }

    pub fn add_sideline(&self, id: &str, sideline: AnalysisSideline) -> Result<(), StoreError> {
        self.update_analysis(id, |data| data.sidelines.push(sideline))
    }

    pub fn pin_engine_line(
        &self,
        id: &str,
        line: &PinnedEngineLine,
    ) -> Result<(), StoreError> {
        self.update_analysis(id, |data| data.pinned_lines.push(line.clone()))
    }

    /// Completes a finite engine pass and optionally makes it the source's sole default.
    pub fn complete_computer_analysis(
        &self,
        id: &str,
        evaluations: Vec<ComputerEvaluation>,
        make_default: bool,
    ) -> Result<(), StoreError> {
        self.update_analysis(id, |data| {
            data.computer_evaluations = evaluations;
            data.computer_analysis_complete = true;
            data.default_analysis = false;
        })?;
        if make_default {
            self.designate_default_analysis(id)?;
        }
        Ok(())
    }

    pub fn designate_default_analysis(&self, id: &str) -> Result<(), StoreError> {
        let transaction = self.conn.unchecked_transaction()?;
        let writer = WorkspaceWriter::new(&transaction);
        let analysis = writer
            .analysis_record(id)?
            .ok_or_else(|| StoreError::Message("Analysis Record is unavailable".into()))?;
        let source = writer
            .get_game_record(&analysis.source_snapshot.source_id)?
            .ok_or_else(|| StoreError::Message("source Completed Game is unavailable".into()))?;
        if source.kind != GameRecordKind::Played || source.payload.result.is_none() {
            return Err(StoreError::Message(
                "Default Analysis must be directly associated with a Completed Game".into(),
            ));
        }
        for sibling in writer.derivations_from(&analysis.source_snapshot.source_id)? {
            if sibling != id {
                writer.update_analysis(&sibling, |data| data.default_analysis = false)?;
            }
        }
        writer.update_analysis(id, |data| data.default_analysis = true)?;
        transaction.commit()?;
        Ok(())
    }

    pub fn derivations_from(&self, id: &str) -> Result<Vec<String>, StoreError> {
        self.edge_ids(
            "SELECT derived_id FROM record_edges WHERE source_id = ?1 AND edge_type = 'derived_from' ORDER BY created_at, derived_id",
            id,
        )
    }

    pub fn sources_of(&self, id: &str) -> Result<Vec<String>, StoreError> {
        self.edge_ids(
            "SELECT source_id FROM record_edges WHERE derived_id = ?1 AND edge_type = 'derived_from' ORDER BY created_at, source_id",
            id,
        )
    }

    fn edge_ids(&self, sql: &str, id: &str) -> Result<Vec<String>, StoreError> {
        let mut statement = self.conn.prepare(sql)?;
        let rows = statement.query_map([id], |row| row.get(0))?;
        rows.collect::<Result<Vec<_>, _>>().map_err(Into::into)
    }

    pub fn purge_game_record(&self, id: &str) -> Result<(), StoreError> {
        self.conn.execute("DELETE FROM game_records WHERE id = ?1", [id])?;
        Ok(())
    }

    /// Purges the library Variant Definition only when no Game Record still
    /// carries an immutable snapshot compiled from it.
    pub fn purge_variant_definition(&self) -> Result<(), StoreError> {
        let bound: bool = self.conn.query_row(
            "SELECT EXISTS(
                SELECT 1 FROM game_records
                WHERE json_extract(payload, '$.variant') LIKE 'omachess:v1:%'
            )",
            [],
            |row| row.get(0),
        )?;
        if bound {
            return Err(StoreError::Message(
                "the Variant Definition is bound into an existing Variant Snapshot".into(),
            ));
        }
        self.clear_residue("variant_definition_draft")
    }

    pub fn create_study(&self, id: &str, name: &str, created_at: &str) -> Result<(), StoreError> {
        if name.trim().is_empty() {
            return Err(StoreError::Message("a Study needs a name".into()));
        }
        self.conn.execute(
            "INSERT INTO studies (id, name, created_at, updated_at) VALUES (?1, ?2, ?3, ?3)",
            rusqlite::params![id, name.trim(), created_at],
        )?;
        Ok(())
    }

    pub fn add_study_record(&self, study_id: &str, record_id: &str) -> Result<(), StoreError> {
        let eligible: bool = self.conn.query_row(
            "SELECT kind = 'analysis' OR result_score IS NOT NULL FROM game_records WHERE id = ?1",
            [record_id],
            |row| row.get(0),
        ).map_err(|error| match error {
            rusqlite::Error::QueryReturnedNoRows =>
                StoreError::Message("Game Record is unavailable".into()),
            other => other.into(),
        })?;
        if !eligible {
            return Err(StoreError::Message(
                "unfinished Played Games cannot belong to a Study".into(),
            ));
        }
        self.conn.execute(
            "INSERT OR IGNORE INTO study_records (study_id, record_id, position)
             VALUES (?1, ?2, COALESCE((SELECT MAX(position) + 1 FROM study_records WHERE study_id = ?1), 0))",
            rusqlite::params![study_id, record_id],
        )?;
        Ok(())
    }

    pub fn reorder_study_record(
        &self,
        study_id: &str,
        record_id: &str,
        position: usize,
    ) -> Result<(), StoreError> {
        let ids = self.study(study_id)?.ok_or_else(|| StoreError::Message("Study is unavailable".into()))?.record_ids;
        let Some(from) = ids.iter().position(|id| id == record_id) else {
            return Err(StoreError::Message("Game Record is not in the Study".into()));
        };
        let mut reordered = ids;
        let id = reordered.remove(from);
        reordered.insert(position.min(reordered.len()), id);
        let transaction = self.conn.unchecked_transaction()?;
        transaction.execute(
            "UPDATE study_records SET position = -position - 1 WHERE study_id = ?1",
            [study_id],
        )?;
        for (index, id) in reordered.iter().enumerate() {
            transaction.execute(
                "UPDATE study_records SET position = ?3 WHERE study_id = ?1 AND record_id = ?2",
                rusqlite::params![study_id, id, index as i64],
            )?;
        }
        transaction.commit()?;
        Ok(())
    }

    pub fn remove_study_record(&self, study_id: &str, record_id: &str) -> Result<(), StoreError> {
        self.conn.execute(
            "DELETE FROM study_records WHERE study_id = ?1 AND record_id = ?2",
            rusqlite::params![study_id, record_id],
        )?;
        Ok(())
    }

    pub fn study(&self, id: &str) -> Result<Option<Study>, StoreError> {
        let name = match self.conn.query_row(
            "SELECT name FROM studies WHERE id = ?1", [id], |row| row.get::<_, String>(0)
        ) {
            Ok(name) => name,
            Err(rusqlite::Error::QueryReturnedNoRows) => return Ok(None),
            Err(error) => return Err(error.into()),
        };
        let mut statement = self.conn.prepare(
            "SELECT record_id FROM study_records WHERE study_id = ?1 ORDER BY position, record_id"
        )?;
        let record_ids = statement.query_map([id], |row| row.get(0))?
            .collect::<Result<Vec<_>, _>>()?;
        Ok(Some(Study { id: id.into(), name, record_ids }))
    }

    pub fn list_studies(&self) -> Result<Vec<Study>, StoreError> {
        let mut statement = self.conn.prepare("SELECT id FROM studies ORDER BY created_at, id")?;
        let study_ids = statement.query_map([], |row| row.get::<_, String>(0))?
            .collect::<Result<Vec<_>, _>>()?;
        study_ids
            .iter()
            .map(|id| self.study(id).map(Option::unwrap))
            .collect()
    }

    pub fn set_residue(&self, key: &str, value: &str) -> Result<(), StoreError> {
        self.conn.execute(
            "
            INSERT INTO workspace_residue (key, value) VALUES (?1, ?2)
            ON CONFLICT(key) DO UPDATE SET value = excluded.value
            ",
            rusqlite::params![key, value],
        )?;
        Ok(())
    }

    pub fn residue(&self, key: &str) -> Result<Option<String>, StoreError> {
        match self.conn.query_row(
            "SELECT value FROM workspace_residue WHERE key = ?1",
            rusqlite::params![key],
            |row| row.get(0),
        ) {
            Ok(value) => Ok(Some(value)),
            Err(rusqlite::Error::QueryReturnedNoRows) => Ok(None),
            Err(error) => Err(error.into()),
        }
    }

    pub fn clear_residue(&self, key: &str) -> Result<(), StoreError> {
        self.conn
            .execute("DELETE FROM workspace_residue WHERE key = ?1", rusqlite::params![key])?;
        Ok(())
    }

    /// Summaries of every Game Record, newest first — for library listing later.
    pub fn list_game_records(&self) -> Result<Vec<GameRecordSummary>, StoreError> {
        let mut statement = self.conn.prepare(
            "
            SELECT id, kind, title, result_score, ply_count, archived, updated_at
            FROM game_records
            ORDER BY updated_at DESC, id ASC
            ",
        )?;
        let rows = statement.query_map([], |row| {
            let kind_name: String = row.get(1)?;
            Ok((
                row.get::<_, String>(0)?,
                kind_name,
                row.get::<_, Option<String>>(2)?,
                row.get::<_, Option<String>>(3)?,
                row.get::<_, i64>(4)?,
                row.get::<_, i64>(5)?,
                row.get::<_, String>(6)?,
            ))
        })?;
        let mut summaries = Vec::new();
        for row in rows {
            let (id, kind_name, title, result_score, ply_count, archived, updated_at) = row?;
            let kind = GameRecordKind::parse(&kind_name).ok_or_else(|| {
                StoreError::Message(format!("unknown Game Record kind: {kind_name}"))
            })?;
            summaries.push(GameRecordSummary {
                id,
                kind,
                title,
                result_score,
                ply_count: ply_count as u32,
                archived: archived != 0,
                updated_at,
            });
        }
        Ok(summaries)
    }
}

/// Columns a library list needs without loading the move-tree payload.
#[derive(Clone, PartialEq, Eq, Debug)]
pub struct GameRecordSummary {
    pub id: String,
    pub kind: GameRecordKind,
    pub title: Option<String>,
    pub result_score: Option<String>,
    pub ply_count: u32,
    pub archived: bool,
    pub updated_at: String,
}

/// Worker write partition: Background Jobs (and later Analysis Record completion).
pub struct WorkerWriter<'a> {
    conn: &'a Connection,
}

impl<'a> WorkerWriter<'a> {
    fn new(conn: &'a Connection) -> Self {
        WorkerWriter { conn }
    }

    /// Confirms the worker tables are present and usable.
    pub fn ensure_ready(&self) -> Result<(), StoreError> {
        let count: i64 = self.conn.query_row(
            "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'background_jobs'",
            [],
            |row| row.get(0),
        )?;
        if count != 1 {
            return Err(StoreError::Message(
                "background_jobs table missing from Live Store".into(),
            ));
        }
        Ok(())
    }

    pub fn create_job(&self, job: &BackgroundJob) -> Result<(), StoreError> {
        self.conn.execute(
            "INSERT INTO background_jobs (id, kind, state, record_id, checkpoint, total, controls, updated_at, payload) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)",
            rusqlite::params![job.id, job.kind, job.state.as_str(), job.record_id, job.checkpoint, job.total, job.controls.join(","), job.updated_at, job.payload],
        )?;
        Ok(())
    }

    pub fn job(&self, id: &str) -> Result<Option<BackgroundJob>, StoreError> {
        match self.conn.query_row("SELECT id, kind, state, record_id, checkpoint, total, controls, updated_at, payload FROM background_jobs WHERE id = ?1", [id], |row| {
            let state: String = row.get(2)?;
            Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?, state, row.get::<_, String>(3)?, row.get::<_, u32>(4)?, row.get::<_, u32>(5)?, row.get::<_, String>(6)?, row.get::<_, String>(7)?, row.get::<_, String>(8)?))
        }) {
            Ok((id, kind, state, record_id, checkpoint, total, controls, updated_at, payload)) => Ok(Some(BackgroundJob { id, kind, state: BackgroundJobState::parse(&state).ok_or_else(|| StoreError::Message(format!("unknown Background Job state: {state}")))?, record_id, checkpoint, total, controls: controls.split(',').filter(|s| !s.is_empty()).map(str::to_owned).collect(), payload, updated_at })),
            Err(rusqlite::Error::QueryReturnedNoRows) => Ok(None),
            Err(error) => Err(error.into()),
        }
    }

    /// Atomically records a completed move boundary and its lifecycle state.
    pub fn checkpoint(&self, id: &str, checkpoint: u32, state: BackgroundJobState, updated_at: &str) -> Result<(), StoreError> {
        let current = self.job(id)?.ok_or_else(|| StoreError::Message("Background Job is unavailable".into()))?;
        if checkpoint > current.total || checkpoint < current.checkpoint
            || !current.state.can_transition_to(state) && current.state != state {
            return Err(StoreError::Message("invalid Background Job lifecycle transition".into()));
        }
        let changed = self.conn.execute("UPDATE background_jobs SET checkpoint = ?2, state = ?3, updated_at = ?4 WHERE id = ?1 AND checkpoint <= ?2", rusqlite::params![id, checkpoint, state.as_str(), updated_at])?;
        if changed == 0 { return Err(StoreError::Message("Background Job checkpoint was not accepted".into())); }
        Ok(())
    }

    /// Startup recovery: running work had no orderly shutdown and is never resumed implicitly.
    pub fn interrupt_inflight_jobs(&self, updated_at: &str) -> Result<(), StoreError> {
        self.conn.execute("UPDATE background_jobs SET state = 'interrupted', updated_at = ?1 WHERE state = 'running'", [updated_at])?;
        Ok(())
    }
}

fn row_to_record(row: &rusqlite::Row<'_>) -> Result<GameRecord, StoreError> {
    let kind_name: String = row.get(1)?;
    let kind = GameRecordKind::parse(&kind_name)
        .ok_or_else(|| StoreError::Message(format!("unknown Game Record kind: {kind_name}")))?;
    let payload_version: u32 = row.get(8)?;
    if payload_version != GameRecordPayload::VERSION {
        return Err(StoreError::Message(format!(
            "unsupported Game Record payload version {payload_version}"
        )));
    }
    let payload_text: String = row.get(9)?;
    let archived: i64 = row.get(5)?;
    Ok(GameRecord {
        id: row.get(0)?,
        kind,
        title: row.get(2)?,
        result_score: row.get(3)?,
        ply_count: row.get::<_, i64>(4)? as u32,
        archived: archived != 0,
        created_at: row.get(6)?,
        updated_at: row.get(7)?,
        payload: decode_payload(&payload_text)?,
    })
}

/// Encodes a payload as a compact JSON object. The on-disk schema is internal;
/// only the public store API is promised.
fn encode_payload(payload: &GameRecordPayload) -> Result<String, StoreError> {
    let mut out = String::from("{\"variant\":");
    push_json_string(&mut out, &payload.variant);
    out.push_str(",\"start_fen\":");
    push_json_string(&mut out, &payload.start_fen);
    out.push_str(",\"moves\":[");
    for (index, played) in payload.moves.iter().enumerate() {
        if index > 0 {
            out.push(',');
        }
        out.push_str("{\"uci\":");
        push_json_string(&mut out, &played.uci);
        out.push_str(",\"san\":");
        push_json_string(&mut out, &played.san);
        out.push_str(",\"number\":");
        out.push_str(&played.number.to_string());
        out.push_str(",\"side\":");
        push_json_string(&mut out, &played.side);
        out.push('}');
    }
    out.push_str("],\"result\":");
    match &payload.result {
        Some(result) => {
            out.push_str("{\"status\":");
            push_json_string(&mut out, &result.status);
            out.push_str(",\"termination\":");
            push_json_string(&mut out, &result.termination);
            out.push_str(",\"score\":");
            push_json_string(&mut out, &result.score);
            out.push('}');
        }
        None => out.push_str("null"),
    }
    out.push_str(",\"participation\":");
    match &payload.participation {
        Some(value) => push_json_string(&mut out, value),
        None => out.push_str("null"),
    }
    out.push_str(",\"clock\":");
    match &payload.clock {
        Some(value) => push_json_string(&mut out, value),
        None => out.push_str("null"),
    }
    out.push('}');
    Ok(out)
}

fn decode_payload(text: &str) -> Result<GameRecordPayload, StoreError> {
    let variant = required_string(text, "variant")?;
    let start_fen = required_string(text, "start_fen")?;
    let moves = decode_moves(text)?;
    let result = decode_optional_result(text)?;
    let participation = optional_string(text, "participation")?;
    let clock = optional_string(text, "clock")?;
    Ok(GameRecordPayload {
        variant,
        start_fen,
        moves,
        result,
        participation,
        clock,
    })
}

fn decode_moves(text: &str) -> Result<Vec<MoveEntry>, StoreError> {
    let Some(array) = extract_array(text, "moves") else {
        return Err(StoreError::Message("Game Record payload missing moves".into()));
    };
    if array.trim().is_empty() {
        return Ok(Vec::new());
    }
    let mut moves = Vec::new();
    for object in split_top_level_objects(array)? {
        moves.push(MoveEntry {
            uci: required_string(object, "uci")?,
            san: required_string(object, "san")?,
            number: required_string(object, "number")?
                .parse()
                .map_err(|_| StoreError::Message(format!("bad move number in {object}")))?,
            side: required_string(object, "side")?,
        });
    }
    Ok(moves)
}

fn decode_optional_result(text: &str) -> Result<Option<RecordResult>, StoreError> {
    if field_is_null(text, "result") {
        return Ok(None);
    }
    let Some(object) = extract_object(text, "result") else {
        return Ok(None);
    };
    Ok(Some(RecordResult {
        status: required_string(object, "status")?,
        termination: required_string(object, "termination")?,
        score: required_string(object, "score")?,
    }))
}

fn push_json_string(out: &mut String, value: &str) {
    out.push('"');
    for character in value.chars() {
        match character {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
}

fn json_error(error: serde_json::Error) -> StoreError {
    StoreError::Message(format!("unreadable Analysis Record content: {error}"))
}

fn required_string(input: &str, name: &str) -> Result<String, StoreError> {
    read_field(input, name)?
        .ok_or_else(|| StoreError::Message(format!("Game Record payload missing {name}")))
}

fn optional_string(input: &str, name: &str) -> Result<Option<String>, StoreError> {
    if field_is_null(input, name) {
        return Ok(None);
    }
    read_field(input, name)
}

fn field_is_null(input: &str, name: &str) -> bool {
    let needle = format!("\"{name}\"");
    let Some(start) = input.find(&needle) else {
        return true;
    };
    let rest = input[start + needle.len()..].trim_start();
    let Some(rest) = rest.strip_prefix(':') else {
        return false;
    };
    rest.trim_start().starts_with("null")
}

fn read_field(input: &str, name: &str) -> Result<Option<String>, StoreError> {
    let needle = format!("\"{name}\"");
    let Some(start) = input.find(&needle) else {
        return Ok(None);
    };
    let rest = input[start + needle.len()..].trim_start();
    let Some(rest) = rest.strip_prefix(':') else {
        return Err(StoreError::Message(format!("malformed field {name}")));
    };
    let rest = rest.trim_start();
    if rest.starts_with("null") {
        return Ok(None);
    }
    if rest.starts_with('"') {
        return Ok(Some(read_quoted(rest)?));
    }
    // Numbers are returned as their textual form.
    let end = rest
        .find(|c: char| c == ',' || c == '}' || c.is_whitespace())
        .unwrap_or(rest.len());
    Ok(Some(rest[..end].to_owned()))
}

fn read_quoted(input: &str) -> Result<String, StoreError> {
    let bytes = input.as_bytes();
    if bytes.first() != Some(&b'"') {
        return Err(StoreError::Message("expected a JSON string".into()));
    }
    let mut value = String::new();
    let mut index = 1;
    while index < bytes.len() {
        match bytes[index] {
            b'"' => return Ok(value),
            b'\\' => {
                index += 1;
                let escaped = *bytes
                    .get(index)
                    .ok_or_else(|| StoreError::Message("truncated escape".into()))?;
                match escaped {
                    b'"' => value.push('"'),
                    b'\\' => value.push('\\'),
                    b'n' => value.push('\n'),
                    b'r' => value.push('\r'),
                    b't' => value.push('\t'),
                    _ => {
                        return Err(StoreError::Message(format!(
                            "unsupported escape \\{}",
                            escaped as char
                        )))
                    }
                }
                index += 1;
            }
            _ => {
                let character = input[index..].chars().next().unwrap();
                value.push(character);
                index += character.len_utf8();
            }
        }
    }
    Err(StoreError::Message("truncated JSON string".into()))
}

fn extract_array<'a>(input: &'a str, name: &str) -> Option<&'a str> {
    let needle = format!("\"{name}\"");
    let start = input.find(&needle)?;
    let rest = input[start + needle.len()..].trim_start().strip_prefix(':')?.trim_start();
    if !rest.starts_with('[') {
        return None;
    }
    let mut depth = 0;
    for (offset, character) in rest.char_indices() {
        match character {
            '[' => depth += 1,
            ']' => {
                depth -= 1;
                if depth == 0 {
                    return Some(&rest[1..offset]);
                }
            }
            _ => {}
        }
    }
    None
}

fn extract_object<'a>(input: &'a str, name: &str) -> Option<&'a str> {
    let needle = format!("\"{name}\"");
    let start = input.find(&needle)?;
    let rest = input[start + needle.len()..].trim_start().strip_prefix(':')?.trim_start();
    if !rest.starts_with('{') {
        return None;
    }
    let mut depth = 0;
    for (offset, character) in rest.char_indices() {
        match character {
            '{' => depth += 1,
            '}' => {
                depth -= 1;
                if depth == 0 {
                    return Some(&rest[..=offset]);
                }
            }
            _ => {}
        }
    }
    None
}

fn split_top_level_objects(array_body: &str) -> Result<Vec<&str>, StoreError> {
    let mut objects = Vec::new();
    let mut depth = 0;
    let mut start = None;
    for (offset, character) in array_body.char_indices() {
        match character {
            '{' => {
                if depth == 0 {
                    start = Some(offset);
                }
                depth += 1;
            }
            '}' => {
                depth -= 1;
                if depth == 0 {
                    let from = start.ok_or_else(|| {
                        StoreError::Message("malformed moves array".into())
                    })?;
                    objects.push(&array_body[from..=offset]);
                    start = None;
                }
            }
            _ => {}
        }
    }
    if depth != 0 {
        return Err(StoreError::Message("truncated moves array".into()));
    }
    Ok(objects)
}

impl LiveStore {
    /// Opens the Live Store at `path`, creating it when absent.
    pub fn open(path: &Path) -> Result<Self, OpenError> {
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent).map_err(|source| OpenError::CreateDirectory {
                path: parent.to_path_buf(),
                source,
            })?;
        }

        let conn = Connection::open_with_flags(
            path,
            OpenFlags::SQLITE_OPEN_READ_WRITE | OpenFlags::SQLITE_OPEN_CREATE,
        )
        .map_err(|source| OpenError::OpenFile {
            path: path.to_path_buf(),
            source,
        })?;

        // Soft limits only before migration; durable journal mode is set after
        // a successful migration so a refused open leaves the prior file's
        // journal mode alone.
        conn.execute_batch("PRAGMA busy_timeout = 5000; PRAGMA foreign_keys = ON;")
            .map_err(|source| OpenError::Configure {
                path: path.to_path_buf(),
                source,
            })?;

        conn.execute_batch("BEGIN IMMEDIATE")
            .map_err(|source| OpenError::Migration {
                path: path.to_path_buf(),
                detail: format!("could not begin the launch migration: {source}"),
            })?;

        match migrate(&conn) {
            Ok(()) => {
                conn.execute_batch("COMMIT").map_err(|source| OpenError::Migration {
                    path: path.to_path_buf(),
                    detail: format!("could not commit the launch migration: {source}"),
                })?;
            }
            Err(detail) => {
                let _ = conn.execute_batch("ROLLBACK");
                return Err(OpenError::Migration {
                    path: path.to_path_buf(),
                    detail,
                });
            }
        }

        conn.execute_batch("PRAGMA journal_mode = WAL;")
            .map_err(|source| OpenError::Configure {
                path: path.to_path_buf(),
                source,
            })?;

        Ok(LiveStore {
            conn,
            path: path.to_path_buf(),
        })
    }

    /// The on-disk path of this store.
    pub fn path(&self) -> &Path {
        &self.path
    }

    /// The schema version recorded in the open store.
    pub fn schema_version(&self) -> Result<u32, StoreError> {
        let value: String = self.conn.query_row(
            "SELECT value FROM meta WHERE key = 'schema_version'",
            [],
            |row| row.get(0),
        )?;
        value
            .parse()
            .map_err(|_| StoreError::Message(format!("unreadable schema version: {value}")))
    }

    /// Workspace write partition: Game Records, residue, and other library tables.
    pub fn workspace(&self) -> WorkspaceWriter<'_> {
        WorkspaceWriter::new(&self.conn)
    }

    /// Worker write partition: Background Jobs (and later Analysis Record completion).
    pub fn worker(&self) -> WorkerWriter<'_> {
        WorkerWriter::new(&self.conn)
    }

    /// Opens the Live Store at the fixed XDG location.
    pub fn open_default() -> Result<Self, OpenError> {
        Self::open(&live_store_path())
    }
}

/// `$XDG_DATA_HOME/omachess/live-store.sqlite`, falling back to
/// `~/.local/share/omachess/live-store.sqlite`.
pub fn live_store_path() -> PathBuf {
    xdg_data_home().join("omachess").join("live-store.sqlite")
}

/// Paths a player should copy to back up Omachess work.
///
/// Returns `(must_copy, copy_if_preferences_matter)` directory paths.
pub fn backup_paths() -> (PathBuf, PathBuf) {
    (
        xdg_data_home().join("omachess"),
        xdg_config_home().join("omachess"),
    )
}

fn xdg_data_home() -> PathBuf {
    if let Ok(path) = std::env::var("XDG_DATA_HOME") {
        if !path.is_empty() {
            return PathBuf::from(path);
        }
    }
    home_dir().join(".local").join("share")
}

fn xdg_config_home() -> PathBuf {
    if let Ok(path) = std::env::var("XDG_CONFIG_HOME") {
        if !path.is_empty() {
            return PathBuf::from(path);
        }
    }
    home_dir().join(".config")
}

fn home_dir() -> PathBuf {
    std::env::var_os("HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("/"))
}

/// Runs the launch migration inside the caller's open transaction.
fn migrate(conn: &Connection) -> Result<(), String> {
    conn.execute_batch(
        "
        CREATE TABLE IF NOT EXISTS meta (
            key TEXT PRIMARY KEY NOT NULL,
            value TEXT NOT NULL
        );
        ",
    )
    .map_err(|error| format!("could not create meta table: {error}"))?;

    let existing = match conn.query_row(
        "SELECT value FROM meta WHERE key = 'schema_version'",
        [],
        |row| row.get::<_, String>(0),
    ) {
        Ok(value) => Some(value),
        Err(rusqlite::Error::QueryReturnedNoRows) => None,
        Err(error) => return Err(format!("could not read schema version: {error}")),
    };

    match existing {
        None => {
            create_schema_v1(conn)?;
            conn.execute(
                "INSERT INTO meta (key, value) VALUES ('schema_version', ?1)",
                [SCHEMA_VERSION.to_string()],
            )
            .map_err(|error| format!("could not record schema version: {error}"))?;
            Ok(())
        }
        Some(version) => {
            let parsed: u32 = version
                .parse()
                .map_err(|_| format!("unreadable schema version: {version}"))?;
            if parsed == SCHEMA_VERSION {
                Ok(())
            } else if parsed == 1 {
                create_analysis_schema(conn)?;
                create_studies_schema(conn)?;
                conn.execute(
                    "UPDATE meta SET value = ?1 WHERE key = 'schema_version'",
                    [SCHEMA_VERSION.to_string()],
                )
                .map_err(|error| format!("could not record schema version: {error}"))?;
                Ok(())
            } else if parsed == 2 {
                create_studies_schema(conn)?;
                conn.execute(
                    "UPDATE meta SET value = ?1 WHERE key = 'schema_version'",
                    [SCHEMA_VERSION.to_string()],
                )
                .map_err(|error| format!("could not record schema version: {error}"))?;
                Ok(())
            } else if parsed == 3 {
                migrate_background_jobs_v4(conn)?;
                conn.execute(
                    "UPDATE meta SET value = ?1 WHERE key = 'schema_version'",
                    [SCHEMA_VERSION.to_string()],
                )
                .map_err(|error| format!("could not record schema version: {error}"))?;
                Ok(())
            } else if parsed > SCHEMA_VERSION {
                Err(format!(
                    "Live Store schema version {parsed} is newer than this Omachess understands ({SCHEMA_VERSION})"
                ))
            } else {
                Err(format!(
                    "Live Store schema version {parsed} is older than this Omachess ({SCHEMA_VERSION}) and no migration path exists yet"
                ))
            }
        }
    }
}

fn create_schema_v1(conn: &Connection) -> Result<(), String> {
    conn.execute_batch(
        "
        CREATE TABLE game_records (
            id TEXT PRIMARY KEY NOT NULL,
            kind TEXT NOT NULL CHECK (kind IN ('played', 'analysis')),
            title TEXT,
            result_score TEXT,
            ply_count INTEGER NOT NULL DEFAULT 0,
            archived INTEGER NOT NULL DEFAULT 0 CHECK (archived IN (0, 1)),
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            payload_version INTEGER NOT NULL,
            payload TEXT NOT NULL
        );

        CREATE TABLE workspace_residue (
            key TEXT PRIMARY KEY NOT NULL,
            value TEXT NOT NULL
        );

        -- Worker write partition. The worker exclusively writes this table;
        -- WAL lets the workspace safely read it while it checkpoints.
        CREATE TABLE background_jobs (
            id TEXT PRIMARY KEY NOT NULL,
            kind TEXT NOT NULL DEFAULT 'computer_analysis',
            state TEXT NOT NULL,
            record_id TEXT NOT NULL DEFAULT '',
            checkpoint INTEGER NOT NULL DEFAULT 0,
            total INTEGER NOT NULL DEFAULT 0,
            controls TEXT NOT NULL DEFAULT '',
            updated_at TEXT NOT NULL,
            payload TEXT NOT NULL
        );
        ",
    )
    .map_err(|error| format!("could not create base schema tables: {error}"))?;
    create_analysis_schema(conn)?;
    create_studies_schema(conn)
}

fn migrate_background_jobs_v4(conn: &Connection) -> Result<(), String> {
    conn.execute_batch(
        "ALTER TABLE background_jobs ADD COLUMN kind TEXT NOT NULL DEFAULT 'computer_analysis';
         ALTER TABLE background_jobs ADD COLUMN record_id TEXT NOT NULL DEFAULT '';
         ALTER TABLE background_jobs ADD COLUMN checkpoint INTEGER NOT NULL DEFAULT 0;
         ALTER TABLE background_jobs ADD COLUMN total INTEGER NOT NULL DEFAULT 0;
         ALTER TABLE background_jobs ADD COLUMN controls TEXT NOT NULL DEFAULT '';",
    )
    .map_err(|error| format!("could not migrate Background Jobs: {error}"))
}

fn create_analysis_schema(conn: &Connection) -> Result<(), String> {
    conn.execute_batch(
        "
        CREATE TABLE analysis_records (
            record_id TEXT PRIMARY KEY NOT NULL,
            content TEXT NOT NULL
        );
        CREATE TABLE record_edges (
            source_id TEXT NOT NULL,
            derived_id TEXT NOT NULL,
            edge_type TEXT NOT NULL CHECK (edge_type IN ('derived_from')),
            created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
            PRIMARY KEY (source_id, derived_id, edge_type)
        );
        CREATE INDEX record_edges_by_derived
            ON record_edges (derived_id, edge_type);
        ",
    )
    .map_err(|error| format!("could not migrate Analysis Record tables: {error}"))
}

fn create_studies_schema(conn: &Connection) -> Result<(), String> {
    conn.execute_batch(
        "
        CREATE TABLE studies (
            id TEXT PRIMARY KEY NOT NULL,
            name TEXT NOT NULL,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        );
        CREATE TABLE study_records (
            study_id TEXT NOT NULL REFERENCES studies(id) ON DELETE CASCADE,
            record_id TEXT NOT NULL REFERENCES game_records(id) ON DELETE CASCADE,
            position INTEGER NOT NULL,
            PRIMARY KEY (study_id, record_id),
            UNIQUE (study_id, position)
        );
        CREATE INDEX study_records_by_record ON study_records(record_id);
        ",
    )
    .map_err(|error| format!("could not migrate Study tables: {error}"))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn completed_record(id: &str, title: &str) -> GameRecord {
        let mut payload = GameRecordPayload::empty_standard();
        payload.moves.push(MoveEntry {
            uci: "e2e4".into(),
            san: "e4".into(),
            number: 1,
            side: "white".into(),
        });
        payload.result = Some(RecordResult {
            status: "white".into(),
            termination: "checkmate".into(),
            score: "1-0".into(),
        });
        payload.participation = Some("white=Ada\nblack=Grace\nevent=Match".into());
        GameRecord {
            id: id.into(),
            kind: GameRecordKind::Played,
            title: Some(title.into()),
            result_score: Some("1-0".into()),
            ply_count: 1,
            archived: false,
            created_at: "2026-07-27T00:00:00Z".into(),
            updated_at: "2026-07-27T00:00:00Z".into(),
            payload,
        }
    }

    #[test]
    fn derivations_have_independent_content_snapshots_and_bidirectional_provenance() {
        let dir = tempfile::TempDir::new().unwrap();
        let store = LiveStore::open(&dir.path().join("live-store.sqlite")).unwrap();
        let source = completed_record("played-1", "Original");
        store.workspace().upsert_game_record(&source).unwrap();

        let first = store
            .workspace()
            .derive_analysis_record("played-1", "analysis-1", "2026-07-27T00:01:00Z")
            .unwrap();
        let second = store
            .workspace()
            .derive_analysis_record("played-1", "analysis-2", "2026-07-27T00:02:00Z")
            .unwrap();
        store
            .workspace()
            .add_annotation("analysis-1", 1, "Interesting")
            .unwrap();

        assert_eq!(first.source_snapshot.moves, source.payload.moves);
        assert_eq!(first.source_snapshot.metadata.as_deref(), source.payload.participation.as_deref());
        assert!(second.annotations.is_empty());
        assert_eq!(
            store.workspace().derivations_from("played-1").unwrap(),
            vec!["analysis-1".to_string(), "analysis-2".to_string()]
        );
        assert_eq!(
            store.workspace().sources_of("analysis-1").unwrap(),
            vec!["played-1".to_string()]
        );
    }

    #[test]
    fn studies_keep_order_many_to_many_and_only_accept_eligible_records() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");
        {
            let store = LiveStore::open(&path).unwrap();
            let writer = store.workspace();
            writer.upsert_game_record(&completed_record("completed", "Completed")).unwrap();
            writer.derive_analysis_record("completed", "analysis-1", "now").unwrap();
            writer.derive_analysis_record("completed", "analysis-2", "later").unwrap();
            let mut unfinished = completed_record("unfinished", "Unfinished");
            unfinished.payload.result = None;
            unfinished.result_score = None;
            writer.upsert_game_record(&unfinished).unwrap();
            writer.create_study("study-1", "Ideas", "now").unwrap();
            writer.create_study("study-2", "Openings", "now").unwrap();
            writer.add_study_record("study-1", "completed").unwrap();
            writer.add_study_record("study-1", "analysis-1").unwrap();
            writer.add_study_record("study-1", "analysis-2").unwrap();
            writer.add_study_record("study-2", "analysis-1").unwrap();
            assert!(writer.add_study_record("study-1", "unfinished").is_err());
            writer.reorder_study_record("study-1", "analysis-2", 0).unwrap();
            writer.remove_study_record("study-1", "analysis-1").unwrap();
            assert!(writer.get_game_record("analysis-1").unwrap().is_some());
        }
        let store = LiveStore::open(&path).unwrap();
        assert_eq!(
            store.workspace().study("study-1").unwrap().unwrap().record_ids,
            vec!["analysis-2", "completed"]
        );
        assert_eq!(
            store.workspace().study("study-2").unwrap().unwrap().record_ids,
            vec!["analysis-1"]
        );
    }

    #[test]
    fn source_snapshot_and_pinned_engine_line_survive_source_purge_and_restart() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");
        {
            let store = LiveStore::open(&path).unwrap();
            store
                .workspace()
                .upsert_game_record(&completed_record("played-1", "Original"))
                .unwrap();
            store
                .workspace()
                .derive_analysis_record("played-1", "analysis-1", "2026-07-27T00:01:00Z")
                .unwrap();
            store
                .workspace()
                .pin_engine_line(
                    "analysis-1",
                    &PinnedEngineLine {
                        position_fen: GameRecordPayload::STANDARD_START.into(),
                        evaluation: "+0.22".into(),
                        variation: "e2e4 e7e5".into(),
                        engine: "Stockfish 18".into(),
                        search_context: "depth 8 · movetime 250 ms".into(),
                    },
                )
                .unwrap();
            store.workspace().purge_game_record("played-1").unwrap();
        }

        let store = LiveStore::open(&path).unwrap();
        let analysis = store
            .workspace()
            .analysis_record("analysis-1")
            .unwrap()
            .unwrap();
        assert_eq!(analysis.source_snapshot.moves[0].uci, "e2e4");
        assert_eq!(analysis.pinned_lines[0].engine, "Stockfish 18");
        assert_eq!(analysis.pinned_lines[0].search_context, "depth 8 · movetime 250 ms");
    }

    #[test]
    fn opening_a_new_live_store_records_current_schema_version() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");
        let store = LiveStore::open(&path).expect("a new Live Store should open");
        assert_eq!(store.schema_version().unwrap(), SCHEMA_VERSION);
    }

    #[test]
    fn worker_jobs_checkpoint_at_move_boundaries_and_recover_as_interrupted() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");
        {
            let store = LiveStore::open(&path).unwrap();
            let worker = store.worker();
            worker.create_job(&BackgroundJob {
                id: "job-1".into(), kind: "computer_analysis".into(),
                state: BackgroundJobState::Running, record_id: "played-1".into(),
                checkpoint: 0, total: 5, controls: vec!["pause".into(), "cancel".into(), "open".into()],
                payload: "{}".into(), updated_at: "one".into(),
            }).unwrap();
            worker.checkpoint("job-1", 3, BackgroundJobState::Running, "two").unwrap();
            assert!(worker.checkpoint("job-1", 2, BackgroundJobState::Running, "bad").is_err());
        }
        let store = LiveStore::open(&path).unwrap();
        let worker = store.worker();
        worker.interrupt_inflight_jobs("recovered").unwrap();
        let job = worker.job("job-1").unwrap().unwrap();
        assert_eq!(job.checkpoint, 3);
        assert_eq!(job.state, BackgroundJobState::Interrupted);
        assert_eq!(job.controls, vec!["pause", "cancel", "open"]);
        assert!(worker.checkpoint("job-1", 3, BackgroundJobState::Running, "resume").is_ok());
        assert!(worker.checkpoint("job-1", 5, BackgroundJobState::Complete, "complete").is_ok());
        assert!(worker.checkpoint("job-1", 5, BackgroundJobState::Running, "bad").is_err());
        assert!(worker.checkpoint("job-1", 6, BackgroundJobState::Complete, "bad").is_err());
    }

    #[test]
    fn a_game_record_survives_closing_and_reopening_the_live_store() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");
        let id = {
            let store = LiveStore::open(&path).unwrap();
            let record = GameRecord {
                id: "gr_opening".into(),
                kind: GameRecordKind::Played,
                title: None,
                result_score: None,
                ply_count: 1,
                archived: false,
                created_at: "2026-07-27T00:00:00Z".into(),
                updated_at: "2026-07-27T00:00:00Z".into(),
                payload: GameRecordPayload {
                    variant: "standard".into(),
                    start_fen: GameRecordPayload::STANDARD_START.to_owned(),
                    moves: vec![MoveEntry {
                        uci: "e2e4".into(),
                        san: "e4".into(),
                        number: 1,
                        side: "white".into(),
                    }],
                    result: None,
                    participation: None,
                    clock: None,
                },
            };
            store.workspace().upsert_game_record(&record).unwrap();
            store.workspace().set_residue("active_record_id", &record.id).unwrap();
            record.id
        };

        let store = LiveStore::open(&path).unwrap();
        let loaded = store
            .workspace()
            .get_game_record(&id)
            .unwrap()
            .expect("the Game Record is still in the Live Store");
        assert_eq!(loaded.kind, GameRecordKind::Played);
        assert_eq!(loaded.ply_count, 1);
        assert_eq!(loaded.payload.moves.len(), 1);
        assert_eq!(loaded.payload.moves[0].san, "e4");
        assert!(loaded.payload.result.is_none());
        assert!(loaded.payload.participation.is_none());
        assert!(loaded.payload.clock.is_none());
        assert_eq!(
            store.workspace().residue("active_record_id").unwrap().as_deref(),
            Some("gr_opening")
        );
    }

    #[test]
    fn a_newer_schema_refuses_to_open_and_leaves_the_store_untouched() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");
        {
            let store = LiveStore::open(&path).unwrap();
            store
                .workspace()
                .upsert_game_record(&GameRecord {
                    id: "keep_me".into(),
                    kind: GameRecordKind::Played,
                    title: None,
                    result_score: None,
                    ply_count: 0,
                    archived: false,
                    created_at: "2026-07-27T00:00:00Z".into(),
                    updated_at: "2026-07-27T00:00:00Z".into(),
                    payload: GameRecordPayload::empty_standard(),
                })
                .unwrap();
        }
        // Simulate a future Omachess writing a schema this build cannot open.
        {
            let conn = Connection::open(&path).unwrap();
            conn.execute(
                "UPDATE meta SET value = '99' WHERE key = 'schema_version'",
                [],
            )
            .unwrap();
        }

        let error = match LiveStore::open(&path) {
            Ok(_) => panic!("newer schema must refuse to open"),
            Err(error) => error,
        };
        let message = error.to_string();
        assert!(
            message.contains("99"),
            "failure must name the unsupported version: {message}"
        );
        assert!(
            message.contains("untouched"),
            "failure must say the previous store was left untouched: {message}"
        );

        // Prior data remains — the failed launch migration did not rewrite it.
        let conn = Connection::open(&path).unwrap();
        let version: String = conn
            .query_row(
                "SELECT value FROM meta WHERE key = 'schema_version'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(version, "99");
        let count: i64 = conn
            .query_row(
                "SELECT COUNT(*) FROM game_records WHERE id = 'keep_me'",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(count, 1);
    }

    #[test]
    fn workspace_and_worker_partitions_share_the_store_without_redesign() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");
        let store = LiveStore::open(&path).unwrap();
        store.worker().ensure_ready().unwrap();
        assert!(store.workspace().list_game_records().unwrap().is_empty());
    }

    #[test]
    fn the_default_live_store_path_is_under_xdg_data_home() {
        let dir = tempfile::TempDir::new().unwrap();
        // Safety: this test only needs the resolved path, not concurrent env use.
        std::env::set_var("XDG_DATA_HOME", dir.path());
        let path = live_store_path();
        assert_eq!(path, dir.path().join("omachess").join("live-store.sqlite"));
        std::env::remove_var("XDG_DATA_HOME");
    }
}
