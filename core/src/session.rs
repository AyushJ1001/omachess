//! The workspace session: the core-owned state a workspace window shows.
//!
//! A session accepts commands describing player intent and answers with
//! events describing the new state. It never calls back into the workspace;
//! the workspace drains the event queue when it is ready to apply changes.
//!
//! Every chess answer in an event comes from the Played Game, which gets it
//! from the Rules Authority. The session decides nothing about chess: it
//! decides what a workspace needs to be told.

use crate::board::Orientation;
use crate::game::{result_label, Destination, Game, MoveRejected};
use crate::json;
use crate::rules::Winner;

pub struct Session {
    game: Game,
    orientation: Orientation,
    events: Vec<String>,
}

/// Why a command was rejected. Values are part of the C ABI contract.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum CommandError {
    UnknownCommand = 1,
    MalformedCommand = 2,
    /// The player's intent was understood but the game cannot honour it — an
    /// illegal move, or a move in a game that is over or being reviewed.
    RejectedMove = 5,
}

impl Session {
    pub fn new() -> Self {
        Session {
            game: Game::standard(),
            orientation: Orientation::WhiteBottom,
            events: Vec::new(),
        }
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
            _ => return Err(CommandError::UnknownCommand),
        }
        let event = self.board_changed_event();
        self.events.push(event);
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
            })
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

    fn board_changed_event(&mut self) -> String {
        let mut out = String::with_capacity(4096);
        out.push_str("{\"type\":\"board_changed\",\"variant\":\"standard\",\"orientation\":");
        json::write_string(&mut out, self.orientation.name());

        out.push_str(",\"squares\":[");
        let position = self.game.position();
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

        out.push_str(",\"sideToMove\":");
        json::write_string(&mut out, if self.game.white_to_move() { "white" } else { "black" });
        out.push_str(",\"inCheck\":");
        out.push_str(if self.game.in_check() { "true" } else { "false" });

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
        out.push_str(if self.game.reviewing() { "true" } else { "false" });

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

#[cfg(test)]
mod tests {
    use super::*;

    /// The board as the core last described it, with nothing changed.
    fn describe(session: &mut Session) -> String {
        session.submit(r#"{"type":"describe_board"}"#).unwrap();
        let mut latest = None;
        while let Some(event) = session.poll_event() {
            latest = Some(event);
        }
        latest.expect("describing the board always answers")
    }

    fn play(session: &mut Session, from: &str, to: &str) {
        let command = format!(r#"{{"type":"play_move","from":"{from}","to":"{to}"}}"#);
        session.submit(&command).unwrap_or_else(|error| panic!("{from}{to}: {error:?}"));
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
        assert!(session.poll_event().unwrap().contains(r#""orientation":"white""#));
    }

    #[test]
    fn rejected_commands_queue_no_events() {
        let mut session = Session::new();
        assert_eq!(session.submit(r#"{"type":"resign"}"#), Err(CommandError::UnknownCommand));
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
        session.submit(r#"{"type":"play_move","from":"e2","to":"e4"}"#).unwrap();
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

        session.submit(r#"{"type":"navigate","to":"backward"}"#).unwrap();
        let event = session.poll_event().unwrap();
        assert!(event.contains(r#"{"name":"e5","light":false,"piece":null}"#));
        assert!(event.contains(r#""cursor":1"#));
        assert!(event.contains(r#""reviewing":true"#));
        // The move list is the record; navigating only changes where we look.
        assert!(event.contains(r#""san":"e5""#));
        // No move may be played from a position being reviewed.
        assert!(event.contains(r#""moves":[]"#));

        session.submit(r#"{"type":"navigate","to":"start"}"#).unwrap();
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
        assert_eq!(session.submit(r#"{"type":"navigate"}"#), Err(CommandError::MalformedCommand));
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
}
