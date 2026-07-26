//! The workspace session: the core-owned state a workspace window shows.
//!
//! A session accepts commands describing player intent and answers with
//! events describing the new state. It never calls back into the workspace;
//! the workspace drains the event queue when it is ready to apply changes.

use crate::board::{Orientation, Position};
use crate::json;

pub struct Session {
    position: Position,
    orientation: Orientation,
    events: Vec<String>,
}

/// Why a command was rejected. Values are part of the C ABI contract.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum CommandError {
    UnknownCommand = 1,
    MalformedCommand = 2,
}

impl Session {
    pub fn new() -> Self {
        Session {
            position: Position::standard_start(),
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

    fn board_changed_event(&self) -> String {
        let mut out = String::with_capacity(2048);
        out.push_str("{\"type\":\"board_changed\",\"variant\":\"standard\",\"orientation\":");
        json::write_string(&mut out, self.orientation.name());
        out.push_str(",\"squares\":[");
        for (index, square) in self.position.rendered(self.orientation).iter().enumerate() {
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
        out.push_str("]}");
        out
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

    #[test]
    fn describing_the_board_answers_with_the_starting_position() {
        let mut session = Session::new();
        session.submit(r#"{"type":"describe_board"}"#).unwrap();
        let event = session.poll_event().unwrap();
        assert!(event.contains(r#""type":"board_changed""#));
        assert!(event.contains(r#""orientation":"white""#));
        assert!(event.contains(r#"{"name":"a8","light":true,"piece":"black_rook"}"#));
        assert!(event.contains(r#"{"name":"e1","light":false,"piece":"white_king"}"#));
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
}
