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

use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};
use std::time::Instant;

use omachess_store::{
    GameRecord, GameRecordKind, GameRecordPayload, GameRecordSummary, LiveStore, MoveEntry,
    OpenError, RecordResult,
};

use crate::board::{Orientation, Piece, Position};
use crate::game::{result_label, Destination, Game, MoveRejected, PlayedMove, Side};
use crate::json;
use crate::pgn::{self, ImportEntry, ImportReport, PgnGame};
use crate::rules::Winner;

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
    metadata: GameMetadata,
    setup: Option<PositionSetup>,
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
            metadata: GameMetadata::default(),
            setup: None,
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
                Ok(Some(record)) if record.ply_count > 0 || record.payload.result.is_some() => {
                    // When open tabs already remember this record, the tab
                    // chrome is the restore surface — no separate restore card.
                    if active_id.as_ref() == Some(&record_id) {
                        None
                    } else {
                        Some(RestoreOffer {
                            record_id,
                            ply_count: record.ply_count,
                        })
                    }
                }
                _ => None,
            },
            _ => None,
        };

        let mut session = Session {
            game: Game::standard(),
            orientation: Orientation::WhiteBottom,
            events: Vec::new(),
            store: Some(store),
            record_id: None,
            open_tabs,
            restore_offer,
            clock: None,
            metadata: GameMetadata::default(),
            setup: None,
        };
        // Remembered open tabs restore their active board on open. Clocks and
        // engines stay idle — that remains a later ticket's job.
        if let Some(id) = active_id {
            let _ = session.load_record(&id);
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
            "import_pgn" => self.import_pgn(command)?,
            "export_pgn" => self.export_pgn(command)?,
            _ => return Err(CommandError::UnknownCommand),
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
        }
        Ok(())
    }

    fn import_pgn(&mut self, command: &str) -> Result<(), CommandError> {
        let text = json::read_string_field(command, "pgn").ok_or(CommandError::MalformedCommand)?;
        let Some(store) = self.store.as_ref() else { return Err(CommandError::Store) };
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
                            (outcome.is_over() && outcome.winner.score() == imported.result)
                                .then(|| RecordResult {
                                    status: status_name(outcome.winner).to_owned(),
                                    termination: outcome.termination.name().to_owned(),
                                    score: outcome.winner.score().to_owned(),
                                })
                        });
                    let record = GameRecord {
                        id: id.clone(), kind: GameRecordKind::Played,
                        title: (!title.is_empty()).then_some(title.clone()),
                        result_score: result.as_ref().map(|value| value.score.clone()),
                        ply_count: imported.moves.len() as u32, archived: false,
                        created_at: now.clone(), updated_at: now,
                        payload: GameRecordPayload {
                            variant: "standard".into(), start_fen: imported.start_fen,
                            moves: imported.moves.into_iter().map(|played| MoveEntry {
                                uci: played.uci, san: played.san, number: played.number,
                                side: played.side.into(),
                            }).collect(),
                            result, participation: Some(encode_metadata(&metadata)), clock: None,
                        },
                    };
                    store.workspace().upsert_game_record(&record).map_err(|_| CommandError::Store)?;
                    results.push(ImportReport::Imported { entry: index + 1, title, id });
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
        let Some(store) = self.store.as_ref() else { return Err(CommandError::Store) };
        let mut documents = Vec::new();
        for id in ids.split(',').filter(|id| !id.is_empty()) {
            let record = store.workspace().get_game_record(id)
                .map_err(|_| CommandError::Store)?.ok_or(CommandError::Store)?;
            if record.payload.variant != "standard" { continue; }
            let metadata = decode_metadata(record.payload.participation.as_deref());
            let mut tags = pgn::decode_tags(&metadata.tags);
            let site = existing_tag_or(&tags, "Site", "?").to_owned();
            let round = existing_tag_or(&tags, "Round", "?").to_owned();
            set_pgn_tag(&mut tags, "Event", value_or_unknown(&metadata.event));
            set_pgn_tag(&mut tags, "Site", &site);
            set_pgn_tag(&mut tags, "Date", if metadata.date.is_empty() { "????.??.??" } else { &metadata.date });
            set_pgn_tag(&mut tags, "Round", &round);
            set_pgn_tag(&mut tags, "White", value_or_unknown(&metadata.white));
            set_pgn_tag(&mut tags, "Black", value_or_unknown(&metadata.black));
            let result = record.payload.result.as_ref().map(|value| value.score.as_str())
                .or_else(|| tags.iter().find(|(name, _)| name == "Result").map(|(_, value)| value.as_str()))
                .unwrap_or("*").to_owned();
            set_pgn_tag(&mut tags, "Result", &result);
            if record.payload.start_fen != GameRecordPayload::STANDARD_START {
                set_pgn_tag(&mut tags, "SetUp", "1");
                set_pgn_tag(&mut tags, "FEN", &record.payload.start_fen);
            }
            documents.push(pgn::export(&PgnGame {
                tags, start_fen: record.payload.start_fen,
                moves: record.payload.moves.into_iter().map(|entry| PlayedMove {
                    uci: entry.uci, san: entry.san, number: entry.number,
                    side: if entry.side == "black" { "black" } else { "white" },
                }).collect(),
                result,
            }));
        }
        self.events.push(pgn_export_ready_event(&documents.join("\n")));
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
        self.persist_current_record()?;
        Ok(())
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
        if self.game.outcome().is_over() || self.game.reviewing() {
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
            let loser = if white_to_move { Side::White } else { Side::Black };
            self.game.complete_on_time(loser);
            clock.history.push((clock.white_ms, clock.black_ms));
            clock.last_tick = None;
            self.persist_current_record()?;
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
        self.persist_metadata(&id)
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
        self.ensure_tab_open(&offer.record_id);
        self.restore_offer = None;
        self.persist_residue()?;
        self.events
            .push(String::from("{\"type\":\"restore_cleared\"}"));
        self.emit_tabs_changed();
        Ok(())
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
        // The previous Game Record stays in the Live Store; this only clears
        // the board so the next move starts a new record.
        self.game = Game::standard();
        self.record_id = None;
        self.orientation = Orientation::WhiteBottom;
        self.restore_offer = None;
        self.clock = None;
        self.metadata = GameMetadata::default();
        self.setup = None;
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
            fen_suffix: fen.split_once(' ').map_or("w - - 0 1", |(_, suffix)| suffix).into(),
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
            && (fields[2].chars().any(|c| !"KQkq".contains(c))
                || fields[2].chars().count() > 4)
        {
            setup.error = "FEN castling rights must use K, Q, k, q, or “-”.".into();
            return Ok(());
        }
        if fields[3] != "-"
            && !(fields[3].len() == 2
                && matches!(fields[3].as_bytes()[0], b'a'..=b'h')
                && matches!(fields[3].as_bytes()[1], b'3' | b'6'))
        {
            setup.error = "FEN en-passant target must be a third- or sixth-rank square, or “-”.".into();
            return Ok(());
        }
        if fields[4].parse::<u32>().is_err()
            || fields[5].parse::<u32>().ok().filter(|number| *number > 0).is_none()
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
        Ok(())
    }

    fn open_record(&mut self, command: &str) -> Result<(), CommandError> {
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
        self.game = Game::from_history(&record.payload.start_fen, moves).ok_or(CommandError::Store)?;
        if stored_result.as_ref().is_some_and(|result| result.termination == "time_forfeit") {
            let loser = if stored_clock.as_ref().is_some_and(|clock| clock.white_ms == 0) {
                Side::White
            } else if stored_clock.as_ref().is_some_and(|clock| clock.black_ms == 0)
                || stored_result.as_ref().is_some_and(|result| result.score == "1-0")
            {
                Side::Black
            } else {
                Side::White
            };
            self.game.complete_on_time(loser);
        }
        self.clock = stored_clock;
        self.metadata = decode_metadata(record.payload.participation.as_deref());
        if self.metadata.title.is_empty() {
            self.metadata.title = record.title.clone().unwrap_or_default();
        }
        self.setup = None;
        self.record_id = Some(record.id);
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
        let id = self
            .record_id
            .clone()
            .unwrap_or_else(new_record_id);
        let outcome = self.game.outcome();
        let result = if outcome.is_over() {
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
            clock: self.clock.as_ref().map(encode_clock),
        };
        let created_at = store
            .workspace()
            .get_game_record(&id)
            .ok()
            .flatten()
            .map(|existing| existing.created_at)
            .unwrap_or_else(|| now.clone());
        let record = GameRecord {
            id: id.clone(),
            kind: GameRecordKind::Played,
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
        store.workspace().upsert_game_record(&record).map_err(|_| CommandError::Store)?;
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
            out.push_str(",\"activity\":\"played_game\"");
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
        for (index, offer) in self.game.offers().iter().enumerate() {
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
                    && !self.game.reviewing();
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
    format!("{};{};{};{}", clock.initial_ms, clock.white_ms, clock.black_ms, history)
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
    json::write_string(&mut out, "Restore previous game");
    out.push('}');
    out
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

fn import_results_event(results: &[ImportReport]) -> String {
    let mut out = String::from("{\"type\":\"pgn_import_results\",\"entries\":[");
    for (index, result) in results.iter().enumerate() {
        if index > 0 { out.push(','); }
        let (entry, title, id, reason) = match result {
            ImportReport::Imported { entry, title, id } =>
                (*entry, title.as_str(), Some(id.as_str()), None),
            ImportReport::Failed(failure) =>
                (failure.entry, failure.title.as_str(), None, Some(failure.reason.as_str())),
        };
        out.push_str("{\"entry\":");
        out.push_str(&entry.to_string());
        out.push_str(",\"title\":");
        json::write_string(&mut out, title);
        out.push_str(",\"status\":");
        json::write_string(&mut out, if id.is_some() { "imported" } else { "failed" });
        if let Some(id) = id {
            out.push_str(",\"id\":"); json::write_string(&mut out, id);
        }
        if let Some(reason) = reason {
            out.push_str(",\"reason\":"); json::write_string(&mut out, reason);
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
    if value.is_empty() { return; }
    if let Some((_, existing)) = tags.iter_mut().find(|(key, _)| key == name) {
        *existing = value.to_owned();
    } else {
        tags.push((name.to_owned(), value.to_owned()));
    }
}

fn value_or_unknown(value: &str) -> &str {
    if value.is_empty() { "?" } else { value }
}

fn existing_tag_or<'a>(tags: &'a [(String, String)], name: &str, fallback: &'a str) -> &'a str {
    tags.iter().find(|(key, _)| key == name).map(|(_, value)| value.as_str())
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
        if self.record_id.is_some() && !self.game.outcome().is_over() {
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
        assert!(event.contains(r#""moveList":[{"number":1,"side":"white","san":"e4"}]"#));
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
    fn a_played_game_reloads_from_the_live_store_after_restart() {
        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("live-store.sqlite");

        {
            let mut session = Session::open(&path).unwrap();
            play(&mut session, "e2", "e4");
            play(&mut session, "e7", "e5");
            play(&mut session, "g1", "f3");
            // Closing the session is the restart boundary: residue is written
            // when the session ends.
            drop(session);
        }

        let mut session = Session::open(&path).unwrap();
        session.submit(r#"{"type":"describe_board"}"#).unwrap();
        let mut board = None;
        let mut tabs = None;
        while let Some(event) = session.poll_event() {
            if event.contains(r#""type":"board_changed""#) {
                board = Some(event);
            } else if event.contains(r#""type":"tabs_changed""#) {
                tabs = Some(event);
            }
        }
        // Open tabs restore the active board on restart; the Game Record stays
        // in the tab chrome rather than behind a separate restore card.
        let tabs = tabs.expect("the open tab is restored after restart");
        assert!(tabs.contains(r#""activeId":"gr_"#));
        let event = board.expect("the active tab's board is restored");
        assert!(event.contains(r#""san":"e4"#));
        assert!(event.contains(r#""san":"e5"#));
        assert!(event.contains(r#""san":"Nf3"#));
        assert!(event.contains(r#""cursor":3"#));
        assert!(event.contains(r#""piece":"white_knight""#) && event.contains(r#""name":"f3""#));
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
        assert!(tabs.contains(&format!(r#""activeId":"{second_id}""#)));
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
