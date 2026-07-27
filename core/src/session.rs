//! The workspace session: the core-owned state a workspace window shows.
//!
//! A session accepts commands describing player intent and answers with
//! events describing the new state. It never calls back into the workspace;
//! the workspace drains the event queue when it is ready to apply changes.
//!
//! Every chess answer in an event comes from the Played Game, which gets it
//! from the Rules Authority. The session decides nothing about chess: it
//! decides what a workspace needs to be told.
//!
//! When opened against a Live Store, every successful change advances the
//! Game Record's Saved Snapshot, and closing the session records workspace
//! residue so a later session can offer restore.

use std::collections::BTreeMap;
use std::io::Write;
use std::path::Path;
use std::process::{Command, Stdio};
use std::thread;
use std::time::Instant;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use omachess_store::{
    AnalysisRecordData, AnalysisSideline, ComputerEvaluation, GameRecord, GameRecordKind,
    GameRecordPayload, GameRecordSummary, LiveStore, MoveEntry, OpenError, PinnedEngineLine,
    RecordResult,
};

use crate::board::{Orientation, Piece, Position};
use crate::game::{result_label, Destination, Game, MoveRejected, PlayedMove, Side};
use crate::json;
use crate::pgn::{self, ImportEntry, ImportReport, PgnGame};
use crate::rules::{parse_uci, Rules, Winner};

pub struct Session {
    game: Game,
    orientation: Orientation,
    events: Vec<String>,
    store: Option<LiveStore>,
    /// The Game Record currently being played, when the session has a store.
    record_id: Option<String>,
    /// Open workspace tabs, in open order — each names a Game Record id.
    open_tabs: Vec<String>,
    /// A prior Game Record the player may restore, when residue points at one.
    restore_offer: Option<RestoreOffer>,
    clock: Option<GameClock>,
    /// Suspended Games are fully loaded but inert until the player resumes.
    suspended: bool,
    metadata: GameMetadata,
    setup: Option<PositionSetup>,
    save_mode: SaveMode,
    dirty: bool,
    workshop: Option<VariantDefinition>,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum SaveMode {
    Autosave,
    Manual,
}

impl SaveMode {
    fn name(self) -> &'static str {
        match self {
            Self::Autosave => "autosave",
            Self::Manual => "manual",
        }
    }
}

#[derive(Clone, Debug)]
struct VariantDefinition {
    preset: String,
    files: u8,
    ranks: u8,
    pieces: String,
    custom_name: String,
    custom_letter: String,
    custom_betza: String,
    placement: BTreeMap<String, String>,
    promotion: bool,
    castling: bool,
    double_step: bool,
    extinction: bool,
    goal: bool,
    mandatory_capture: bool,
    drops: bool,
    error: String,
    playable: bool,
    validation_message: String,
    step: u8,
}

impl Default for VariantDefinition {
    fn default() -> Self {
        Self {
            preset: "standard-8x8".into(),
            files: 8,
            ranks: 8,
            pieces: "KQRBNP".into(),
            custom_name: String::new(),
            custom_letter: String::new(),
            custom_betza: String::new(),
            placement: BTreeMap::new(),
            promotion: true,
            castling: true,
            double_step: true,
            extinction: false,
            goal: false,
            mandatory_capture: false,
            drops: false,
            error: String::new(),
            playable: false,
            validation_message: String::new(),
            step: 1,
        }
    }
}

#[derive(Clone, Default, Debug)]
struct GameMetadata {
    white: String,
    black: String,
    event: String,
    date: String,
    title: String,
    tags: String,
}

#[derive(Clone, Debug)]
struct GameClock {
    initial_ms: u64,
    white_ms: u64,
    black_ms: u64,
    history: Vec<(u64, u64)>,
    last_tick: Option<Instant>,
}

impl GameClock {
    fn new(initial_ms: u64) -> Self {
        Self {
            initial_ms,
            white_ms: initial_ms,
            black_ms: initial_ms,
            history: Vec::new(),
            last_tick: None,
        }
    }
}

struct PositionSetup {
    position: Position,
    fen: String,
    fen_suffix: String,
    rule_valid: bool,
    error: String,
}

#[derive(Clone, Debug)]
struct RestoreOffer {
    record_id: String,
    ply_count: u32,
}

/// Why a command was rejected. Values are part of the C ABI contract.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum CommandError {
    UnknownCommand = 1,
    MalformedCommand = 2,
    /// The player's intent was understood but the game cannot honour it — an
    /// illegal move, or a move in a game that is over or being reviewed.
    RejectedMove = 5,
    /// The Live Store could not honour a durable operation.
    Store = 6,
}

/// Why a session could not be opened against the Live Store.
pub type SessionOpenError = OpenError;

impl Session {
    /// An ephemeral session with no Live Store — used by unit tests that only
    /// exercise in-memory play.
    pub fn new() -> Self {
        Session {
            game: Game::standard(),
            orientation: Orientation::WhiteBottom,
            events: Vec::new(),
            store: None,
            record_id: None,
            open_tabs: Vec::new(),
            restore_offer: None,
            clock: None,
            suspended: false,
            metadata: GameMetadata::default(),
            setup: None,
            save_mode: SaveMode::Autosave,
            dirty: false,
            workshop: None,
        }
    }

    /// Opens a session against the Live Store at the fixed XDG location.
    pub fn open_default() -> Result<Self, SessionOpenError> {
        Self::open_store(LiveStore::open_default()?)
    }

    /// Opens a session against the Live Store at `path`.
    pub fn open(path: &Path) -> Result<Self, SessionOpenError> {
        Self::open_store(LiveStore::open(path)?)
    }

    fn open_store(store: LiveStore) -> Result<Self, SessionOpenError> {
        let save_mode = match store.workspace().residue("save_mode") {
            Ok(Some(mode)) if mode == "manual" => SaveMode::Manual,
            _ => SaveMode::Autosave,
        };
        let open_tabs = match store.workspace().residue("open_tab_ids") {
            Ok(Some(encoded)) => decode_tab_ids(&encoded),
            _ => Vec::new(),
        };
        let active_id = store
            .workspace()
            .residue("active_record_id")
            .ok()
            .flatten()
            .filter(|id| open_tabs.iter().any(|tab| tab == id));
        let restore_offer = match store.workspace().residue("active_record_id") {
            Ok(Some(record_id)) => match store.workspace().get_game_record(&record_id) {
                Ok(Some(record))
                    if record.kind == GameRecordKind::Played
                        && record.ply_count > 0
                        && record.payload.result.is_none() =>
                {
                    Some(RestoreOffer {
                        record_id,
                        ply_count: record.ply_count,
                    })
                }
                _ => None,
            },
            _ => None,
        };

        let workshop = store
            .workspace()
            .residue("variant_definition_draft")
            .ok()
            .flatten()
            .and_then(|value| decode_variant_definition(&value));
        let mut session = Session {
            game: Game::standard(),
            orientation: Orientation::WhiteBottom,
            events: Vec::new(),
            store: Some(store),
            record_id: None,
            open_tabs,
            restore_offer,
            clock: None,
            suspended: false,
            metadata: GameMetadata::default(),
            setup: None,
            save_mode,
            dirty: false,
            workshop,
        };
        // Completed records may reopen directly. Unfinished Played Games are
        // offered for restore and remain unloaded until the player chooses it.
        if session.restore_offer.is_none() {
            if let Some(id) = active_id {
                let _ = session.load_record(&id);
            }
        }
        Ok(session)
    }

    /// Applies one command given as JSON, queueing the events it produces.
    pub fn submit(&mut self, command: &str) -> Result<(), CommandError> {
        let Some(kind) = json::read_string_field(command, "type") else {
            return Err(CommandError::MalformedCommand);
        };
        match kind.as_str() {
            // Asks for the current state without changing it, so a workspace
            // that attaches (or reattaches) starts from core-owned truth.
            "describe_board" => {}
            "flip_board" => self.orientation = self.orientation.flipped(),
            "play_move" => self.play_move(command)?,
            "navigate" => self.navigate(command)?,
            "restore_record" => self.restore_record()?,
            "suspend_game" => self.suspend_game()?,
            "resume_game" => self.resume_game()?,
            "dismiss_restore" => self.dismiss_restore()?,
            "new_game" => self.new_game()?,
            "open_record" => self.open_record(command)?,
            "close_tab" => self.close_tab(command)?,
            "configure_clock" => self.configure_clock(command)?,
            "tick_clock" => self.tick_clock()?,
            "update_metadata" => self.update_metadata(command)?,
            "begin_position_setup" => self.begin_position_setup(),
            "set_setup_fen" => self.set_setup_fen(command)?,
            "place_setup_piece" => self.place_setup_piece(command)?,
            "relocate_setup_piece" => self.relocate_setup_piece(command)?,
            "start_setup_game" => self.start_setup_game()?,
            "set_save_mode" => self.set_save_mode(command)?,
            "save_record" => self.save_record()?,
            "discard_changes" => self.discard_changes()?,
            "new_variant_definition" => self.new_variant_definition()?,
            "select_board_preset" => self.select_board_preset(command)?,
            "set_workshop_step" => self.set_workshop_step(command)?,
            "toggle_builtin_piece" => self.toggle_builtin_piece(command)?,
            "set_custom_piece" => self.set_custom_piece(command)?,
            "place_workshop_piece" => self.place_workshop_piece(command)?,
            "toggle_variant_rule" => self.toggle_variant_rule(command)?,
            "validate_variant_definition" => self.validate_variant_definition()?,
            "import_pgn" => self.import_pgn(command)?,
            "export_pgn" => self.export_pgn(command)?,
            "derive_analysis_record" => self.derive_analysis_record()?,
            "complete_computer_analysis" => self.complete_computer_analysis(command)?,
            "designate_default_analysis" => self.designate_default_analysis()?,
            "add_analysis_annotation" => self.add_analysis_annotation(command)?,
            "add_analysis_sideline" => self.add_analysis_sideline(command)?,
            "pin_engine_line" => self.pin_engine_line(command)?,
            _ => return Err(CommandError::UnknownCommand),
        }
        if matches!(
            kind.as_str(),
            "select_board_preset"
                | "toggle_builtin_piece"
                | "set_custom_piece"
                | "place_workshop_piece"
                | "toggle_variant_rule"
        ) {
            if let Some(definition) = self.workshop.as_mut() {
                definition.playable = false;
                definition.validation_message.clear();
            }
            self.persist_variant_definition()?;
        }
        let event = self.board_changed_event();
        self.events.push(event);
        if kind == "describe_board" {
            if self.store.is_some() {
                self.emit_library_changed();
                self.emit_tabs_changed();
            }
            if let Some(offer) = &self.restore_offer {
                self.events.push(restore_available_event(offer));
            }
            self.emit_record_graph_changed();
            self.emit_analysis_record_changed();
        }
        if self.workshop.is_some() {
            self.events.push(self.workshop_changed_event());
            let playable = self
                .workshop
                .as_ref()
                .is_some_and(|definition| definition.playable);
            self.events.push(format!(
                "{{\"type\":\"variant_library_changed\",\"id\":\"variant-draft\",\"kind\":\"variant\",\"title\":\"Untitled Variant\",\"playable\":{playable}}}"
            ));
        }
        Ok(())
    }

    fn derive_analysis_record(&mut self) -> Result<(), CommandError> {
        if self.has_unsaved_changes() {
            return Err(CommandError::RejectedMove);
        }
        let source_id = self.record_id.clone().ok_or(CommandError::RejectedMove)?;
        let store = self.store.as_ref().ok_or(CommandError::Store)?;
        let id = new_record_id();
        store
            .workspace()
            .derive_analysis_record(&source_id, &id, &timestamp_now())
            .map_err(|_| CommandError::RejectedMove)?;
        self.load_record(&id)?;
        self.suspended = false;
        self.ensure_tab_open(&id);
        self.persist_residue()?;
        self.emit_library_changed();
        self.emit_tabs_changed();
        self.emit_record_graph_changed();
        self.emit_analysis_record_changed();
        Ok(())
    }

    fn complete_computer_analysis(&mut self, command: &str) -> Result<(), CommandError> {
        if self.has_unsaved_changes() {
            return Err(CommandError::RejectedMove);
        }
        let encoded =
            json::read_string_field(command, "evaluations").ok_or(CommandError::MalformedCommand)?;
        let mut evaluations: Vec<ComputerEvaluation> =
            serde_json::from_str(&encoded).map_err(|_| CommandError::MalformedCommand)?;
        if evaluations.len() != self.game.moves().len() + 1
            || evaluations
                .iter()
                .enumerate()
                .any(|(ply, value)| value.ply as usize != ply)
        {
            return Err(CommandError::MalformedCommand);
        }
        for (ply, evaluation) in evaluations.iter_mut().enumerate() {
            let Some(played) = self.game.moves().get(ply) else {
                evaluation.glyph.clear();
                evaluation.better_line = None;
                continue;
            };
            let engine_move = evaluation
                .better_line
                .as_deref()
                .and_then(|line| line.split_whitespace().next());
            evaluation.glyph = if engine_move == Some(played.uci.as_str()) {
                "!".into()
            } else {
                "?".into()
            };
            if engine_move == Some(played.uci.as_str()) {
                evaluation.better_line = None;
            }
        }
        let better_lines = evaluations.clone();
        let source_id = self.record_id.clone().ok_or(CommandError::RejectedMove)?;
        let store = self.store.as_ref().ok_or(CommandError::Store)?;
        let id = new_record_id();
        store
            .workspace()
            .derive_analysis_record(&source_id, &id, &timestamp_now())
            .map_err(|_| CommandError::Store)?;
        store
            .workspace()
            .complete_computer_analysis(&id, evaluations, true)
            .map_err(|_| CommandError::Store)?;
        for evaluation in better_lines {
            let Some(variation) = evaluation.better_line else {
                continue;
            };
            let Some(mut line_game) = Game::from_history(
                &self.game.start_fen(),
                self.game.moves().iter().take(evaluation.ply as usize).cloned().collect(),
            ) else {
                continue;
            };
            let base = line_game.moves().len();
            let mut valid = true;
            for uci in variation.split_whitespace() {
                let Some(parsed) = parse_uci(uci) else {
                    valid = false;
                    break;
                };
                if line_game
                    .play(&parsed.from, &parsed.to, parsed.promotion.as_deref())
                    .is_err()
                {
                    valid = false;
                    break;
                }
            }
            if valid {
                let moves = line_game.moves()[base..]
                    .iter()
                    .map(|played| MoveEntry {
                        uci: played.uci.clone(),
                        san: played.san.clone(),
                        number: played.number,
                        side: played.side.to_owned(),
                    })
                    .collect();
                store
                    .workspace()
                    .add_sideline(
                        &id,
                        AnalysisSideline {
                            after_ply: evaluation.ply,
                            moves,
                        },
                    )
                    .map_err(|_| CommandError::Store)?;
            }
        }
        self.load_record(&id)?;
        self.suspended = false;
        self.ensure_tab_open(&id);
        self.persist_residue()?;
        self.emit_library_changed();
        self.emit_tabs_changed();
        self.emit_record_graph_changed();
        self.emit_analysis_record_changed();
        Ok(())
    }

    fn designate_default_analysis(&mut self) -> Result<(), CommandError> {
        let id = self.analysis_record_id()?;
        self.store
            .as_ref()
            .ok_or(CommandError::Store)?
            .workspace()
            .designate_default_analysis(&id)
            .map_err(|_| CommandError::Store)?;
        self.emit_analysis_record_changed();
        Ok(())
    }

    fn add_analysis_annotation(&mut self, command: &str) -> Result<(), CommandError> {
        let id = self.analysis_record_id()?;
        let ply = json::read_string_field(command, "ply")
            .and_then(|value| value.parse().ok())
            .ok_or(CommandError::MalformedCommand)?;
        let text =
            json::read_string_field(command, "text").ok_or(CommandError::MalformedCommand)?;
        self.store
            .as_ref()
            .ok_or(CommandError::Store)?
            .workspace()
            .add_annotation(&id, ply, &text)
            .map_err(|_| CommandError::Store)?;
        self.emit_analysis_record_changed();
        Ok(())
    }

    fn add_analysis_sideline(&mut self, command: &str) -> Result<(), CommandError> {
        let id = self.analysis_record_id()?;
        let after_ply = json::read_string_field(command, "after_ply")
            .and_then(|value| value.parse().ok())
            .ok_or(CommandError::MalformedCommand)?;
        let variation =
            json::read_string_field(command, "variation").ok_or(CommandError::MalformedCommand)?;
        let workspace = self.store.as_ref().ok_or(CommandError::Store)?.workspace();
        let analysis = workspace
            .analysis_record(&id)
            .map_err(|_| CommandError::Store)?
            .ok_or(CommandError::Store)?;
        let prefix: Vec<_> = analysis
            .main_line
            .iter()
            .take(after_ply as usize)
            .map(|entry| PlayedMove {
                uci: entry.uci.clone(),
                san: entry.san.clone(),
                number: entry.number,
                side: if entry.side == "black" { "black" } else { "white" },
            })
            .collect();
        if prefix.len() != after_ply as usize {
            return Err(CommandError::MalformedCommand);
        }
        let mut sideline_game =
            Game::from_history(&analysis.source_snapshot.start_fen, prefix)
                .ok_or(CommandError::Store)?;
        let base = sideline_game.moves().len();
        for uci in variation.split_whitespace() {
            let parsed = parse_uci(uci).ok_or(CommandError::MalformedCommand)?;
            sideline_game
                .play(&parsed.from, &parsed.to, parsed.promotion.as_deref())
                .map_err(|_| CommandError::RejectedMove)?;
        }
        let moves = sideline_game.moves()[base..]
            .iter()
            .map(|played| MoveEntry {
                uci: played.uci.clone(),
                san: played.san.clone(),
                number: played.number,
                side: played.side.to_owned(),
            })
            .collect();
        workspace
            .add_sideline(&id, AnalysisSideline { after_ply, moves })
            .map_err(|_| CommandError::Store)?;
        self.emit_analysis_record_changed();
        Ok(())
    }

    fn pin_engine_line(&mut self, command: &str) -> Result<(), CommandError> {
        let id = self.analysis_record_id()?;
        let required = |name| {
            json::read_string_field(command, name).ok_or(CommandError::MalformedCommand)
        };
        let line = PinnedEngineLine {
            position_fen: required("position_fen")?,
            evaluation: required("evaluation")?,
            variation: required("variation")?,
            engine: required("engine")?,
            search_context: required("search_context")?,
        };
        self.store
            .as_ref()
            .ok_or(CommandError::Store)?
            .workspace()
            .pin_engine_line(&id, &line)
            .map_err(|_| CommandError::Store)?;
        self.emit_analysis_record_changed();
        Ok(())
    }

    fn analysis_record_id(&self) -> Result<String, CommandError> {
        let id = self.record_id.clone().ok_or(CommandError::RejectedMove)?;
        let record = self
            .store
            .as_ref()
            .ok_or(CommandError::Store)?
            .workspace()
            .get_game_record(&id)
            .map_err(|_| CommandError::Store)?
            .ok_or(CommandError::Store)?;
        (record.kind == GameRecordKind::Analysis)
            .then_some(id)
            .ok_or(CommandError::RejectedMove)
    }

    fn emit_analysis_record_changed(&mut self) {
        let Ok(id) = self.analysis_record_id() else {
            return;
        };
        let Some(store) = self.store.as_ref() else {
            return;
        };
        let Ok(Some(data)) = store.workspace().analysis_record(&id) else {
            return;
        };
        let sources = store.workspace().sources_of(&id).unwrap_or_default();
        let derivations = store.workspace().derivations_from(&id).unwrap_or_default();
        self.events
            .push(analysis_record_changed_event(&data, &sources, &derivations));
    }

    fn emit_record_graph_changed(&mut self) {
        let Some(id) = self.record_id.as_ref() else {
            return;
        };
        let Some(store) = self.store.as_ref() else {
            return;
        };
        let sources = store.workspace().sources_of(id).unwrap_or_default();
        let derivations = store.workspace().derivations_from(id).unwrap_or_default();
        self.events
            .push(record_graph_changed_event(&sources, &derivations));
    }

    fn persist_variant_definition(&self) -> Result<(), CommandError> {
        let (Some(store), Some(definition)) = (&self.store, &self.workshop) else {
            return Ok(());
        };
        store
            .workspace()
            .set_residue(
                "variant_definition_draft",
                &encode_variant_definition(definition),
            )
            .map_err(|_| CommandError::Store)
    }

    fn new_variant_definition(&mut self) -> Result<(), CommandError> {
        self.workshop = Some(VariantDefinition::default());
        self.persist_variant_definition()
    }

    fn select_board_preset(&mut self, command: &str) -> Result<(), CommandError> {
        let id = json::read_string_field(command, "id").ok_or(CommandError::MalformedCommand)?;
        let (files, ranks) = preset_geometry(&id).ok_or(CommandError::MalformedCommand)?;
        let (max_files, max_ranks) = engine_geometry();
        if files > max_files || ranks > max_ranks {
            return Err(CommandError::RejectedMove);
        }
        let definition = self
            .workshop
            .as_mut()
            .ok_or(CommandError::MalformedCommand)?;
        definition.preset = id;
        definition.files = files;
        definition.ranks = ranks;
        definition.placement.clear();
        self.persist_variant_definition()
    }

    fn set_workshop_step(&mut self, command: &str) -> Result<(), CommandError> {
        let step = json::read_string_field(command, "step")
            .and_then(|value| value.parse::<u8>().ok())
            .ok_or(CommandError::MalformedCommand)?;
        self.workshop
            .as_mut()
            .ok_or(CommandError::MalformedCommand)?
            .step = step.clamp(1, 4);
        Ok(())
    }

    fn place_workshop_piece(&mut self, command: &str) -> Result<(), CommandError> {
        let square =
            json::read_string_field(command, "square").ok_or(CommandError::MalformedCommand)?;
        let piece =
            json::read_string_field(command, "piece").ok_or(CommandError::MalformedCommand)?;
        let definition = self
            .workshop
            .as_mut()
            .ok_or(CommandError::MalformedCommand)?;
        if !square_in_geometry(&square, definition.files, definition.ranks) {
            return Err(CommandError::MalformedCommand);
        }
        if piece.is_empty() {
            definition.placement.remove(&square);
        } else if workshop_piece_id(definition, &piece).is_some() {
            definition.placement.insert(square, piece);
        } else {
            return Err(CommandError::MalformedCommand);
        }
        self.persist_variant_definition()
    }

    fn toggle_variant_rule(&mut self, command: &str) -> Result<(), CommandError> {
        let rule =
            json::read_string_field(command, "rule").ok_or(CommandError::MalformedCommand)?;
        let definition = self
            .workshop
            .as_mut()
            .ok_or(CommandError::MalformedCommand)?;
        let selected = match rule.as_str() {
            "promotion" => &mut definition.promotion,
            "castling" => &mut definition.castling,
            "doubleStep" => &mut definition.double_step,
            "extinction" => &mut definition.extinction,
            "goal" => &mut definition.goal,
            "mandatoryCapture" => &mut definition.mandatory_capture,
            "drops" => &mut definition.drops,
            _ => return Err(CommandError::MalformedCommand),
        };
        *selected = !*selected;
        self.persist_variant_definition()
    }

    fn toggle_builtin_piece(&mut self, command: &str) -> Result<(), CommandError> {
        let code =
            json::read_string_field(command, "code").ok_or(CommandError::MalformedCommand)?;
        if matches!(code.as_str(), "K" | "P") {
            return Ok(());
        }
        let definition = self
            .workshop
            .as_mut()
            .ok_or(CommandError::MalformedCommand)?;
        if definition.pieces.contains(&code) {
            definition.pieces = definition.pieces.replace(&code, "");
        } else {
            definition.pieces.push_str(&code);
        }
        self.persist_variant_definition()
    }

    fn set_custom_piece(&mut self, command: &str) -> Result<(), CommandError> {
        let name = json::read_string_field(command, "name").unwrap_or_default();
        let letter = json::read_string_field(command, "letter").unwrap_or_default();
        let betza = json::read_string_field(command, "betza").unwrap_or_default();
        let definition = self
            .workshop
            .as_mut()
            .ok_or(CommandError::MalformedCommand)?;
        definition.error.clear();
        if let Some(offending) = betza.chars().find(|c| {
            !(if c.is_ascii_uppercase() {
                "WFDNACZGBRQKHX".contains(*c)
            } else {
                "fblrmcipgshe0123456789".contains(*c)
            })
        }) {
            definition.error = format!("Unsupported Betza atom: {offending}");
            return Ok(());
        }
        if name.is_empty()
            || letter.len() != 1
            || betza.is_empty()
            || !betza.chars().any(|c| c.is_ascii_uppercase())
        {
            definition.error =
                "Custom piece needs a name, one letter, and a Betza movement atom.".into();
            return Ok(());
        }
        let letter = letter.to_ascii_uppercase();
        if definition.pieces.contains(&letter) {
            definition.error = format!("Piece letter {letter} is already in use.");
            return Ok(());
        }
        definition.custom_name = name;
        definition.custom_letter = letter;
        definition.custom_betza = betza;
        self.persist_variant_definition()
    }

    fn validate_variant_definition(&mut self) -> Result<(), CommandError> {
        let definition = self
            .workshop
            .as_mut()
            .ok_or(CommandError::MalformedCommand)?;
        definition.playable = false;
        let failure = if !definition.error.is_empty() {
            let message = format!("Pieces step — {}", definition.error);
            definition.error.clear();
            Some(message)
        } else if definition.extinction {
            Some("Rules step — Royal checkmate and Extinction both decide how the game ends. Choose one win condition.".into())
        } else {
            let adapter = compile_variant_adapter(definition);
            if adapter != compile_variant_adapter(definition) {
                Some(
                    "Rules step — the Variant Definition did not compile deterministically.".into(),
                )
            } else {
                let fen = draft_variant_fen(definition);
                let (max_files, max_ranks) = engine_geometry();
                if definition.files > max_files || definition.ranks > max_ranks {
                    Some(format!("Board step — the detected Fairy-Stockfish build supports boards up to {max_files}×{max_ranks}."))
                } else {
                    let payload = format!(
                        "{}\n--OMACHESS-FEN--\n{fen}",
                        String::from_utf8_lossy(&adapter)
                    );
                    match run_isolated_validation("consistency", &payload) {
                    Err(IsolatedFailure::Deadline) => Some("Validate step — Fairy-Stockfish consistency check exceeded its deadline.".into()),
                    Err(IsolatedFailure::Rejected) => Some("Rules step — Fairy-Stockfish could not consistently load these rules.".into()),
                    Ok(()) => {
                        match run_isolated_validation("smoke", &payload) {
                                Err(IsolatedFailure::Deadline) => Some("Validate step — the bounded engine smoke test exceeded its deadline.".into()),
                                Err(IsolatedFailure::Rejected) => Some("Starting position step — make the position Rule-valid so the engine can load it, generate legal moves, and complete a bounded search.".into()),
                                Ok(()) => None,
                        }
                    }
                  }
                }
            }
        };
        match failure {
            Some(message) => {
                definition.step = if message.starts_with("Board step") {
                    1
                } else if message.starts_with("Pieces step") {
                    2
                } else if message.starts_with("Starting position step") {
                    3
                } else {
                    4
                };
                definition.validation_message = message;
            }
            None => {
                definition.playable = true;
                definition.validation_message = "Playable — every validation stage passed.".into();
            }
        }
        self.persist_variant_definition()
    }

    fn import_pgn(&mut self, command: &str) -> Result<(), CommandError> {
        let text = json::read_string_field(command, "pgn").ok_or(CommandError::MalformedCommand)?;
        let Some(store) = self.store.as_ref() else {
            return Err(CommandError::Store);
        };
        let mut results = Vec::new();
        for (index, entry) in pgn::import(&text).into_iter().enumerate() {
            match entry {
                ImportEntry::Imported(imported) => {
                    let now = timestamp_now();
                    let id = new_record_id();
                    let title = pgn::tag_value_from(&imported.tags, "Event");
                    let metadata = GameMetadata {
                        white: pgn::tag_value_from(&imported.tags, "White"),
                        black: pgn::tag_value_from(&imported.tags, "Black"),
                        event: title.clone(),
                        date: pgn::tag_value_from(&imported.tags, "Date"),
                        title: title.clone(),
                        tags: pgn::encode_tags(&imported.tags),
                    };
                    let result = Game::from_history(&imported.start_fen, imported.moves.clone())
                        .and_then(|game| {
                            let outcome = game.outcome();
                            (outcome.is_over() && outcome.winner.score() == imported.result).then(
                                || RecordResult {
                                    status: status_name(outcome.winner).to_owned(),
                                    termination: outcome.termination.name().to_owned(),
                                    score: outcome.winner.score().to_owned(),
                                },
                            )
                        });
                    let record = GameRecord {
                        id: id.clone(),
                        kind: GameRecordKind::Played,
                        title: (!title.is_empty()).then_some(title.clone()),
                        result_score: result.as_ref().map(|value| value.score.clone()),
                        ply_count: imported.moves.len() as u32,
                        archived: false,
                        created_at: now.clone(),
                        updated_at: now,
                        payload: GameRecordPayload {
                            variant: "standard".into(),
                            start_fen: imported.start_fen,
                            moves: imported
                                .moves
                                .into_iter()
                                .map(|played| MoveEntry {
                                    uci: played.uci,
                                    san: played.san,
                                    number: played.number,
                                    side: played.side.into(),
                                })
                                .collect(),
                            result,
                            participation: Some(encode_metadata(&metadata)),
                            clock: None,
                        },
                    };
                    store
                        .workspace()
                        .upsert_game_record(&record)
                        .map_err(|_| CommandError::Store)?;
                    results.push(ImportReport::Imported {
                        entry: index + 1,
                        title,
                        id,
                    });
                }
                ImportEntry::Failed(failure) => {
                    results.push(ImportReport::Failed(failure));
                }
            }
        }
        self.events.push(import_results_event(&results));
        self.emit_library_changed();
        Ok(())
    }

    fn export_pgn(&mut self, command: &str) -> Result<(), CommandError> {
        let ids = json::read_string_field(command, "ids").ok_or(CommandError::MalformedCommand)?;
        let Some(store) = self.store.as_ref() else {
            return Err(CommandError::Store);
        };
        let mut documents = Vec::new();
        for id in ids.split(',').filter(|id| !id.is_empty()) {
            let record = store
                .workspace()
                .get_game_record(id)
                .map_err(|_| CommandError::Store)?
                .ok_or(CommandError::Store)?;
            if record.payload.variant != "standard" {
                continue;
            }
            let metadata = decode_metadata(record.payload.participation.as_deref());
            let mut tags = pgn::decode_tags(&metadata.tags);
            let site = existing_tag_or(&tags, "Site", "?").to_owned();
            let round = existing_tag_or(&tags, "Round", "?").to_owned();
            set_pgn_tag(&mut tags, "Event", value_or_unknown(&metadata.event));
            set_pgn_tag(&mut tags, "Site", &site);
            set_pgn_tag(
                &mut tags,
                "Date",
                if metadata.date.is_empty() {
                    "????.??.??"
                } else {
                    &metadata.date
                },
            );
            set_pgn_tag(&mut tags, "Round", &round);
            set_pgn_tag(&mut tags, "White", value_or_unknown(&metadata.white));
            set_pgn_tag(&mut tags, "Black", value_or_unknown(&metadata.black));
            let result = record
                .payload
                .result
                .as_ref()
                .map(|value| value.score.as_str())
                .or_else(|| {
                    tags.iter()
                        .find(|(name, _)| name == "Result")
                        .map(|(_, value)| value.as_str())
                })
                .unwrap_or("*")
                .to_owned();
            set_pgn_tag(&mut tags, "Result", &result);
            if record.payload.start_fen != GameRecordPayload::STANDARD_START {
                set_pgn_tag(&mut tags, "SetUp", "1");
                set_pgn_tag(&mut tags, "FEN", &record.payload.start_fen);
            }
            documents.push(pgn::export(&PgnGame {
                tags,
                start_fen: record.payload.start_fen,
                moves: record
                    .payload
                    .moves
                    .into_iter()
                    .map(|entry| PlayedMove {
                        uci: entry.uci,
                        san: entry.san,
                        number: entry.number,
                        side: if entry.side == "black" {
                            "black"
                        } else {
                            "white"
                        },
                    })
                    .collect(),
                result,
            }));
        }
        self.events
            .push(pgn_export_ready_event(&documents.join("\n")));
        Ok(())
    }

    /// Removes and returns the oldest queued event, if any.
    pub fn poll_event(&mut self) -> Option<String> {
        if self.events.is_empty() {
            None
        } else {
            Some(self.events.remove(0))
        }
    }

    fn play_move(&mut self, command: &str) -> Result<(), CommandError> {
        if self.suspended {
            return Err(CommandError::RejectedMove);
        }
        self.apply_elapsed_clock()?;
        if self.setup.is_some() {
            return Err(CommandError::RejectedMove);
        }
        let from = json::read_string_field(command, "from");
        let to = json::read_string_field(command, "to");
        let (Some(from), Some(to)) = (from, to) else {
            return Err(CommandError::MalformedCommand);
        };
        let promotion = json::read_string_field(command, "promotion");
        if self.save_mode == SaveMode::Manual && self.record_id.is_none() {
            self.persist_current_record()?;
        }
        self.game
            .play(&from, &to, promotion.as_deref())
            .map_err(|rejection| match rejection {
                MoveRejected::Illegal | MoveRejected::GameOver | MoveRejected::Reviewing => {
                    CommandError::RejectedMove
                }
            })?;
        if let Some(clock) = self.clock.as_mut() {
            clock.history.push((clock.white_ms, clock.black_ms));
            clock.last_tick = Some(Instant::now());
        }
        self.record_changed()?;
        Ok(())
    }

    fn set_save_mode(&mut self, command: &str) -> Result<(), CommandError> {
        let Some(mode) = json::read_string_field(command, "mode") else {
            return Err(CommandError::MalformedCommand);
        };
        let next = match mode.as_str() {
            "autosave" => SaveMode::Autosave,
            "manual" => SaveMode::Manual,
            _ => return Err(CommandError::MalformedCommand),
        };
        if next == SaveMode::Autosave && self.dirty {
            self.persist_current_record()?;
            self.dirty = false;
        }
        self.save_mode = next;
        if let Some(store) = self.store.as_ref() {
            store
                .workspace()
                .set_residue("save_mode", self.save_mode.name())
                .map_err(|_| CommandError::Store)?;
        }
        Ok(())
    }

    fn save_record(&mut self) -> Result<(), CommandError> {
        if self.record_id.is_some() {
            self.persist_current_record()?;
        }
        self.dirty = false;
        Ok(())
    }

    fn discard_changes(&mut self) -> Result<(), CommandError> {
        if self.dirty {
            let Some(id) = self.record_id.clone() else {
                return Err(CommandError::MalformedCommand);
            };
            self.load_record(&id)?;
        }
        self.dirty = false;
        Ok(())
    }

    fn record_changed(&mut self) -> Result<(), CommandError> {
        if self.save_mode == SaveMode::Manual {
            self.dirty = true;
            Ok(())
        } else {
            self.persist_current_record()
        }
    }

    fn has_unsaved_changes(&self) -> bool {
        self.save_mode == SaveMode::Manual && self.dirty
    }

    fn configure_clock(&mut self, command: &str) -> Result<(), CommandError> {
        if !self.game.moves().is_empty() || self.game.outcome().is_over() {
            return Err(CommandError::RejectedMove);
        }
        let Some(milliseconds) = json::read_string_field(command, "milliseconds")
            .and_then(|value| value.parse::<u64>().ok())
        else {
            return Err(CommandError::MalformedCommand);
        };
        self.clock = if milliseconds == 0 {
            None
        } else {
            Some(GameClock::new(milliseconds))
        };
        Ok(())
    }

    fn tick_clock(&mut self) -> Result<(), CommandError> {
        self.apply_elapsed_clock()
    }

    fn apply_elapsed_clock(&mut self) -> Result<(), CommandError> {
        if self.suspended || self.game.outcome().is_over() || self.game.reviewing() {
            if let Some(clock) = self.clock.as_mut() {
                clock.last_tick = None;
            }
            return Ok(());
        }
        let Some(clock) = self.clock.as_mut() else {
            return Ok(());
        };
        // A configured clock starts with the first move. Before then the
        // Played Game is ready, so neither player's time runs.
        if self.game.moves().is_empty() {
            clock.last_tick = None;
            return Ok(());
        }
        let now = Instant::now();
        let Some(last) = clock.last_tick.replace(now) else {
            return Ok(());
        };
        let elapsed = now.duration_since(last).as_millis() as u64;
        let white_to_move = self.game.white_to_move();
        let remaining = if white_to_move {
            &mut clock.white_ms
        } else {
            &mut clock.black_ms
        };
        *remaining = remaining.saturating_sub(elapsed);
        if *remaining == 0 {
            let loser = if white_to_move {
                Side::White
            } else {
                Side::Black
            };
            self.game.complete_on_time(loser);
            clock.history.push((clock.white_ms, clock.black_ms));
            clock.last_tick = None;
            self.record_changed()?;
        }
        Ok(())
    }

    fn update_metadata(&mut self, command: &str) -> Result<(), CommandError> {
        let Some(id) = self.record_id.clone() else {
            return Err(CommandError::MalformedCommand);
        };
        for (field, destination) in [
            ("white", &mut self.metadata.white),
            ("black", &mut self.metadata.black),
            ("event", &mut self.metadata.event),
            ("date", &mut self.metadata.date),
            ("title", &mut self.metadata.title),
            ("tags", &mut self.metadata.tags),
        ] {
            if let Some(value) = json::read_string_field(command, field) {
                *destination = value;
            }
        }
        if self.save_mode == SaveMode::Manual {
            self.dirty = true;
            Ok(())
        } else {
            self.persist_metadata(&id)
        }
    }

    fn navigate(&mut self, command: &str) -> Result<(), CommandError> {
        let Some(name) = json::read_string_field(command, "to") else {
            return Err(CommandError::MalformedCommand);
        };
        let Some(destination) = Destination::parse(&name) else {
            return Err(CommandError::MalformedCommand);
        };
        // Asking to go where the board already is is not a failure; the
        // workspace still gets told what it is showing.
        self.game.navigate(destination);
        Ok(())
    }

    fn restore_record(&mut self) -> Result<(), CommandError> {
        let Some(offer) = self.restore_offer.clone() else {
            return Err(CommandError::MalformedCommand);
        };
        self.load_record(&offer.record_id)?;
        self.suspended = true;
        self.ensure_tab_open(&offer.record_id);
        self.restore_offer = None;
        self.persist_residue()?;
        self.events
            .push(String::from("{\"type\":\"restore_cleared\"}"));
        self.emit_tabs_changed();
        self.emit_record_graph_changed();
        self.emit_analysis_record_changed();
        Ok(())
    }

    fn suspend_game(&mut self) -> Result<(), CommandError> {
        if !self.can_suspend_game() {
            return Err(CommandError::RejectedMove);
        }
        self.apply_elapsed_clock()?;
        self.suspended = true;
        self.persist_current_record()
    }

    fn resume_game(&mut self) -> Result<(), CommandError> {
        if !self.suspended || self.game.outcome().is_over() {
            return Err(CommandError::RejectedMove);
        }
        self.suspended = false;
        if let Some(clock) = self.clock.as_mut() {
            clock.last_tick = Some(Instant::now());
        }
        Ok(())
    }

    fn can_suspend_game(&mut self) -> bool {
        self.setup.is_none()
            && !self.suspended
            && !self.has_unsaved_changes()
            && !self.game.moves().is_empty()
            && !self.game.reviewing()
            && !self.game.outcome().is_over()
    }

    fn dismiss_restore(&mut self) -> Result<(), CommandError> {
        self.restore_offer = None;
        if let Some(store) = self.store.as_ref() {
            store
                .workspace()
                .clear_residue("active_record_id")
                .map_err(|_| CommandError::Store)?;
        }
        self.events
            .push(String::from("{\"type\":\"restore_cleared\"}"));
        Ok(())
    }

    fn new_game(&mut self) -> Result<(), CommandError> {
        if self.has_unsaved_changes() {
            return Err(CommandError::RejectedMove);
        }
        // The previous Game Record stays in the Live Store; this only clears
        // the board so the next move starts a new record.
        self.game = Game::standard();
        self.record_id = None;
        self.orientation = Orientation::WhiteBottom;
        self.restore_offer = None;
        self.clock = None;
        self.suspended = false;
        self.metadata = GameMetadata::default();
        self.setup = None;
        self.dirty = false;
        if let Some(store) = self.store.as_ref() {
            let _ = store.workspace().clear_residue("active_record_id");
        }
        // Clear the active tab highlight so board/rail and tab chrome agree.
        self.emit_tabs_changed();
        Ok(())
    }

    fn begin_position_setup(&mut self) {
        // Position Setup is not clocked play; entering it suspends and removes
        // the active time control as its capability contract promises.
        self.clock = None;
        let fen = self.game.fen();
        let position = Position::from_fen(&fen).expect("Rules Authority FEN is drawable");
        self.setup = Some(PositionSetup {
            position,
            fen: fen.clone(),
            fen_suffix: fen
                .split_once(' ')
                .map_or("w - - 0 1", |(_, suffix)| suffix)
                .into(),
            rule_valid: true,
            error: String::new(),
        });
    }

    fn set_setup_fen(&mut self, command: &str) -> Result<(), CommandError> {
        let Some(fen) = json::read_string_field(command, "fen") else {
            return Err(CommandError::MalformedCommand);
        };
        let Some(setup) = self.setup.as_mut() else {
            return Err(CommandError::MalformedCommand);
        };
        if fen.split_whitespace().count() != 6 {
            setup.error = "FEN must contain six fields.".into();
            return Ok(());
        }
        let fields: Vec<_> = fen.split_whitespace().collect();
        if !matches!(fields[1], "w" | "b") {
            setup.error = "FEN side to move must be “w” or “b”.".into();
            return Ok(());
        }
        if fields[2] != "-"
            && (fields[2].chars().any(|c| !"KQkq".contains(c)) || fields[2].chars().count() > 4)
        {
            setup.error = "FEN castling rights must use K, Q, k, q, or “-”.".into();
            return Ok(());
        }
        if fields[3] != "-"
            && !(fields[3].len() == 2
                && matches!(fields[3].as_bytes()[0], b'a'..=b'h')
                && matches!(fields[3].as_bytes()[1], b'3' | b'6'))
        {
            setup.error =
                "FEN en-passant target must be a third- or sixth-rank square, or “-”.".into();
            return Ok(());
        }
        if fields[4].parse::<u32>().is_err()
            || fields[5]
                .parse::<u32>()
                .ok()
                .filter(|number| *number > 0)
                .is_none()
        {
            setup.error =
                "FEN move counters must be a non-negative halfmove and positive fullmove number."
                    .into();
            return Ok(());
        }
        let Some(position) = Position::from_fen(&fen) else {
            setup.error =
                "FEN piece placement must describe exactly eight ranks of eight squares.".into();
            return Ok(());
        };
        setup.position = position;
        setup.fen = fen.clone();
        setup.fen_suffix = fields[1..].join(" ");
        setup.rule_valid = Game::from_position(&fen).is_some();
        setup.error.clear();
        Ok(())
    }

    fn place_setup_piece(&mut self, command: &str) -> Result<(), CommandError> {
        let Some(square) = json::read_string_field(command, "square") else {
            return Err(CommandError::MalformedCommand);
        };
        let piece_name = json::read_string_field(command, "piece").unwrap_or_default();
        let piece = if piece_name.is_empty() {
            None
        } else {
            Some(Piece::from_id(&piece_name).ok_or(CommandError::MalformedCommand)?)
        };
        let Some(setup) = self.setup.as_mut() else {
            return Err(CommandError::MalformedCommand);
        };
        if !setup.position.place(&square, piece) {
            return Err(CommandError::MalformedCommand);
        }
        reclassify_setup(setup);
        Ok(())
    }

    fn relocate_setup_piece(&mut self, command: &str) -> Result<(), CommandError> {
        let (Some(from), Some(to)) = (
            json::read_string_field(command, "from"),
            json::read_string_field(command, "to"),
        ) else {
            return Err(CommandError::MalformedCommand);
        };
        let Some(setup) = self.setup.as_mut() else {
            return Err(CommandError::MalformedCommand);
        };
        if !setup.position.relocate(&from, &to) {
            return Err(CommandError::MalformedCommand);
        }
        reclassify_setup(setup);
        Ok(())
    }

    fn start_setup_game(&mut self) -> Result<(), CommandError> {
        let Some(setup) = self.setup.as_ref() else {
            return Err(CommandError::MalformedCommand);
        };
        if !setup.rule_valid {
            return Err(CommandError::RejectedMove);
        }
        self.game = Game::from_position(&setup.fen).ok_or(CommandError::RejectedMove)?;
        self.setup = None;
        self.record_id = None;
        self.restore_offer = None;
        self.metadata = GameMetadata::default();
        self.suspended = false;
        Ok(())
    }

    fn open_record(&mut self, command: &str) -> Result<(), CommandError> {
        if self.has_unsaved_changes() {
            return Err(CommandError::RejectedMove);
        }
        let Some(id) = json::read_string_field(command, "id") else {
            return Err(CommandError::MalformedCommand);
        };
        self.load_record(&id)?;
        self.ensure_tab_open(&id);
        self.restore_offer = None;
        self.persist_residue()?;
        self.events
            .push(String::from("{\"type\":\"restore_cleared\"}"));
        self.emit_tabs_changed();
        self.emit_record_graph_changed();
        self.emit_analysis_record_changed();
        Ok(())
    }

    fn close_tab(&mut self, command: &str) -> Result<(), CommandError> {
        let Some(id) = json::read_string_field(command, "id") else {
            return Err(CommandError::MalformedCommand);
        };
        let Some(index) = self.open_tabs.iter().position(|tab| tab == &id) else {
            return Ok(());
        };
        self.open_tabs.remove(index);
        let was_active = self.record_id.as_deref() == Some(id.as_str());
        if was_active {
            if let Some(next) = self.open_tabs.get(index).cloned().or_else(|| {
                index
                    .checked_sub(1)
                    .and_then(|i| self.open_tabs.get(i).cloned())
            }) {
                self.load_record(&next)?;
            } else {
                self.game = Game::standard();
                self.record_id = None;
                self.clock = None;
                self.suspended = false;
                self.metadata = GameMetadata::default();
                self.setup = None;
                if let Some(store) = self.store.as_ref() {
                    let _ = store.workspace().clear_residue("active_record_id");
                }
            }
        }
        self.persist_residue()?;
        self.emit_tabs_changed();
        Ok(())
    }

    fn load_record(&mut self, id: &str) -> Result<(), CommandError> {
        let Some(store) = self.store.as_ref() else {
            return Err(CommandError::Store);
        };
        let record = store
            .workspace()
            .get_game_record(id)
            .map_err(|_| CommandError::Store)?
            .ok_or(CommandError::Store)?;
        let stored_result = record.payload.result.clone();
        let stored_clock = record.payload.clock.as_deref().and_then(decode_clock);
        let moves = record
            .payload
            .moves
            .into_iter()
            .map(|entry| PlayedMove {
                uci: entry.uci,
                san: entry.san,
                number: entry.number,
                side: match entry.side.as_str() {
                    "black" => "black",
                    _ => "white",
                },
            })
            .collect();
        self.game =
            Game::from_history(&record.payload.start_fen, moves).ok_or(CommandError::Store)?;
        if stored_result
            .as_ref()
            .is_some_and(|result| result.termination == "time_forfeit")
        {
            let loser = if stored_clock
                .as_ref()
                .is_some_and(|clock| clock.white_ms == 0)
            {
                Side::White
            } else if stored_clock
                .as_ref()
                .is_some_and(|clock| clock.black_ms == 0)
                || stored_result
                    .as_ref()
                    .is_some_and(|result| result.score == "1-0")
            {
                Side::Black
            } else {
                Side::White
            };
            self.game.complete_on_time(loser);
        }
        self.clock = stored_clock;
        self.suspended = stored_result.is_none() && record.ply_count > 0;
        if record.kind == GameRecordKind::Analysis {
            self.suspended = false;
        }
        self.metadata = decode_metadata(record.payload.participation.as_deref());
        if self.metadata.title.is_empty() {
            self.metadata.title = record.title.clone().unwrap_or_default();
        }
        self.setup = None;
        self.record_id = Some(record.id);
        self.dirty = false;
        Ok(())
    }

    fn ensure_tab_open(&mut self, id: &str) {
        if !self.open_tabs.iter().any(|tab| tab == id) {
            self.open_tabs.push(id.to_owned());
        }
    }

    fn persist_current_record(&mut self) -> Result<(), CommandError> {
        let Some(store) = self.store.as_ref() else {
            return Ok(());
        };
        let now = timestamp_now();
        let id = self.record_id.clone().unwrap_or_else(new_record_id);
        let existing = store.workspace().get_game_record(&id).ok().flatten();
        let kind = existing
            .as_ref()
            .map_or(GameRecordKind::Played, |record| record.kind);
        let outcome = self.game.outcome();
        let result = if kind == GameRecordKind::Played && outcome.is_over() {
            Some(RecordResult {
                status: status_name(outcome.winner).to_owned(),
                termination: outcome.termination.name().to_owned(),
                score: outcome.winner.score().to_owned(),
            })
        } else {
            None
        };
        let result_score = result.as_ref().map(|result| result.score.clone());
        let payload = GameRecordPayload {
            variant: "standard".into(),
            start_fen: self.game.start_fen().to_owned(),
            moves: self
                .game
                .moves()
                .iter()
                .map(|played| MoveEntry {
                    uci: played.uci.clone(),
                    san: played.san.clone(),
                    number: played.number,
                    side: played.side.to_owned(),
                })
                .collect(),
            result,
            participation: Some(encode_metadata(&self.metadata)),
            clock: (kind == GameRecordKind::Played)
                .then(|| self.clock.as_ref().map(encode_clock))
                .flatten(),
        };
        let created_at = existing
            .as_ref()
            .map(|record| record.created_at.clone())
            .unwrap_or_else(|| now.clone());
        let record = GameRecord {
            id: id.clone(),
            kind,
            title: (!self.metadata.title.is_empty()).then(|| self.metadata.title.clone()),
            result_score,
            ply_count: payload.moves.len() as u32,
            archived: false,
            created_at,
            updated_at: now,
            payload,
        };
        store
            .workspace()
            .upsert_game_record(&record)
            .map_err(|_| CommandError::Store)?;
        self.record_id = Some(id.clone());
        self.ensure_tab_open(&id);
        self.persist_residue()?;
        // Playing a new game dismisses any prior restore offer.
        self.restore_offer = None;
        self.emit_library_changed();
        self.emit_tabs_changed();
        Ok(())
    }

    fn persist_metadata(&mut self, id: &str) -> Result<(), CommandError> {
        let Some(store) = self.store.as_ref() else {
            return Ok(());
        };
        let mut record = store
            .workspace()
            .get_game_record(id)
            .map_err(|_| CommandError::Store)?
            .ok_or(CommandError::Store)?;
        record.title = (!self.metadata.title.is_empty()).then(|| self.metadata.title.clone());
        record.payload.participation = Some(encode_metadata(&self.metadata));
        record.updated_at = timestamp_now();
        store
            .workspace()
            .upsert_game_record(&record)
            .map_err(|_| CommandError::Store)?;
        self.emit_library_changed();
        self.emit_tabs_changed();
        Ok(())
    }

    fn emit_library_changed(&mut self) {
        let Some(store) = self.store.as_ref() else {
            return;
        };
        let Ok(summaries) = store.workspace().list_game_records() else {
            return;
        };
        // Archived records stay out of the default Personal Library view.
        let visible: Vec<_> = summaries.into_iter().filter(|s| !s.archived).collect();
        self.events.push(library_changed_event(&visible));
    }

    fn emit_tabs_changed(&mut self) {
        let titles = self.tab_titles();
        self.events.push(tabs_changed_event(
            &self.open_tabs,
            self.record_id.as_deref(),
            &titles,
        ));
    }

    fn tab_titles(&self) -> Vec<String> {
        let Some(store) = self.store.as_ref() else {
            return self
                .open_tabs
                .iter()
                .map(|_| "Game Record".into())
                .collect();
        };
        self.open_tabs
            .iter()
            .map(|id| match store.workspace().get_game_record(id) {
                Ok(Some(record)) => record
                    .title
                    .clone()
                    .unwrap_or_else(|| default_title_for(record.kind, record.ply_count)),
                _ => "Game Record".into(),
            })
            .collect()
    }

    fn persist_residue(&self) -> Result<(), CommandError> {
        let Some(store) = self.store.as_ref() else {
            return Ok(());
        };
        if let Some(id) = &self.record_id {
            store
                .workspace()
                .set_residue("active_record_id", id)
                .map_err(|_| CommandError::Store)?;
        }
        store
            .workspace()
            .set_residue("open_tab_ids", &encode_tab_ids(&self.open_tabs))
            .map_err(|_| CommandError::Store)?;
        Ok(())
    }

    fn board_changed_event(&mut self) -> String {
        let mut out = String::with_capacity(4096);
        out.push_str("{\"type\":\"board_changed\",\"variant\":\"standard\",\"orientation\":");
        json::write_string(&mut out, self.orientation.name());

        out.push_str(",\"squares\":[");
        if let Some(definition) = &self.workshop {
            let mut index = 0;
            for rank in (1..=definition.ranks).rev() {
                for file in 0..definition.files {
                    if index > 0 {
                        out.push(',');
                    }
                    index += 1;
                    out.push_str("{\"name\":");
                    json::write_string(&mut out, &format!("{}{}", (b'a' + file) as char, rank));
                    out.push_str(",\"light\":");
                    out.push_str(if (rank + file) % 2 == 1 {
                        "true"
                    } else {
                        "false"
                    });
                    out.push_str(",\"piece\":");
                    let square = format!("{}{}", (b'a' + file) as char, rank);
                    match definition
                        .placement
                        .get(&square)
                        .and_then(|piece| workshop_piece_id(definition, piece))
                    {
                        Some(piece) => json::write_string(&mut out, &piece),
                        None => out.push_str("null"),
                    }
                    out.push_str(",\"footprint\":");
                    json::write_string(&mut out, rule_footprint(definition, &square));
                    out.push('}');
                }
            }
            out.push_str("],\"activity\":\"variant_workshop\",\"sideToMove\":\"white\",\"inCheck\":false,\"moves\":[],\"moveList\":[],\"cursor\":0,\"reviewing\":false,\"lastMove\":{\"from\":null,\"to\":null},\"result\":{\"over\":false,\"label\":\"\",\"status\":\"\",\"score\":\"\"},\"clock\":{\"enabled\":false,\"running\":false,\"whiteMs\":0,\"blackMs\":0},\"metadata\":{\"white\":\"\",\"black\":\"\",\"event\":\"\",\"date\":\"\",\"title\":\"\",\"tags\":\"\"}}");
            return out;
        }
        let position = self.setup.as_ref().map(|setup| &setup.position);
        let game_position;
        let position = match position {
            Some(position) => position,
            None => {
                game_position = self.game.position();
                &game_position
            }
        };
        for (index, square) in position.rendered(self.orientation).iter().enumerate() {
            if index > 0 {
                out.push(',');
            }
            out.push_str("{\"name\":");
            json::write_string(&mut out, &square.name);
            out.push_str(",\"light\":");
            out.push_str(if square.light { "true" } else { "false" });
            out.push_str(",\"piece\":");
            match &square.piece {
                Some(piece) => json::write_string(&mut out, &piece.id()),
                None => out.push_str("null"),
            }
            out.push('}');
        }
        out.push(']');
        out.push_str(",\"displayedFen\":");
        json::write_string(&mut out, &position.setup_fen());
        out.push_str(",\"displayedPositionRuleValid\":");
        out.push_str(
            if self.setup.as_ref().is_none_or(|setup| setup.rule_valid) {
                "true"
            } else {
                "false"
            },
        );

        if let Some(setup) = &self.setup {
            out.push_str(",\"activity\":\"position_setup\",\"positionClass\":");
            json::write_string(
                &mut out,
                if setup.rule_valid {
                    "Rule-valid Position"
                } else {
                    "Freeform Position"
                },
            );
            out.push_str(",\"setupFen\":");
            json::write_string(&mut out, &setup.fen);
            out.push_str(",\"setupError\":");
            json::write_string(&mut out, &setup.error);
            out.push_str(",\"positionCapabilities\":");
            json::write_string(
                &mut out,
                if setup.rule_valid {
                    "Clocks · Result detection · Start a Played Game · Engine use"
                } else {
                    "No clocks · No result detection · Cannot start a Played Game · Engine use not guaranteed"
                },
            );
        } else {
            let activity = self
                .record_id
                .as_ref()
                .and_then(|id| self.store.as_ref()?.workspace().get_game_record(id).ok().flatten())
                .map_or("played_game", |record| {
                    if record.kind == GameRecordKind::Analysis {
                        "analysis_record"
                    } else {
                        "played_game"
                    }
                });
            out.push_str(",\"activity\":");
            json::write_string(&mut out, activity);
        }

        out.push_str(",\"sideToMove\":");
        json::write_string(
            &mut out,
            if self.game.white_to_move() {
                "white"
            } else {
                "black"
            },
        );
        out.push_str(",\"inCheck\":");
        out.push_str(if self.game.in_check() {
            "true"
        } else {
            "false"
        });

        // The moves a player may make now. The workspace uses these to show
        // where a picked-up piece may go and to offer a promotion choice; the
        // core still rejects anything else that arrives.
        out.push_str(",\"moves\":[");
        let offers = if self.suspended {
            Vec::new()
        } else {
            self.game.offers()
        };
        for (index, offer) in offers.iter().enumerate() {
            if index > 0 {
                out.push(',');
            }
            out.push_str("{\"from\":");
            json::write_string(&mut out, &offer.from);
            out.push_str(",\"to\":");
            json::write_string(&mut out, &offer.to);
            out.push_str(",\"promotions\":[");
            for (place, role) in offer.promotions.iter().enumerate() {
                if place > 0 {
                    out.push(',');
                }
                json::write_string(&mut out, role);
            }
            out.push_str("]}");
        }
        out.push(']');

        out.push_str(",\"moveList\":[");
        for (index, played) in self.game.moves().iter().enumerate() {
            if index > 0 {
                out.push(',');
            }
            // The number and the side are the engine's, so a variant whose
            // sides do not simply alternate is still numbered correctly.
            out.push_str("{\"number\":");
            out.push_str(&played.number.to_string());
            out.push_str(",\"side\":");
            json::write_string(&mut out, played.side);
            out.push_str(",\"san\":");
            json::write_string(&mut out, &played.san);
            out.push_str(",\"uci\":");
            json::write_string(&mut out, &played.uci);
            out.push('}');
        }
        out.push(']');

        out.push_str(",\"cursor\":");
        out.push_str(&self.game.cursor().to_string());
        out.push_str(",\"reviewing\":");
        out.push_str(if self.game.reviewing() {
            "true"
        } else {
            "false"
        });
        out.push_str(",\"saveMode\":");
        json::write_string(&mut out, self.save_mode.name());
        out.push_str(",\"dirty\":");
        out.push_str(if self.has_unsaved_changes() {
            "true"
        } else {
            "false"
        });
        out.push_str(",\"needsUnsavedDecision\":");
        out.push_str(if self.has_unsaved_changes() {
            "true"
        } else {
            "false"
        });
        out.push_str(",\"suspended\":");
        out.push_str(if self.suspended { "true" } else { "false" });
        out.push_str(",\"canSuspend\":");
        out.push_str(if self.can_suspend_game() {
            "true"
        } else {
            "false"
        });

        out.push_str(",\"clock\":");
        match &self.clock {
            Some(clock) => {
                out.push_str("{\"enabled\":true,\"initialMs\":");
                out.push_str(&clock.initial_ms.to_string());
                out.push_str(",\"whiteMs\":");
                out.push_str(&clock.white_ms.to_string());
                out.push_str(",\"blackMs\":");
                out.push_str(&clock.black_ms.to_string());
                out.push_str(",\"running\":");
                let running = !self.game.moves().is_empty()
                    && !self.game.outcome().is_over()
                    && !self.game.reviewing()
                    && !self.suspended;
                out.push_str(if running { "true" } else { "false" });
                out.push('}');
            }
            None => out.push_str("{\"enabled\":false}"),
        }
        out.push_str(",\"metadata\":{");
        for (index, (name, value)) in [
            ("white", &self.metadata.white),
            ("black", &self.metadata.black),
            ("event", &self.metadata.event),
            ("date", &self.metadata.date),
            ("title", &self.metadata.title),
            ("tags", &self.metadata.tags),
        ]
        .into_iter()
        .enumerate()
        {
            if index > 0 {
                out.push(',');
            }
            json::write_string(&mut out, name);
            out.push(':');
            json::write_string(&mut out, value);
        }
        out.push('}');

        out.push_str(",\"lastMove\":");
        match self.game.last_move() {
            // The move is given by its squares so the workspace can mark them
            // without reading chess notation.
            Some(played) => {
                let (from, rest) = played.uci.split_at(2);
                out.push_str("{\"from\":");
                json::write_string(&mut out, from);
                out.push_str(",\"to\":");
                json::write_string(&mut out, &rest[..2]);
                out.push('}');
            }
            None => out.push_str("null"),
        }

        let outcome = self.game.outcome();
        out.push_str(",\"result\":{\"status\":");
        json::write_string(&mut out, status_name(outcome.winner));
        out.push_str(",\"termination\":");
        json::write_string(&mut out, outcome.termination.name());
        out.push_str(",\"score\":");
        json::write_string(&mut out, outcome.winner.score());
        out.push_str(",\"label\":");
        json::write_string(&mut out, &result_label(outcome));
        out.push_str(",\"over\":");
        out.push_str(if outcome.is_over() { "true" } else { "false" });
        out.push_str("}}");
        out
    }

    fn workshop_changed_event(&self) -> String {
        let definition = self
            .workshop
            .as_ref()
            .expect("workshop event needs a definition");
        let (max_files, max_ranks) = engine_geometry();
        let mut out = String::from("{\"type\":\"workshop_changed\",\"active\":true,\"step\":");
        out.push_str(&definition.step.to_string());
        out.push_str(",\"files\":");
        out.push_str(&definition.files.to_string());
        out.push_str(",\"ranks\":");
        out.push_str(&definition.ranks.to_string());
        out.push_str(",\"selectedPieces\":");
        json::write_string(&mut out, &definition.pieces);
        out.push_str(",\"fen\":");
        json::write_string(&mut out, &workshop_fen(definition));
        out.push_str(",\"ruleValid\":");
        out.push_str(if workshop_position_rule_valid(definition) {
            "true"
        } else {
            "false"
        });
        out.push_str(",\"rules\":{");
        for (index, (name, selected)) in [
            ("royal", true),
            ("promotion", definition.promotion),
            ("castling", definition.castling),
            ("doubleStep", definition.double_step),
            ("extinction", definition.extinction),
            ("goal", definition.goal),
            ("mandatoryCapture", definition.mandatory_capture),
            ("drops", definition.drops),
        ]
        .iter()
        .enumerate()
        {
            if index > 0 {
                out.push(',');
            }
            json::write_string(&mut out, name);
            out.push(':');
            out.push_str(if *selected { "true" } else { "false" });
        }
        out.push_str("},\"ruleConflict\":");
        json::write_string(
            &mut out,
            if definition.extinction {
                "Royal checkmate and Extinction both decide how the game ends. Choose one win condition."
            } else {
                ""
            },
        );
        out.push_str(",\"playable\":");
        out.push_str(if definition.playable { "true" } else { "false" });
        out.push_str(",\"validationMessage\":");
        json::write_string(&mut out, &definition.validation_message);
        for (key, value) in [
            ("customName", &definition.custom_name),
            ("customLetter", &definition.custom_letter),
            ("customBetza", &definition.custom_betza),
            ("error", &definition.error),
        ] {
            out.push(',');
            json::write_string(&mut out, key);
            out.push(':');
            json::write_string(&mut out, value);
        }
        out.push_str(",\"presets\":[");
        for (index, (id, name, files, ranks)) in [
            ("standard-8x8", "Standard 8×8", 8, 8),
            ("grand-10x8", "Grand 10×8", 10, 8),
            ("wide-10x10", "Wide 10×10", 10, 10),
            ("max-12x10", "Max 12×10", 12, 10),
        ]
        .iter()
        .enumerate()
        {
            if index > 0 {
                out.push(',');
            }
            let available = *files <= max_files && *ranks <= max_ranks;
            out.push_str("{\"id\":");
            json::write_string(&mut out, id);
            out.push_str(",\"name\":");
            json::write_string(&mut out, name);
            out.push_str(",\"available\":");
            out.push_str(if available { "true" } else { "false" });
            out.push_str(",\"reason\":");
            json::write_string(
                &mut out,
                if available {
                    ""
                } else if max_files == 0 {
                    "No Fairy-Stockfish build detected"
                } else {
                    "Detected build supports boards up to 8×8"
                },
            );
            out.push('}');
        }
        out.push_str("]}");
        out.pop();
        out.push_str(",\"pieces\":[");
        for (index, (code, name, betza)) in [
            ("K", "King", "K"),
            ("Q", "Queen", "Q"),
            ("R", "Rook", "R"),
            ("B", "Bishop", "B"),
            ("N", "Knight", "N"),
            ("P", "Pawn", "fmWfceF"),
            ("A", "Archbishop", "BN"),
            ("C", "Chancellor", "RN"),
            ("M", "Amazon", "QN"),
            ("F", "Ferz", "F"),
            ("W", "Wazir", "W"),
            ("G", "Grasshopper", "gQ"),
            ("O", "Cannon", "mRcpR"),
        ]
        .iter()
        .enumerate()
        {
            if index > 0 {
                out.push(',');
            }
            out.push_str("{\"code\":");
            json::write_string(&mut out, code);
            out.push_str(",\"name\":");
            json::write_string(&mut out, name);
            out.push_str(",\"betza\":");
            json::write_string(&mut out, betza);
            out.push('}');
        }
        out.push_str("]}");
        out
    }
}

fn encode_metadata(metadata: &GameMetadata) -> String {
    [
        &metadata.white,
        &metadata.black,
        &metadata.event,
        &metadata.date,
        &metadata.title,
        &metadata.tags,
    ]
    .map(|value| value.replace('\n', " "))
    .join("\n")
}

fn decode_metadata(encoded: Option<&str>) -> GameMetadata {
    let parts: Vec<_> = encoded.unwrap_or("").split('\n').collect();
    let at = |index: usize| parts.get(index).copied().unwrap_or("").to_owned();
    GameMetadata {
        white: at(0),
        black: at(1),
        event: at(2),
        date: at(3),
        title: at(4),
        tags: at(5),
    }
}

fn encode_clock(clock: &GameClock) -> String {
    let history = clock
        .history
        .iter()
        .map(|(white, black)| format!("{white}:{black}"))
        .collect::<Vec<_>>()
        .join(",");
    format!(
        "{};{};{};{}",
        clock.initial_ms, clock.white_ms, clock.black_ms, history
    )
}

fn decode_clock(encoded: &str) -> Option<GameClock> {
    let mut fields = encoded.splitn(4, ';');
    let initial_ms = fields.next()?.parse().ok()?;
    let white_ms = fields.next()?.parse().ok()?;
    let black_ms = fields.next()?.parse().ok()?;
    let history = fields
        .next()
        .unwrap_or("")
        .split(',')
        .filter(|entry| !entry.is_empty())
        .filter_map(|entry| {
            let (white, black) = entry.split_once(':')?;
            Some((white.parse().ok()?, black.parse().ok()?))
        })
        .collect();
    Some(GameClock {
        initial_ms,
        white_ms,
        black_ms,
        history,
        last_tick: None,
    })
}

fn reclassify_setup(setup: &mut PositionSetup) {
    let generated = setup.position.setup_fen();
    let placement = generated
        .split_once(' ')
        .map_or(generated.as_str(), |(value, _)| value);
    setup.fen = format!("{placement} {}", setup.fen_suffix);
    setup.rule_valid = Game::from_position(&setup.fen).is_some();
    setup.error.clear();
}

fn restore_available_event(offer: &RestoreOffer) -> String {
    let mut out = String::from("{\"type\":\"restore_available\",\"recordId\":");
    json::write_string(&mut out, &offer.record_id);
    out.push_str(",\"plyCount\":");
    out.push_str(&offer.ply_count.to_string());
    out.push_str(",\"label\":");
    let label = if offer.ply_count == 1 {
        String::from("Restore suspended Played Game · 1 move")
    } else {
        format!("Restore suspended Played Game · {} moves", offer.ply_count)
    };
    json::write_string(&mut out, &label);
    out.push('}');
    out
}

fn preset_geometry(id: &str) -> Option<(u8, u8)> {
    match id {
        "standard-8x8" => Some((8, 8)),
        "grand-10x8" => Some((10, 8)),
        "wide-10x10" => Some((10, 10)),
        "max-12x10" => Some((12, 10)),
        _ => None,
    }
}

fn engine_geometry() -> (u8, u8) {
    match std::env::var("OMACHESS_FAIRY_STOCKFISH_CAPABILITIES").as_deref() {
        Ok("largeboards") => (12, 10),
        Ok("none") => (0, 0),
        Ok("stock") => (8, 8),
        _ => (12, 10),
    }
}

fn encode_variant_definition(definition: &VariantDefinition) -> String {
    let mut out = String::from("{\"schemaVersion\":\"1\"");
    let files = definition.files.to_string();
    let ranks = definition.ranks.to_string();
    let placement = encode_placement(&definition.placement);
    for (key, value) in [
        ("preset", definition.preset.as_str()),
        ("files", files.as_str()),
        ("ranks", ranks.as_str()),
        ("pieces", definition.pieces.as_str()),
        ("customName", definition.custom_name.as_str()),
        ("customLetter", definition.custom_letter.as_str()),
        ("customBetza", definition.custom_betza.as_str()),
        ("placement", placement.as_str()),
        ("promotion", bool_name(definition.promotion)),
        ("castling", bool_name(definition.castling)),
        ("doubleStep", bool_name(definition.double_step)),
        ("extinction", bool_name(definition.extinction)),
        ("goal", bool_name(definition.goal)),
        ("mandatoryCapture", bool_name(definition.mandatory_capture)),
        ("drops", bool_name(definition.drops)),
        ("playable", bool_name(definition.playable)),
        ("validationMessage", definition.validation_message.as_str()),
    ] {
        out.push(',');
        json::write_string(&mut out, key);
        out.push(':');
        json::write_string(&mut out, value);
    }
    out.push('}');
    out
}

fn bool_name(value: bool) -> &'static str {
    if value {
        "true"
    } else {
        "false"
    }
}

fn read_bool(value: &str, key: &str, fallback: bool) -> bool {
    json::read_string_field(value, key)
        .map(|selected| selected == "true")
        .unwrap_or(fallback)
}

fn encode_placement(placement: &BTreeMap<String, String>) -> String {
    placement
        .iter()
        .map(|(square, piece)| format!("{square}:{piece}"))
        .collect::<Vec<_>>()
        .join(",")
}

fn decode_placement(encoded: &str) -> BTreeMap<String, String> {
    encoded
        .split(',')
        .filter_map(|entry| entry.split_once(':'))
        .map(|(square, piece)| (square.to_owned(), piece.to_owned()))
        .collect()
}

fn square_in_geometry(square: &str, files: u8, ranks: u8) -> bool {
    let bytes = square.as_bytes();
    if bytes.len() < 2 || !(b'a'..b'a' + files).contains(&bytes[0]) {
        return false;
    }
    square[1..]
        .parse::<u8>()
        .is_ok_and(|rank| (1..=ranks).contains(&rank))
}

fn workshop_piece_id(definition: &VariantDefinition, code: &str) -> Option<String> {
    let role = match code.to_ascii_uppercase().as_str() {
        "K" => "king",
        "Q" => "queen",
        "R" => "rook",
        "B" => "bishop",
        "N" => "knight",
        "P" => "pawn",
        selected
            if definition.pieces.contains(selected)
                || definition.custom_letter.eq_ignore_ascii_case(selected) =>
        {
            return Some(format!(
                "{}_fairy_{selected}",
                if code.chars().all(char::is_uppercase) {
                    "white"
                } else {
                    "black"
                }
            ));
        }
        _ => return None,
    };
    Some(format!(
        "{}_{role}",
        if code.chars().all(char::is_uppercase) {
            "white"
        } else {
            "black"
        }
    ))
}

fn workshop_position_rule_valid(definition: &VariantDefinition) -> bool {
    definition.files == 8
        && definition.ranks == 8
        && Rules::new("standard", Some(&draft_variant_fen(definition))).is_some()
}

fn rule_footprint(definition: &VariantDefinition, square: &str) -> &'static str {
    if square.len() < 2 {
        return "";
    }
    let file = square.as_bytes()[0];
    let rank = square[1..].parse::<u8>().unwrap_or_default();
    if definition.castling && matches!(file, b'c' | b'g') && (rank == 1 || rank == definition.ranks)
    {
        "castling"
    } else if definition.promotion && (rank == 1 || rank == definition.ranks) {
        "promotion"
    } else if definition.goal && matches!(file, b'd' | b'e') && matches!(rank, 4 | 5) {
        "goal"
    } else {
        ""
    }
}

fn workshop_fen(definition: &VariantDefinition) -> String {
    let draft = draft_variant_fen(definition);
    if definition.files == 8 && definition.ranks == 8 {
        if let Some(mut rules) = Rules::new("standard", Some(&draft)) {
            return rules.fen();
        }
    }
    draft
}

// An incomplete Draft Variant Definition cannot be loaded by the Rules
// Authority yet. This serialization is only its live editor text; once the
// authority accepts it, `workshop_fen` replaces it with the authority's FEN.
fn draft_variant_fen(definition: &VariantDefinition) -> String {
    let mut rows = Vec::with_capacity(definition.ranks as usize);
    for rank in (1..=definition.ranks).rev() {
        let mut row = String::new();
        let mut empty = 0;
        for file in 0..definition.files {
            let square = format!("{}{}", (b'a' + file) as char, rank);
            if let Some(piece) = definition.placement.get(&square) {
                if empty > 0 {
                    row.push_str(&empty.to_string());
                    empty = 0;
                }
                row.push_str(piece);
            } else {
                empty += 1;
            }
        }
        if empty > 0 {
            row.push_str(&empty.to_string());
        }
        rows.push(row);
    }
    format!("{} w - - 0 1", rows.join("/"))
}

fn decode_variant_definition(value: &str) -> Option<VariantDefinition> {
    if json::read_string_field(value, "schemaVersion").as_deref() != Some("1") {
        return None;
    }
    Some(VariantDefinition {
        preset: json::read_string_field(value, "preset")?,
        files: json::read_string_field(value, "files")?.parse().ok()?,
        ranks: json::read_string_field(value, "ranks")?.parse().ok()?,
        pieces: json::read_string_field(value, "pieces")?,
        custom_name: json::read_string_field(value, "customName")?,
        custom_letter: json::read_string_field(value, "customLetter")?,
        custom_betza: json::read_string_field(value, "customBetza")?,
        placement: decode_placement(
            &json::read_string_field(value, "placement").unwrap_or_default(),
        ),
        promotion: read_bool(value, "promotion", true),
        castling: read_bool(value, "castling", true),
        double_step: read_bool(value, "doubleStep", true),
        extinction: read_bool(value, "extinction", false),
        goal: read_bool(value, "goal", false),
        mandatory_capture: read_bool(value, "mandatoryCapture", false),
        drops: read_bool(value, "drops", false),
        error: String::new(),
        playable: read_bool(value, "playable", false),
        validation_message: json::read_string_field(value, "validationMessage").unwrap_or_default(),
        step: 1,
    })
}

fn compile_variant_adapter(definition: &VariantDefinition) -> Vec<u8> {
    let mut adapter = format!(
        "[omachess:chess]\nmaxFile = {}\nmaxRank = {}\nstartFen = {}\ncastling = {}\ndoubleStep = {}\nmustCapture = {}\npieceDrops = {}\ncapturesToHand = {}\n",
        definition.files,
        definition.ranks,
        draft_variant_fen(definition),
        definition.castling,
        definition.double_step,
        definition.mandatory_capture,
        definition.drops,
        definition.drops,
    );
    if !definition.promotion {
        adapter.push_str("promotionPawnTypes =\npromotionPieceTypes =\n");
    }
    if !definition.custom_letter.is_empty() {
        adapter.push_str(&format!(
            "customPiece1 = {}:{}\n",
            definition.custom_letter.to_ascii_lowercase(),
            definition.custom_betza
        ));
    }
    if definition.goal {
        adapter.push_str(
            "flagPiece = k\nflagRegionWhite = d4 e4 d5 e5\nflagRegionBlack = d4 e4 d5 e5\n",
        );
    }
    adapter.into_bytes()
}

#[derive(Clone, Copy)]
enum IsolatedFailure {
    Deadline,
    Rejected,
}

fn run_isolated_validation(stage: &str, fen: &str) -> Result<(), IsolatedFailure> {
    let executable = std::env::current_exe().map_err(|_| IsolatedFailure::Rejected)?;
    let mut child = Command::new(executable)
        .arg("--variant-validation-worker")
        .arg(stage)
        .stdin(Stdio::piped())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .map_err(|_| IsolatedFailure::Rejected)?;
    let written = child
        .stdin
        .take()
        .is_some_and(|mut input| input.write_all(fen.as_bytes()).is_ok());
    if !written {
        let _ = child.kill();
        return Err(IsolatedFailure::Rejected);
    }
    let deadline = Instant::now() + Duration::from_secs(2);
    loop {
        match child.try_wait() {
            Ok(Some(status)) => {
                return status
                    .success()
                    .then_some(())
                    .ok_or(IsolatedFailure::Rejected)
            }
            Ok(None) if Instant::now() < deadline => thread::sleep(Duration::from_millis(10)),
            _ => {
                let _ = child.kill();
                let _ = child.wait();
                return Err(IsolatedFailure::Deadline);
            }
        }
    }
}

fn library_changed_event(records: &[GameRecordSummary]) -> String {
    let mut out = String::from("{\"type\":\"library_changed\",\"records\":[");
    for (index, record) in records.iter().enumerate() {
        if index > 0 {
            out.push(',');
        }
        out.push_str("{\"id\":");
        json::write_string(&mut out, &record.id);
        out.push_str(",\"kind\":");
        json::write_string(&mut out, record.kind.as_str());
        out.push_str(",\"title\":");
        match &record.title {
            Some(title) => json::write_string(&mut out, title),
            None => json::write_string(&mut out, &default_record_title(record)),
        }
        out.push_str(",\"plyCount\":");
        out.push_str(&record.ply_count.to_string());
        out.push_str(",\"resultScore\":");
        match &record.result_score {
            Some(score) => json::write_string(&mut out, score),
            None => out.push_str("null"),
        }
        out.push('}');
    }
    out.push_str("]}");
    out
}

fn analysis_record_changed_event(
    data: &AnalysisRecordData,
    sources: &[String],
    derivations: &[String],
) -> String {
    let mut out = String::from("{\"type\":\"analysis_record_changed\",\"sourceSnapshot\":{");
    out.push_str("\"sourceId\":");
    json::write_string(&mut out, &data.source_snapshot.source_id);
    out.push_str(",\"startFen\":");
    json::write_string(&mut out, &data.source_snapshot.start_fen);
    out.push_str(",\"moveCount\":");
    out.push_str(&data.source_snapshot.moves.len().to_string());
    out.push_str("},\"sources\":[");
    for (index, id) in sources.iter().enumerate() {
        if index > 0 {
            out.push(',');
        }
        json::write_string(&mut out, id);
    }
    out.push_str("],\"derivations\":[");
    for (index, id) in derivations.iter().enumerate() {
        if index > 0 {
            out.push(',');
        }
        json::write_string(&mut out, id);
    }
    out.push_str("],\"mainLinePly\":");
    out.push_str(&data.main_line.len().to_string());
    out.push_str(",\"sidelineCount\":");
    out.push_str(&data.sidelines.len().to_string());
    out.push_str(",\"annotationCount\":");
    out.push_str(&data.annotations.len().to_string());
    out.push_str(",\"annotations\":[");
    for (index, annotation) in data.annotations.iter().enumerate() {
        if index > 0 {
            out.push(',');
        }
        out.push_str("{\"ply\":");
        out.push_str(&annotation.ply.to_string());
        out.push_str(",\"text\":");
        json::write_string(&mut out, &annotation.text);
        out.push('}');
    }
    out.push_str("],\"sidelines\":[");
    for (index, sideline) in data.sidelines.iter().enumerate() {
        if index > 0 {
            out.push(',');
        }
        out.push_str("{\"afterPly\":");
        out.push_str(&sideline.after_ply.to_string());
        out.push_str(",\"moves\":[");
        for (move_index, played) in sideline.moves.iter().enumerate() {
            if move_index > 0 {
                out.push(',');
            }
            out.push_str("{\"uci\":");
            json::write_string(&mut out, &played.uci);
            out.push_str(",\"san\":");
            json::write_string(&mut out, &played.san);
            out.push('}');
        }
        out.push_str("]}");
    }
    out.push_str("],\"pinnedLines\":[");
    for (index, line) in data.pinned_lines.iter().enumerate() {
        if index > 0 {
            out.push(',');
        }
        out.push_str("{\"positionFen\":");
        json::write_string(&mut out, &line.position_fen);
        out.push_str(",\"evaluation\":");
        json::write_string(&mut out, &line.evaluation);
        out.push_str(",\"variation\":");
        json::write_string(&mut out, &line.variation);
        out.push_str(",\"engine\":");
        json::write_string(&mut out, &line.engine);
        out.push_str(",\"searchContext\":");
        json::write_string(&mut out, &line.search_context);
        out.push('}');
    }
    out.push_str("],\"computerEvaluations\":[");
    for (index, evaluation) in data.computer_evaluations.iter().enumerate() {
        if index > 0 {
            out.push(',');
        }
        out.push_str("{\"ply\":");
        out.push_str(&evaluation.ply.to_string());
        out.push_str(",\"positionFen\":");
        json::write_string(&mut out, &evaluation.position_fen);
        out.push_str(",\"evaluation\":");
        json::write_string(&mut out, &evaluation.evaluation);
        out.push_str(",\"glyph\":");
        json::write_string(&mut out, &evaluation.glyph);
        out.push_str(",\"betterLine\":");
        match &evaluation.better_line {
            Some(line) => json::write_string(&mut out, line),
            None => out.push_str("null"),
        }
        out.push('}');
    }
    out.push_str("],\"computerAnalysisComplete\":");
    out.push_str(if data.computer_analysis_complete { "true" } else { "false" });
    out.push_str(",\"defaultAnalysis\":");
    out.push_str(if data.default_analysis { "true" } else { "false" });
    out.push('}');
    out
}

fn record_graph_changed_event(sources: &[String], derivations: &[String]) -> String {
    let mut out = String::from("{\"type\":\"record_graph_changed\",\"sources\":[");
    for (index, id) in sources.iter().enumerate() {
        if index > 0 {
            out.push(',');
        }
        json::write_string(&mut out, id);
    }
    out.push_str("],\"derivations\":[");
    for (index, id) in derivations.iter().enumerate() {
        if index > 0 {
            out.push(',');
        }
        json::write_string(&mut out, id);
    }
    out.push_str("]}");
    out
}

fn import_results_event(results: &[ImportReport]) -> String {
    let mut out = String::from("{\"type\":\"pgn_import_results\",\"entries\":[");
    for (index, result) in results.iter().enumerate() {
        if index > 0 {
            out.push(',');
        }
        let (entry, title, id, reason) = match result {
            ImportReport::Imported { entry, title, id } => {
                (*entry, title.as_str(), Some(id.as_str()), None)
            }
            ImportReport::Failed(failure) => (
                failure.entry,
                failure.title.as_str(),
                None,
                Some(failure.reason.as_str()),
            ),
        };
        out.push_str("{\"entry\":");
        out.push_str(&entry.to_string());
        out.push_str(",\"title\":");
        json::write_string(&mut out, title);
        out.push_str(",\"status\":");
        json::write_string(&mut out, if id.is_some() { "imported" } else { "failed" });
        if let Some(id) = id {
            out.push_str(",\"id\":");
            json::write_string(&mut out, id);
        }
        if let Some(reason) = reason {
            out.push_str(",\"reason\":");
            json::write_string(&mut out, reason);
        }
        out.push('}');
    }
    out.push_str("]}");
    out
}

fn pgn_export_ready_event(pgn: &str) -> String {
    let mut out = String::from("{\"type\":\"pgn_export_ready\",\"pgn\":");
    json::write_string(&mut out, pgn);
    out.push('}');
    out
}

fn set_pgn_tag(tags: &mut Vec<(String, String)>, name: &str, value: &str) {
    if value.is_empty() {
        return;
    }
    if let Some((_, existing)) = tags.iter_mut().find(|(key, _)| key == name) {
        *existing = value.to_owned();
    } else {
        tags.push((name.to_owned(), value.to_owned()));
    }
}

fn value_or_unknown(value: &str) -> &str {
    if value.is_empty() {
        "?"
    } else {
        value
    }
}

fn existing_tag_or<'a>(tags: &'a [(String, String)], name: &str, fallback: &'a str) -> &'a str {
    tags.iter()
        .find(|(key, _)| key == name)
        .map(|(_, value)| value.as_str())
        .unwrap_or(fallback)
}

fn tabs_changed_event(open_tabs: &[String], active_id: Option<&str>, titles: &[String]) -> String {
    let mut out = String::from("{\"type\":\"tabs_changed\",\"openTabs\":[");
    for (index, id) in open_tabs.iter().enumerate() {
        if index > 0 {
            out.push(',');
        }
        out.push_str("{\"id\":");
        json::write_string(&mut out, id);
        out.push_str(",\"title\":");
        let title = titles
            .get(index)
            .map(String::as_str)
            .unwrap_or("Game Record");
        json::write_string(&mut out, title);
        out.push('}');
    }
    out.push_str("],\"activeId\":");
    match active_id {
        Some(id) => json::write_string(&mut out, id),
        None => out.push_str("null"),
    }
    out.push('}');
    out
}

fn encode_tab_ids(ids: &[String]) -> String {
    ids.join(",")
}

fn decode_tab_ids(encoded: &str) -> Vec<String> {
    if encoded.is_empty() {
        return Vec::new();
    }
    encoded
        .split(',')
        .filter(|part| !part.is_empty())
        .map(str::to_owned)
        .collect()
}

fn default_record_title(record: &GameRecordSummary) -> String {
    default_title_for(record.kind, record.ply_count)
}

fn default_title_for(kind: GameRecordKind, ply_count: u32) -> String {
    match kind {
        GameRecordKind::Played => {
            if ply_count == 0 {
                "Played Game".into()
            } else if ply_count == 1 {
                "Played Game · 1 move".into()
            } else {
                format!("Played Game · {ply_count} moves")
            }
        }
        GameRecordKind::Analysis => "Analysis Record".into(),
    }
}

fn new_record_id() -> String {
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_nanos())
        .unwrap_or(0);
    format!("gr_{nanos}")
}

fn timestamp_now() -> String {
    let secs = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_secs())
        .unwrap_or(0);
    format!("{secs}")
}

/// The stable status identifier a workspace switches on.
fn status_name(winner: Winner) -> &'static str {
    match winner {
        Winner::White => "white_wins",
        Winner::Black => "black_wins",
        Winner::Draw => "draw",
        Winner::None => "playing",
    }
}

impl Default for Session {
    fn default() -> Self {
        Session::new()
    }
}

impl Drop for Session {
    fn drop(&mut self) {
        let _ = self.apply_elapsed_clock();
        if self.record_id.is_some() && !self.game.outcome().is_over() && !self.has_unsaved_changes()
        {
            let _ = self.persist_current_record();
        }
        let _ = self.persist_residue();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The board as the core last described it, with nothing changed.
    fn describe(session: &mut Session) -> String {
        session.submit(r#"{"type":"describe_board"}"#).unwrap();
        let mut board = None;
        while let Some(event) = session.poll_event() {
            if event.contains(r#""type":"board_changed""#) {
                board = Some(event);
            }
        }
        board.expect("describing the board always answers")
    }

    fn play(session: &mut Session, from: &str, to: &str) {
        let command = format!(r#"{{"type":"play_move","from":"{from}","to":"{to}"}}"#);
        session
            .submit(&command)
            .unwrap_or_else(|error| panic!("{from}{to}: {error:?}"));
        while session.poll_event().is_some() {}
    }

    #[test]
    fn compiling_a_variant_definition_is_byte_identical() {
        let definition = VariantDefinition::default();
        let first = compile_variant_adapter(&definition);
        let second = compile_variant_adapter(&definition);
        assert_eq!(first, second);
        assert!(first.starts_with(b"[omachess:chess]\nmaxFile = 8\n"));
    }

    #[test]
    fn describing_the_board_answers_with_the_starting_position() {
        let mut session = Session::new();
        session.submit(r#"{"type":"describe_board"}"#).unwrap();
        let event = session.poll_event().unwrap();
        assert!(event.contains(r#""type":"board_changed""#));
        assert!(event.contains(r#""orientation":"white""#));
        assert!(event.contains(r#"{"name":"a8","light":true,"piece":"black_rook"}"#));
        assert!(event.contains(r#"{"name":"e1","light":false,"piece":"white_king"}"#));
        assert!(event.contains(r#""sideToMove":"white""#));
        assert!(event.contains(r#""moveList":[]"#));
        assert!(event.contains(r#""lastMove":null"#));
        assert!(event.contains(r#""status":"playing""#));
        // An ephemeral session has no Live Store, so no library or tab events.
        assert!(session.poll_event().is_none());
    }

    #[test]
    fn flipping_answers_with_a_reoriented_board() {
        let mut session = Session::new();
        session.submit(r#"{"type":"flip_board"}"#).unwrap();
        let event = session.poll_event().unwrap();
        assert!(event.contains(r#""orientation":"black""#));
        assert!(event.starts_with(
            r#"{"type":"board_changed","variant":"standard","orientation":"black","squares":[{"name":"h1","#
        ));
    }

    #[test]
    fn flipping_twice_returns_to_white_at_the_bottom() {
        let mut session = Session::new();
        session.submit(r#"{"type":"flip_board"}"#).unwrap();
        session.submit(r#"{"type":"flip_board"}"#).unwrap();
        session.poll_event().unwrap();
        assert!(session
            .poll_event()
            .unwrap()
            .contains(r#""orientation":"white""#));
    }

    #[test]
    fn rejected_commands_queue_no_events() {
        let mut session = Session::new();
        assert_eq!(
            session.submit(r#"{"type":"resign"}"#),
            Err(CommandError::UnknownCommand)
        );
        assert_eq!(session.submit("{}"), Err(CommandError::MalformedCommand));
        assert!(session.poll_event().is_none());
    }

    #[test]
    fn the_moves_a_player_may_make_arrive_with_the_board() {
        let mut session = Session::new();
        let event = describe(&mut session);
        assert!(event.contains(r#"{"from":"e2","to":"e4","promotions":[]}"#));
        // A pawn cannot reach e5 from the starting position, so it is not
        // offered as a destination anywhere.
        assert!(!event.contains(r#""to":"e5""#));
    }

    #[test]
    fn playing_a_move_answers_with_the_new_board_and_its_san() {
        let mut session = Session::new();
        session
            .submit(r#"{"type":"play_move","from":"e2","to":"e4"}"#)
            .unwrap();
        let event = session.poll_event().unwrap();
        assert!(event.contains(r#"{"name":"e4","light":true,"piece":"white_pawn"}"#));
        assert!(event.contains(r#"{"name":"e2","light":true,"piece":null}"#));
        assert!(
            event.contains(r#""moveList":[{"number":1,"side":"white","san":"e4","uci":"e2e4"}]"#)
        );
        assert!(event.contains(r#""lastMove":{"from":"e2","to":"e4"}"#));
        assert!(event.contains(r#""sideToMove":"black""#));
    }

    #[test]
    fn an_illegal_move_is_rejected_and_queues_no_event() {
        let mut session = Session::new();
        assert_eq!(
            session.submit(r#"{"type":"play_move","from":"e2","to":"e5"}"#),
            Err(CommandError::RejectedMove)
        );
        assert!(session.poll_event().is_none());
        assert_eq!(
            session.submit(r#"{"type":"play_move","from":"e2"}"#),
            Err(CommandError::MalformedCommand)
        );
        assert!(session.poll_event().is_none());
    }

    #[test]
    fn a_promotion_offers_the_pieces_a_pawn_may_become() {
        let mut session = Session::new();
        // A short line that walks a white pawn to the seventh rank.
        for (from, to) in [
            ("g2", "g4"),
            ("h7", "h5"),
            ("g4", "h5"),
            ("g7", "g6"),
            ("h5", "g6"),
            ("g8", "f6"),
            ("g6", "g7"),
            ("d7", "d5"),
        ] {
            play(&mut session, from, to);
        }
        let event = describe(&mut session);
        assert!(event.contains(
            r#"{"from":"g7","to":"h8","promotions":["queen","rook","bishop","knight"]}"#
        ));

        session
            .submit(r#"{"type":"play_move","from":"g7","to":"h8","promotion":"rook"}"#)
            .unwrap();
        let event = session.poll_event().unwrap();
        assert!(event.contains(r#""san":"gxh8=R""#));
        assert!(event.contains(r#"{"name":"h8","light":false,"piece":"white_rook"}"#));
    }

    #[test]
    fn navigating_changes_the_displayed_position_without_losing_moves() {
        let mut session = Session::new();
        play(&mut session, "e2", "e4");
        play(&mut session, "e7", "e5");

        session
            .submit(r#"{"type":"navigate","to":"backward"}"#)
            .unwrap();
        let event = session.poll_event().unwrap();
        assert!(event.contains(r#"{"name":"e5","light":false,"piece":null}"#));
        assert!(event.contains(r#""cursor":1"#));
        assert!(event.contains(r#""reviewing":true"#));
        // The move list is the record; navigating only changes where we look.
        assert!(event.contains(r#""san":"e5""#));
        // No move may be played from a position being reviewed.
        assert!(event.contains(r#""moves":[]"#));

        session
            .submit(r#"{"type":"navigate","to":"start"}"#)
            .unwrap();
        let event = session.poll_event().unwrap();
        assert!(event.contains(r#"{"name":"e4","light":true,"piece":null}"#));
        assert!(event.contains(r#""cursor":0"#));

        session.submit(r#"{"type":"navigate","to":"end"}"#).unwrap();
        let event = session.poll_event().unwrap();
        assert!(event.contains(r#"{"name":"e5","light":false,"piece":"black_pawn"}"#));
        assert!(event.contains(r#""reviewing":false"#));
        assert!(event.contains(r#"{"from":"g1","to":"f3","promotions":[]}"#));
    }

    #[test]
    fn navigating_nowhere_in_particular_is_refused() {
        let mut session = Session::new();
        assert_eq!(
            session.submit(r#"{"type":"navigate","to":"sideways"}"#),
            Err(CommandError::MalformedCommand)
        );
        assert_eq!(
            session.submit(r#"{"type":"navigate"}"#),
            Err(CommandError::MalformedCommand)
        );
    }

    #[test]
    fn a_checkmate_is_reported_as_a_result() {
        let mut session = Session::new();
        for (from, to) in [("f2", "f3"), ("e7", "e5"), ("g2", "g4"), ("d8", "h4")] {
            play(&mut session, from, to);
        }
        let event = describe(&mut session);
        assert!(event.contains(r#""status":"black_wins""#));
        assert!(event.contains(r#""termination":"checkmate""#));
        assert!(event.contains(r#""score":"0-1""#));
        assert!(event.contains(r#""label":"Black wins by checkmate""#));
        assert!(event.contains(r#""over":true"#));
        // A finished game accepts no more moves.
        assert_eq!(
            session.submit(r#"{"type":"play_move","from":"e1","to":"f2"}"#),
            Err(CommandError::RejectedMove)
        );
    }

    #[test]
    fn a_timed_game_flags_the_side_to_move_and_persists_the_completed_game() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");
        let record_id;
        {
            let mut session = Session::open(&path).unwrap();
            session
                .submit(r#"{"type":"configure_clock","milliseconds":"5"}"#)
                .unwrap();
            play(&mut session, "e2", "e4");
            std::thread::sleep(std::time::Duration::from_millis(10));
            session.submit(r#"{"type":"tick_clock"}"#).unwrap();
            let event = session
                .events
                .iter()
                .find(|event| event.contains(r#""type":"board_changed""#))
                .unwrap();
            assert!(event.contains(r#""termination":"time_forfeit""#));
            assert!(event.contains(r#""score":"1-0""#));
            assert_eq!(
                session.submit(r#"{"type":"play_move","from":"e7","to":"e5"}"#),
                Err(CommandError::RejectedMove)
            );
            record_id = session.record_id.clone().unwrap();
        }

        let mut reopened = Session::open(&path).unwrap();
        reopened
            .submit(&format!(r#"{{"type":"open_record","id":"{record_id}"}}"#))
            .unwrap();
        let event = reopened
            .events
            .iter()
            .find(|event| event.contains(r#""type":"board_changed""#))
            .unwrap();
        assert!(event.contains(r#""termination":"time_forfeit""#));
        assert!(event.contains(r#""whiteMs":"#));
    }

    #[test]
    fn completed_game_metadata_remains_correctable_without_changing_moves() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");
        let mut session = Session::open(&path).unwrap();
        for (from, to) in [("f2", "f3"), ("e7", "e5"), ("g2", "g4"), ("d8", "h4")] {
            play(&mut session, from, to);
        }
        let immutable_moves = session.game.moves().to_vec();
        session
            .submit(
                r#"{"type":"update_metadata","white":"Ada","black":"Grace","event":"Club","date":"2026-07-27","title":"Corrected title","tags":"casual"}"#,
            )
            .unwrap();
        assert_eq!(session.game.moves(), immutable_moves);
        assert_eq!(session.metadata.title, "Corrected title");
        assert_eq!(
            session.submit(r#"{"type":"play_move","from":"e1","to":"f2"}"#),
            Err(CommandError::RejectedMove)
        );
        let record = session
            .store
            .as_ref()
            .unwrap()
            .workspace()
            .get_game_record(session.record_id.as_deref().unwrap())
            .unwrap()
            .unwrap();
        assert_eq!(record.title.as_deref(), Some("Corrected title"));
        assert_eq!(record.payload.moves.len(), 4);
        assert_eq!(record.payload.result.unwrap().score, "0-1");

        session.submit(r#"{"type":"new_game"}"#).unwrap();
        assert!(session.clock.is_none());
        assert_eq!(session.metadata.title, "");
        assert_eq!(session.metadata.white, "");
    }

    #[test]
    fn an_unfinished_played_game_is_restored_suspended_after_restart() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");

        {
            let mut session = Session::open(&path).unwrap();
            session
                .submit(r#"{"type":"configure_clock","milliseconds":"60000"}"#)
                .unwrap();
            play(&mut session, "e2", "e4");
            play(&mut session, "e7", "e5");
            play(&mut session, "g1", "f3");
            // Closing the session is the restart boundary: residue is written
            // when the session ends.
            drop(session);
        }

        let mut session = Session::open(&path).unwrap();
        session.submit(r#"{"type":"describe_board"}"#).unwrap();
        let mut restore = None;
        while let Some(event) = session.poll_event() {
            if event.contains(r#""type":"restore_available""#) {
                restore = Some(event);
            }
        }
        let restore = restore.expect("unfinished work is offered for restore");
        assert!(restore.contains("Restore suspended Played Game · 3 moves"));

        session.submit(r#"{"type":"restore_record"}"#).unwrap();
        let event = describe(&mut session);
        assert!(event.contains(r#""san":"e4"#));
        assert!(event.contains(r#""san":"e5"#));
        assert!(event.contains(r#""san":"Nf3"#));
        assert!(event.contains(r#""cursor":3"#));
        assert!(event.contains(r#""piece":"white_knight""#) && event.contains(r#""name":"f3""#));
        assert!(event.contains(r#""suspended":true"#));
        assert!(event.contains(r#""running":false"#));

        assert_eq!(
            session.submit(r#"{"type":"play_move","from":"b8","to":"c6"}"#),
            Err(CommandError::RejectedMove)
        );
        session.submit(r#"{"type":"resume_game"}"#).unwrap();
        let event = describe(&mut session);
        assert!(event.contains(r#""suspended":false"#));
        assert!(event.contains(r#""running":true"#));
        play(&mut session, "b8", "c6");
    }

    #[test]
    fn a_played_game_can_only_be_suspended_at_its_latest_position() {
        let mut session = Session::new();
        play(&mut session, "e2", "e4");
        session
            .submit(r#"{"type":"navigate","to":"start"}"#)
            .unwrap();
        let event = describe(&mut session);
        assert!(event.contains(r#""canSuspend":false"#));
        assert_eq!(
            session.submit(r#"{"type":"suspend_game"}"#),
            Err(CommandError::RejectedMove)
        );

        session.submit(r#"{"type":"navigate","to":"end"}"#).unwrap();
        let event = describe(&mut session);
        assert!(event.contains(r#""canSuspend":true"#));
    }

    #[test]
    fn manual_save_mode_keeps_changes_dirty_until_saved_and_discard_restores_snapshot() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");
        let mut session = Session::open(&path).unwrap();

        session
            .submit(r#"{"type":"set_save_mode","mode":"manual"}"#)
            .unwrap();
        play(&mut session, "e2", "e4");
        let dirty = describe(&mut session);
        assert!(dirty.contains(r#""saveMode":"manual""#));
        assert!(dirty.contains(r#""dirty":true"#));
        assert_eq!(
            session.submit(r#"{"type":"new_game"}"#),
            Err(CommandError::RejectedMove)
        );
        assert_eq!(
            session.submit(r#"{"type":"open_record","id":"another"}"#),
            Err(CommandError::RejectedMove)
        );

        session.submit(r#"{"type":"discard_changes"}"#).unwrap();
        let discarded = describe(&mut session);
        assert!(discarded.contains(r#""moveList":[]"#));
        assert!(discarded.contains(r#""dirty":false"#));

        play(&mut session, "d2", "d4");
        session.submit(r#"{"type":"save_record"}"#).unwrap();
        let saved = describe(&mut session);
        assert!(saved.contains(r#""san":"d4"#));
        assert!(saved.contains(r#""dirty":false"#));
    }

    #[test]
    fn only_a_dirty_game_record_in_manual_save_mode_requires_an_unsaved_close_decision() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");
        let mut session = Session::open(&path).unwrap();

        play(&mut session, "e2", "e4");
        assert!(!describe(&mut session).contains(r#""dirty":true"#));

        session
            .submit(r#"{"type":"set_save_mode","mode":"manual"}"#)
            .unwrap();
        play(&mut session, "e7", "e5");
        let board = describe(&mut session);
        assert!(board.contains(r#""dirty":true"#));
        assert!(board.contains(r#""needsUnsavedDecision":true"#));
    }

    #[test]
    fn describing_the_board_lists_persisted_library_records() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");

        {
            let mut session = Session::open(&path).unwrap();
            play(&mut session, "e2", "e4");
            play(&mut session, "e7", "e5");
            drop(session);
        }

        let mut session = Session::open(&path).unwrap();
        session.submit(r#"{"type":"describe_board"}"#).unwrap();
        let mut library = None;
        while let Some(event) = session.poll_event() {
            if event.contains(r#""type":"library_changed""#) {
                library = Some(event);
            }
        }
        let event = library.expect("describe_board lists the Personal Library");
        assert!(event.contains(r#""kind":"played""#));
        assert!(event.contains(r#""plyCount":2"#));
        assert!(event.contains(r#""id":"gr_"#));
    }

    #[test]
    fn opening_a_library_record_opens_it_in_a_tab_and_loads_its_board() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");

        let (first_id, second_id) = {
            let mut session = Session::open(&path).unwrap();
            play(&mut session, "e2", "e4");
            play(&mut session, "e7", "e5");
            session.submit(r#"{"type":"describe_board"}"#).unwrap();
            let mut first_library = String::new();
            while let Some(event) = session.poll_event() {
                if event.contains(r#""type":"library_changed""#) {
                    first_library = event;
                }
            }
            let first_id = library_ids(&first_library)
                .into_iter()
                .next()
                .expect("the first Game Record is listed");

            // Start a second Game Record so the library has more than one entry.
            session.submit(r#"{"type":"new_game"}"#).unwrap();
            while session.poll_event().is_some() {}
            play(&mut session, "d2", "d4");
            session.submit(r#"{"type":"describe_board"}"#).unwrap();
            let mut second_library = String::new();
            while let Some(event) = session.poll_event() {
                if event.contains(r#""type":"library_changed""#) {
                    second_library = event;
                }
            }
            let ids = library_ids(&second_library);
            assert_eq!(
                ids.len(),
                2,
                "library should hold both Game Records: {second_library}"
            );
            let second_id = ids
                .into_iter()
                .find(|id| id != &first_id)
                .expect("the second Game Record is listed");
            drop(session);
            (first_id, second_id)
        };

        let mut session = Session::open(&path).unwrap();
        let command = format!(r#"{{"type":"open_record","id":"{first_id}"}}"#);
        session.submit(&command).unwrap();
        let mut board = None;
        let mut tabs = None;
        while let Some(event) = session.poll_event() {
            if event.contains(r#""type":"board_changed""#) {
                board = Some(event);
            } else if event.contains(r#""type":"tabs_changed""#) {
                tabs = Some(event);
            }
        }
        let tabs = tabs.expect("opening a record emits tabs_changed");
        assert!(tabs.contains(&format!(r#""id":"{first_id}""#)));
        assert!(tabs.contains(&format!(r#""activeId":"{first_id}""#)));
        let board = board.expect("opening a record loads its board");
        assert!(board.contains(r#""san":"e4"#));
        assert!(board.contains(r#""san":"e5"#));
        assert!(!board.contains(r#""san":"d4"#));
        // The second record remains available to open; this assertion keeps the
        // ids live so the fixture is clearly two distinct Game Records.
        assert_ne!(first_id, second_id);
    }

    #[test]
    fn switching_tabs_changes_the_board_and_closing_leaves_the_library_intact() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");

        let (first_id, second_id) = two_played_games(&path);

        let mut session = Session::open(&path).unwrap();
        session
            .submit(&format!(r#"{{"type":"open_record","id":"{first_id}"}}"#))
            .unwrap();
        while session.poll_event().is_some() {}
        session
            .submit(&format!(r#"{{"type":"open_record","id":"{second_id}"}}"#))
            .unwrap();
        while session.poll_event().is_some() {}

        // Switch back to the first tab — board and active tab move together.
        session
            .submit(&format!(r#"{{"type":"open_record","id":"{first_id}"}}"#))
            .unwrap();
        let mut board = None;
        let mut tabs = None;
        while let Some(event) = session.poll_event() {
            if event.contains(r#""type":"board_changed""#) {
                board = Some(event);
            } else if event.contains(r#""type":"tabs_changed""#) {
                tabs = Some(event);
            }
        }
        let tabs = tabs.expect("switching tabs emits tabs_changed");
        assert!(tabs.contains(&format!(r#""activeId":"{first_id}""#)));
        assert!(tabs.contains(&format!(r#""id":"{second_id}""#)));
        let board = board.expect("switching tabs loads that record's board");
        assert!(board.contains(r#""san":"e4"#));
        assert!(board.contains(r#""san":"e5"#));
        assert!(!board.contains(r#""san":"d4"#));

        // Close the first tab; the record stays in the Personal Library.
        session
            .submit(&format!(r#"{{"type":"close_tab","id":"{first_id}"}}"#))
            .unwrap();
        let mut tabs = None;
        let mut library = None;
        while let Some(event) = session.poll_event() {
            if event.contains(r#""type":"tabs_changed""#) {
                tabs = Some(event);
            } else if event.contains(r#""type":"library_changed""#) {
                library = Some(event);
            }
        }
        let tabs = tabs.expect("closing a tab emits tabs_changed");
        assert!(!tabs.contains(&format!(r#""id":"{first_id}""#)));
        assert!(tabs.contains(&format!(r#""id":"{second_id}""#)));
        assert!(tabs.contains(&format!(r#""activeId":"{second_id}""#)));

        session.submit(r#"{"type":"describe_board"}"#).unwrap();
        while let Some(event) = session.poll_event() {
            if event.contains(r#""type":"library_changed""#) {
                library = Some(event);
            }
        }
        let library = library.expect("the Personal Library is still listed");
        let ids = library_ids(&library);
        assert!(
            ids.contains(&first_id),
            "closed tab's record stays in the library: {library}"
        );
        assert!(
            ids.contains(&second_id),
            "open tab's record stays in the library: {library}"
        );
    }

    #[test]
    fn open_tabs_survive_restart() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");
        let (first_id, second_id) = two_played_games(&path);

        {
            let mut session = Session::open(&path).unwrap();
            session
                .submit(&format!(r#"{{"type":"open_record","id":"{first_id}"}}"#))
                .unwrap();
            while session.poll_event().is_some() {}
            session
                .submit(&format!(r#"{{"type":"open_record","id":"{second_id}"}}"#))
                .unwrap();
            while session.poll_event().is_some() {}
            session
                .submit(&format!(r#"{{"type":"close_tab","id":"{first_id}"}}"#))
                .unwrap();
            while session.poll_event().is_some() {}
            drop(session);
        }

        let mut session = Session::open(&path).unwrap();
        session.submit(r#"{"type":"describe_board"}"#).unwrap();
        let mut tabs = None;
        let mut library = None;
        while let Some(event) = session.poll_event() {
            if event.contains(r#""type":"tabs_changed""#) {
                tabs = Some(event);
            } else if event.contains(r#""type":"library_changed""#) {
                library = Some(event);
            }
        }
        let library = library.expect("library lists both Game Records after restart");
        let ids = library_ids(&library);
        assert!(ids.contains(&first_id));
        assert!(ids.contains(&second_id));
        let tabs = tabs.expect("open tabs are restored after restart");
        assert!(!tabs.contains(&format!(r#""id":"{first_id}""#)));
        assert!(tabs.contains(&format!(r#""id":"{second_id}""#)));
        assert!(tabs.contains(r#""activeId":null"#));

        session.submit(r#"{"type":"restore_record"}"#).unwrap();
        let mut restored_tabs = None;
        while let Some(event) = session.poll_event() {
            if event.contains(r#""type":"tabs_changed""#) {
                restored_tabs = Some(event);
            }
        }
        let restored_tabs = restored_tabs.expect("restore reactivates the suspended tab");
        assert!(restored_tabs.contains(&format!(r#""activeId":"{second_id}""#)));
    }

    #[test]
    fn starting_a_new_game_clears_the_active_tab_without_closing_open_tabs() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");
        let (first_id, _) = two_played_games(&path);

        let mut session = Session::open(&path).unwrap();
        session
            .submit(&format!(r#"{{"type":"open_record","id":"{first_id}"}}"#))
            .unwrap();
        while session.poll_event().is_some() {}
        session.submit(r#"{"type":"new_game"}"#).unwrap();
        let mut tabs = None;
        let mut board = None;
        while let Some(event) = session.poll_event() {
            if event.contains(r#""type":"tabs_changed""#) {
                tabs = Some(event);
            } else if event.contains(r#""type":"board_changed""#) {
                board = Some(event);
            }
        }
        let tabs = tabs.expect("new_game clears the active tab highlight");
        assert!(tabs.contains(&format!(r#""id":"{first_id}""#)));
        assert!(tabs.contains(r#""activeId":null"#));
        let board = board.expect("new_game clears the board");
        assert!(board.contains(r#""moveList":[]"#));
    }

    #[test]
    fn deriving_twice_creates_independent_analysis_records_with_navigable_provenance() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");
        let mut session = Session::open(&path).unwrap();
        for uci in ["f2f3", "e7e5", "g2g4", "d8h4"] {
            play(&mut session, &uci[..2], &uci[2..4]);
        }
        let source_id = session.record_id.clone().unwrap();

        session.submit(r#"{"type":"derive_analysis_record"}"#).unwrap();
        while session.poll_event().is_some() {}
        let first_id = session.record_id.clone().unwrap();
        session
            .submit(r#"{"type":"add_analysis_annotation","ply":"2","text":"First only"}"#)
            .unwrap();
        while session.poll_event().is_some() {}
        session
            .submit(r#"{"type":"add_analysis_sideline","after_ply":"2","variation":"b1c3"}"#)
            .unwrap();
        while session.poll_event().is_some() {}
        session
            .submit(&format!(r#"{{"type":"open_record","id":"{source_id}"}}"#))
            .unwrap();
        while session.poll_event().is_some() {}
        session.submit(r#"{"type":"derive_analysis_record"}"#).unwrap();
        while session.poll_event().is_some() {}
        let second_id = session.record_id.clone().unwrap();
        drop(session);

        let store = LiveStore::open(&path).unwrap();
        assert_ne!(first_id, second_id);
        assert_eq!(
            store.workspace().derivations_from(&source_id).unwrap().len(),
            2
        );
        assert_eq!(
            store.workspace().sources_of(&first_id).unwrap(),
            vec![source_id]
        );
        let first = store
            .workspace()
            .analysis_record(&first_id)
            .unwrap()
            .unwrap();
        assert_eq!(first.annotations[0].text, "First only");
        assert_eq!(first.sidelines[0].moves[0].san, "Nc3");
        assert!(store
            .workspace()
            .analysis_record(&second_id)
            .unwrap()
            .unwrap()
            .annotations
            .is_empty());
    }

    #[test]
    fn a_pinned_engine_line_and_its_context_are_emitted_after_restart() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");
        {
            let mut session = Session::open(&path).unwrap();
            for uci in ["f2f3", "e7e5", "g2g4", "d8h4"] {
                play(&mut session, &uci[..2], &uci[2..4]);
            }
            session.submit(r#"{"type":"derive_analysis_record"}"#).unwrap();
            while session.poll_event().is_some() {}
            session.submit(r#"{"type":"pin_engine_line","position_fen":"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1","evaluation":"+0.22","variation":"e2e4 e7e5","engine":"Stockfish 18","search_context":"depth 8 · movetime 250 ms"}"#).unwrap();
            while session.poll_event().is_some() {}
        }

        let mut session = Session::open(&path).unwrap();
        session.submit(r#"{"type":"describe_board"}"#).unwrap();
        let events: Vec<_> = std::iter::from_fn(|| session.poll_event()).collect();
        let analysis = events
            .iter()
            .find(|event| event.contains(r#""type":"analysis_record_changed""#))
            .expect("the active Analysis Record is described after restart");
        assert_eq!(
            crate::json::read_string_field(analysis, "type").as_deref(),
            Some("analysis_record_changed")
        );
        assert!(analysis.contains(r#""evaluation":"+0.22""#));
        assert!(analysis.contains(r#""variation":"e2e4 e7e5""#));
        assert!(analysis.contains(r#""engine":"Stockfish 18""#));
        assert!(analysis.contains(r#""searchContext":"depth 8 · movetime 250 ms""#));
    }

    fn two_played_games(path: &std::path::Path) -> (String, String) {
        let mut session = Session::open(path).unwrap();
        play(&mut session, "e2", "e4");
        play(&mut session, "e7", "e5");
        session.submit(r#"{"type":"describe_board"}"#).unwrap();
        let mut library = String::new();
        while let Some(event) = session.poll_event() {
            if event.contains(r#""type":"library_changed""#) {
                library = event;
            }
        }
        let first_id = library_ids(&library)
            .into_iter()
            .next()
            .expect("the first Game Record is listed");

        session.submit(r#"{"type":"new_game"}"#).unwrap();
        while session.poll_event().is_some() {}
        play(&mut session, "d2", "d4");
        session.submit(r#"{"type":"describe_board"}"#).unwrap();
        let mut library = String::new();
        while let Some(event) = session.poll_event() {
            if event.contains(r#""type":"library_changed""#) {
                library = event;
            }
        }
        let second_id = library_ids(&library)
            .into_iter()
            .find(|id| id != &first_id)
            .expect("the second Game Record is listed");
        drop(session);
        (first_id, second_id)
    }

    fn library_ids(library_event: &str) -> Vec<String> {
        library_event
            .split(r#""id":""#)
            .skip(1)
            .filter_map(|chunk| chunk.split('"').next().map(str::to_owned))
            .collect()
    }
}
