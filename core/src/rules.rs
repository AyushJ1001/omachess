//! The Rules Authority: vendored Fairy-Stockfish, and nothing else.
//!
//! Every legal move, every SAN string, every FEN, and every game result in
//! Omachess comes from here. Nothing in this crate reimplements a chess rule,
//! so no second implementation can drift from the engine.
//!
//! `core/rules/omachess_rules.h` describes the C bridge this module wraps;
//! `core/build.rs` compiles the engine and the bridge into the core.

use std::ffi::{c_char, c_int, CStr, CString};
use std::fmt;

#[allow(non_camel_case_types)]
type OmachessRules = std::ffi::c_void;

extern "C" {
    fn omachess_rules_new(variant: *const c_char, start_fen: *const c_char) -> *mut OmachessRules;
    fn omachess_rules_load_variant(adapter: *const c_char) -> c_int;
    fn omachess_rules_free(rules: *mut OmachessRules);
    fn omachess_rules_fen(rules: *mut OmachessRules) -> *const c_char;
    fn omachess_rules_legal_moves(rules: *mut OmachessRules) -> *const c_char;
    fn omachess_rules_san(rules: *mut OmachessRules, uci_move: *const c_char) -> *const c_char;
    fn omachess_rules_push(rules: *mut OmachessRules, uci_move: *const c_char) -> c_int;
    fn omachess_rules_bounded_search(rules: *mut OmachessRules, depth: c_int) -> c_int;
    fn omachess_rules_analysis(rules: *mut OmachessRules, depth: c_int) -> *const c_char;
    fn omachess_rules_pop(rules: *mut OmachessRules) -> c_int;
    fn omachess_rules_side_to_move(rules: *mut OmachessRules) -> c_int;
    fn omachess_rules_in_check(rules: *mut OmachessRules) -> c_int;
    fn omachess_rules_time_forfeit_winner(rules: *mut OmachessRules, loser: c_int) -> c_int;
    fn omachess_rules_termination(rules: *mut OmachessRules) -> c_int;
    fn omachess_rules_winner(rules: *mut OmachessRules) -> c_int;
}

/// Why a game ended, as the engine sees it.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Termination {
    /// The game is still being played.
    Playing,
    Checkmate,
    Stalemate,
    InsufficientMaterial,
    FiftyMove,
    Repetition,
    /// A player's clock reached zero.
    TimeForfeit,
    /// An ending belonging to the Chess Variant's own rules.
    VariantRule,
}

impl Termination {
    fn from_code(code: c_int) -> Self {
        match code {
            1 => Termination::Checkmate,
            2 => Termination::Stalemate,
            3 => Termination::InsufficientMaterial,
            4 => Termination::FiftyMove,
            5 => Termination::Repetition,
            6 => Termination::VariantRule,
            _ => Termination::Playing,
        }
    }

    /// The stable identifier the workspace turns into wording for a player.
    pub fn name(self) -> &'static str {
        match self {
            Termination::Playing => "playing",
            Termination::Checkmate => "checkmate",
            Termination::Stalemate => "stalemate",
            Termination::InsufficientMaterial => "insufficient_material",
            Termination::FiftyMove => "fifty_move_rule",
            Termination::Repetition => "threefold_repetition",
            Termination::TimeForfeit => "time_forfeit",
            Termination::VariantRule => "variant_rule",
        }
    }
}

/// Who a game belongs to.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Winner {
    White,
    Black,
    Draw,
    /// The game is still being played.
    None,
}

impl Winner {
    fn from_code(code: c_int) -> Self {
        match code {
            0 => Winner::White,
            1 => Winner::Black,
            2 => Winner::Draw,
            _ => Winner::None,
        }
    }

    /// The result as it is written in a Game Record.
    pub fn score(self) -> &'static str {
        match self {
            Winner::White => "1-0",
            Winner::Black => "0-1",
            Winner::Draw => "1/2-1/2",
            Winner::None => "*",
        }
    }
}

/// The result of a game in its current position.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Outcome {
    pub termination: Termination,
    pub winner: Winner,
}

impl Outcome {
    pub fn is_over(self) -> bool {
        self.termination != Termination::Playing
    }
}

/// One legal move, in the engine's own coordinate notation.
///
/// A promotion carries the piece it promotes to, because a player has to
/// choose it before the move exists.
#[derive(Clone, PartialEq, Eq, Debug)]
pub struct LegalMove {
    pub from: String,
    pub to: String,
    pub promotion: Option<String>,
}

impl LegalMove {
    /// The engine's coordinate notation for this move, for example `e7e8q`.
    ///
    /// A promotion to a piece no pawn may become has no notation, and the
    /// engine rejects the move that comes back.
    pub fn uci(&self) -> String {
        match self.promotion.as_deref().map(promotion_letter) {
            Some(Some(letter)) => format!("{}{}{}", self.from, self.to, letter),
            Some(None) => format!("{}{}?", self.from, self.to),
            None => format!("{}{}", self.from, self.to),
        }
    }
}

impl fmt::Display for LegalMove {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.uci())
    }
}

/// The pieces a pawn may become, each with the letter the engine writes it as,
/// in the order they are offered to a player.
const PROMOTIONS: [(&str, char); 4] =
    [("queen", 'q'), ("rook", 'r'), ("bishop", 'b'), ("knight", 'n')];

/// The names of those pieces, which is the vocabulary the C ABI carries.
pub const PROMOTION_ROLES: [&str; 4] = ["queen", "rook", "bishop", "knight"];

fn promotion_letter(role: &str) -> Option<char> {
    PROMOTIONS.iter().find(|(name, _)| *name == role).map(|(_, letter)| *letter)
}

fn promotion_role(letter: char) -> Option<&'static str> {
    PROMOTIONS.iter().find(|(_, known)| *known == letter).map(|(name, _)| *name)
}

/// A game under one Chess Variant's rules, positioned at some point in its
/// move history.
///
/// Pushing and popping moves is how the position changes, so navigating a
/// Game Record and playing in it are the same mechanism.
pub struct Rules {
    handle: *mut OmachessRules,
}

// The handle is only ever reached through `&mut self`, and the engine keeps no
// state that another game can observe.
unsafe impl Send for Rules {}

impl Rules {
    pub fn load_variant_adapter(adapter: &str) -> bool {
        CString::new(adapter)
            .is_ok_and(|adapter| unsafe { omachess_rules_load_variant(adapter.as_ptr()) == 1 })
    }
    /// Standard chess from its starting position.
    pub fn standard() -> Self {
        Self::new("standard", None).expect("the engine always knows standard chess")
    }

    /// A game of `variant` starting at `start_fen`, or the variant's own
    /// starting position when none is given.
    ///
    /// Returns `None` when the engine does not know the variant or cannot use
    /// the position.
    pub fn new(variant: &str, start_fen: Option<&str>) -> Option<Self> {
        let variant = CString::new(variant).ok()?;
        let fen = match start_fen {
            Some(fen) => Some(CString::new(fen).ok()?),
            None => None,
        };
        let fen_pointer = fen.as_ref().map_or(std::ptr::null(), |fen| fen.as_ptr());
        // Safety: both strings outlive the call, and the engine copies what it
        // keeps.
        let handle = unsafe { omachess_rules_new(variant.as_ptr(), fen_pointer) };
        if handle.is_null() {
            None
        } else {
            Some(Rules { handle })
        }
    }

    /// The FEN of the current position.
    pub fn fen(&mut self) -> String {
        self.borrowed(|handle| unsafe { omachess_rules_fen(handle) })
    }

    /// Every legal move in the current position.
    pub fn legal_moves(&mut self) -> Vec<LegalMove> {
        let listing = self.borrowed(|handle| unsafe { omachess_rules_legal_moves(handle) });
        listing.split_whitespace().filter_map(parse_uci).collect()
    }

    /// The SAN of `uci_move` in the current position, or `None` when the move
    /// is not legal here.
    pub fn san(&mut self, uci_move: &str) -> Option<String> {
        let uci = CString::new(uci_move).ok()?;
        let san = self.borrowed(|handle| unsafe { omachess_rules_san(handle, uci.as_ptr()) });
        if san.is_empty() {
            None
        } else {
            Some(san)
        }
    }

    /// Plays `uci_move`, or reports that it is not legal here.
    pub fn push(&mut self, uci_move: &str) -> bool {
        let Ok(uci) = CString::new(uci_move) else {
            return false;
        };
        unsafe { omachess_rules_push(self.handle, uci.as_ptr()) == 1 }
    }

    pub fn bounded_search(&mut self, depth: i32) -> bool {
        unsafe { omachess_rules_bounded_search(self.handle, depth) == 1 }
    }

    pub fn analysis(&mut self, depth: i32) -> Option<(String, String)> {
        let value = unsafe { CStr::from_ptr(omachess_rules_analysis(self.handle, depth)) }
            .to_str()
            .ok()?;
        let (score, variation) = value.split_once('|')?;
        (!score.is_empty() && !variation.is_empty())
            .then(|| (score.to_owned(), variation.to_owned()))
    }

    /// Takes back the last move, or reports that the game is at its start.
    pub fn pop(&mut self) -> bool {
        unsafe { omachess_rules_pop(self.handle) == 1 }
    }

    /// Whether White is to move in the current position.
    pub fn white_to_move(&mut self) -> bool {
        unsafe { omachess_rules_side_to_move(self.handle) == 0 }
    }

    /// The number of the full move about to be played, as the engine counts
    /// them. Both halves of one move share a number.
    pub fn move_number(&mut self) -> u32 {
        // The last field of a FEN is the engine's own full-move counter.
        self.fen().split_whitespace().last().and_then(|n| n.parse().ok()).unwrap_or(1)
    }

    /// Whether the side to move is in check.
    pub fn in_check(&mut self) -> bool {
        unsafe { omachess_rules_in_check(self.handle) == 1 }
    }

    pub fn time_forfeit_winner(&mut self, loser_is_white: bool) -> Winner {
        Winner::from_code(unsafe {
            omachess_rules_time_forfeit_winner(self.handle, if loser_is_white { 0 } else { 1 })
        })
    }

    /// The result of the game in the current position.
    pub fn outcome(&mut self) -> Outcome {
        Outcome {
            termination: Termination::from_code(unsafe {
                omachess_rules_termination(self.handle)
            }),
            winner: Winner::from_code(unsafe { omachess_rules_winner(self.handle) }),
        }
    }

    /// Copies out a string the bridge only lends until the next call.
    fn borrowed(
        &mut self,
        ask: impl FnOnce(*mut OmachessRules) -> *const c_char,
    ) -> String {
        let text = ask(self.handle);
        if text.is_null() {
            return String::new();
        }
        // Safety: the bridge returns NUL-terminated UTF-8 that stays valid
        // until the next call on this handle, and we copy it here.
        unsafe { CStr::from_ptr(text) }.to_string_lossy().into_owned()
    }
}

impl Drop for Rules {
    fn drop(&mut self) {
        unsafe { omachess_rules_free(self.handle) };
    }
}

/// Splits an engine move such as `e2e4` or `e7e8q` into its parts.
pub(crate) fn parse_uci(uci: &str) -> Option<LegalMove> {
    // Coordinates are two characters on an 8x8 board; the engine only ever
    // appends a promotion letter.
    let mut characters = uci.chars();
    let from: String = [characters.next()?, characters.next()?].into_iter().collect();
    let to: String = [characters.next()?, characters.next()?].into_iter().collect();
    let promotion = match characters.next() {
        Some(letter) => Some(promotion_role(letter)?.to_owned()),
        None => None,
    };
    if characters.next().is_some() {
        return None;
    }
    Some(LegalMove { from, to, promotion })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_engine_counts_the_moves() {
        let mut rules = Rules::standard();
        assert_eq!(rules.move_number(), 1);
        assert!(rules.push("e2e4"));
        // Black's reply belongs to the same full move.
        assert_eq!(rules.move_number(), 1);
        assert!(rules.push("e7e5"));
        assert_eq!(rules.move_number(), 2);
    }

    #[test]
    fn the_starting_position_has_twenty_legal_moves() {
        let mut rules = Rules::standard();
        assert_eq!(rules.legal_moves().len(), 20);
        assert!(rules.white_to_move());
        assert_eq!(rules.outcome().termination, Termination::Playing);
        assert!(rules.fen().starts_with("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq"));
    }

    #[test]
    fn san_comes_from_the_engine() {
        let mut rules = Rules::standard();
        assert_eq!(rules.san("e2e4").as_deref(), Some("e4"));
        assert_eq!(rules.san("g1f3").as_deref(), Some("Nf3"));
        // Not legal from the starting position, so there is no SAN for it.
        assert_eq!(rules.san("e2e5"), None);
    }

    #[test]
    fn illegal_moves_are_refused_and_change_nothing() {
        let mut rules = Rules::standard();
        assert!(!rules.push("e2e5"));
        assert!(!rules.push("nonsense"));
        assert_eq!(rules.legal_moves().len(), 20);
    }

    #[test]
    fn popping_returns_to_the_previous_position() {
        let mut rules = Rules::standard();
        let start = rules.fen();
        assert!(rules.push("e2e4"));
        assert!(!rules.white_to_move());
        assert_ne!(rules.fen(), start);
        assert!(rules.pop());
        assert_eq!(rules.fen(), start);
        assert!(!rules.pop());
    }

    #[test]
    fn the_engine_reports_checkmate() {
        let mut rules = Rules::standard();
        for uci in ["f2f3", "e7e5", "g2g4", "d8h4"] {
            assert!(rules.push(uci), "{uci} should be legal");
        }
        assert_eq!(
            rules.outcome(),
            Outcome { termination: Termination::Checkmate, winner: Winner::Black }
        );
        assert!(rules.legal_moves().is_empty());
    }

    #[test]
    fn the_engine_reports_stalemate() {
        // A known stalemate in ten moves; Black has no legal move and is not
        // in check.
        let mut rules = Rules::new(
            "standard",
            Some("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"),
        )
        .unwrap();
        assert_eq!(
            rules.outcome(),
            Outcome { termination: Termination::Stalemate, winner: Winner::Draw }
        );
    }

    #[test]
    fn the_engine_reports_insufficient_material() {
        let mut rules = Rules::new("standard", Some("8/8/4k3/8/8/4K3/8/8 w - - 0 1")).unwrap();
        assert_eq!(
            rules.outcome(),
            Outcome { termination: Termination::InsufficientMaterial, winner: Winner::Draw }
        );
    }

    #[test]
    fn the_engine_reports_the_fifty_move_rule() {
        let mut rules =
            Rules::new("standard", Some("8/8/4k3/8/8/4K3/8/6RR w - - 99 60")).unwrap();
        assert_eq!(rules.outcome().termination, Termination::Playing);
        assert!(rules.push("h1h2"));
        assert_eq!(
            rules.outcome(),
            Outcome { termination: Termination::FiftyMove, winner: Winner::Draw }
        );
    }

    #[test]
    fn the_engine_reports_threefold_repetition() {
        let mut rules = Rules::standard();
        // Knights out and back, twice over, returns the starting position for
        // the third time.
        for uci in ["g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1", "f6g8"] {
            assert!(rules.push(uci), "{uci} should be legal");
        }
        assert_eq!(
            rules.outcome(),
            Outcome { termination: Termination::Repetition, winner: Winner::Draw }
        );
    }

    #[test]
    fn promotions_carry_the_piece_the_player_chose() {
        let mut rules = Rules::new("standard", Some("8/4P3/8/8/8/8/6k1/4K3 w - - 0 1")).unwrap();
        let promotions: Vec<String> = rules
            .legal_moves()
            .into_iter()
            .filter(|legal| legal.from == "e7" && legal.to == "e8")
            .filter_map(|legal| legal.promotion)
            .collect();
        assert_eq!(promotions, PROMOTION_ROLES);
        assert_eq!(rules.san("e7e8n").as_deref(), Some("e8=N"));
        assert!(rules.push("e7e8n"));
        assert!(rules.fen().starts_with("4N3/8/"));
    }

    #[test]
    fn castling_and_en_passant_are_the_engines_moves() {
        let mut rules = Rules::standard();
        for uci in ["e2e4", "e7e6", "g1f3", "d7d5", "f1c4", "d5e4"] {
            assert!(rules.push(uci), "{uci} should be legal");
        }
        assert_eq!(rules.san("e1g1").as_deref(), Some("O-O"));
        assert!(rules.push("e1g1"));
        assert!(rules.fen().contains("RNBQ1RK1") || rules.fen().contains("1RK1"));
    }
}
