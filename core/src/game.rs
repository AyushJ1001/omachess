//! The Played Game: the moves played so far, and where the player is looking.
//!
//! A game keeps two positions apart. The Latest Position is the end of the move
//! list, the only place a move may be played. The Displayed Position is
//! wherever the player has navigated to, which may be earlier. Navigating is
//! popping and pushing moves on the Rules Authority, so the Displayed Position
//! is always one the engine itself produced.

use crate::board::Position;
use crate::rules::{LegalMove, Outcome, Rules, Winner, PROMOTION_ROLES};

/// One move as a Game Record keeps it: what was played, and how it reads.
///
/// The number and the side come from the engine's own count of the position the
/// move was played in, not from this move's place in the list.
#[derive(Clone, PartialEq, Eq, Debug)]
pub struct PlayedMove {
    pub uci: String,
    pub san: String,
    pub number: u32,
    pub side: &'static str,
}

/// Where a navigation request wants to go.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Destination {
    Backward,
    Forward,
    Start,
    /// The Latest Position, where play continues.
    End,
}

impl Destination {
    pub fn parse(name: &str) -> Option<Self> {
        match name {
            "backward" => Some(Destination::Backward),
            "forward" => Some(Destination::Forward),
            "start" => Some(Destination::Start),
            "end" => Some(Destination::End),
            _ => None,
        }
    }
}

/// Why a move was not played.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum MoveRejected {
    /// The Rules Authority does not allow it in this position.
    Illegal,
    /// The game already has a result.
    GameOver,
    /// The player is looking at an earlier position; play continues at the
    /// Latest Position.
    Reviewing,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Side {
    White,
    Black,
}

/// The moves available from one square to one other square.
///
/// A promotion is several engine moves that share a from and a to, so they are
/// grouped here: the workspace has to offer the player the choice before the
/// move exists.
#[derive(Clone, PartialEq, Eq, Debug)]
pub struct MoveOffer {
    pub from: String,
    pub to: String,
    /// The pieces a pawn may become, in the order they are offered. Empty for
    /// every move that is not a promotion.
    pub promotions: Vec<String>,
}

pub struct Game {
    /// The Rules Authority, positioned at the Displayed Position.
    rules: Rules,
    /// The starting position this Game Record began from.
    start_fen: String,
    moves: Vec<PlayedMove>,
    /// How many moves have been applied to the Displayed Position.
    cursor: usize,
    /// The result of the Latest Position. Navigating does not change it: a
    /// finished game stays finished while its moves are being reviewed.
    outcome: Outcome,
}

impl Game {
    /// A new Played Game of standard chess, ready to start.
    pub fn standard() -> Self {
        let mut rules = Rules::standard();
        let outcome = rules.outcome();
        let start_fen = rules.fen();
        Game {
            rules,
            start_fen,
            moves: Vec::new(),
            cursor: 0,
            outcome,
        }
    }

    /// Rebuilds a Game Record from its starting position and played moves.
    pub fn from_history(start_fen: &str, moves: Vec<PlayedMove>) -> Option<Self> {
        let mut rules = Rules::new("standard", Some(start_fen))?;
        for played in &moves {
            if !rules.push(&played.uci) {
                return None;
            }
        }
        let outcome = rules.outcome();
        let cursor = moves.len();
        Some(Game {
            rules,
            start_fen: start_fen.to_owned(),
            moves,
            cursor,
            outcome,
        })
    }

    pub fn start_fen(&self) -> &str {
        &self.start_fen
    }

    /// The position the player is looking at.
    pub fn position(&mut self) -> Position {
        let fen = self.rules.fen();
        Position::from_fen(&fen)
            .expect("the Rules Authority always describes a board the core can draw")
    }

    pub fn fen(&mut self) -> String {
        self.rules.fen()
    }

    /// Whether White is to move in the Displayed Position.
    pub fn white_to_move(&mut self) -> bool {
        self.rules.white_to_move()
    }

    /// Whether the side to move in the Displayed Position is in check.
    pub fn in_check(&mut self) -> bool {
        self.rules.in_check()
    }

    pub fn moves(&self) -> &[PlayedMove] {
        &self.moves
    }

    pub fn cursor(&self) -> usize {
        self.cursor
    }

    /// Whether the player is looking at an earlier position than the latest.
    pub fn reviewing(&self) -> bool {
        self.cursor < self.moves.len()
    }

    pub fn outcome(&self) -> Outcome {
        self.outcome
    }

    /// Completes an otherwise unfinished Played Game when `loser` flags.
    pub fn complete_on_time(&mut self, loser: Side) -> bool {
        if self.outcome.is_over() {
            return false;
        }
        self.outcome = Outcome {
            termination: crate::rules::Termination::TimeForfeit,
            winner: self.rules.time_forfeit_winner(loser == Side::White),
        };
        true
    }

    /// The move that produced the Displayed Position, if any.
    pub fn last_move(&self) -> Option<&PlayedMove> {
        self.cursor.checked_sub(1).and_then(|index| self.moves.get(index))
    }

    /// Every move a player may make now, grouped by the squares it joins.
    ///
    /// Empty while reviewing an earlier position or once the game has a
    /// result, because no move may be played in either case.
    pub fn offers(&mut self) -> Vec<MoveOffer> {
        if self.reviewing() || self.outcome.is_over() {
            return Vec::new();
        }

        let mut offers: Vec<MoveOffer> = Vec::new();
        for legal in self.rules.legal_moves() {
            let LegalMove { from, to, promotion } = legal;
            let existing = offers
                .iter_mut()
                .find(|offer| offer.from == from && offer.to == to);
            let offer = match existing {
                Some(offer) => offer,
                None => {
                    offers.push(MoveOffer { from, to, promotions: Vec::new() });
                    offers.last_mut().expect("just pushed")
                }
            };
            if let Some(role) = promotion {
                offer.promotions.push(role);
            }
        }

        // Offer promotion pieces in one settled order rather than the engine's
        // move-generation order, so the player always sees the same choices in
        // the same places.
        for offer in &mut offers {
            offer.promotions.sort_by_key(|role| {
                PROMOTION_ROLES.iter().position(|known| known == role).unwrap_or(usize::MAX)
            });
        }
        offers
    }

    /// Plays the move from `from` to `to`, promoting to `promotion` when the
    /// move is a promotion.
    ///
    /// The Rules Authority decides whether the move exists; this only decides
    /// whether the game is in a state to accept one.
    pub fn play(
        &mut self,
        from: &str,
        to: &str,
        promotion: Option<&str>,
    ) -> Result<(), MoveRejected> {
        if self.outcome.is_over() {
            return Err(MoveRejected::GameOver);
        }
        if self.reviewing() {
            return Err(MoveRejected::Reviewing);
        }

        if let Some(role) = promotion {
            if !PROMOTION_ROLES.contains(&role) {
                return Err(MoveRejected::Illegal);
            }
        }
        let uci = LegalMove {
            from: from.to_owned(),
            to: to.to_owned(),
            promotion: promotion.map(str::to_owned),
        }
        .uci();

        // Ask the engine about the move in the position it is played in: the
        // SAN names it there, its number and side belong to that position, and
        // a move with no SAN is the engine's way of saying it is not legal.
        let Some(san) = self.rules.san(&uci) else {
            return Err(MoveRejected::Illegal);
        };
        let number = self.rules.move_number();
        let side = if self.rules.white_to_move() { "white" } else { "black" };
        if !self.rules.push(&uci) {
            return Err(MoveRejected::Illegal);
        }

        self.moves.push(PlayedMove { uci, san, number, side });
        self.cursor = self.moves.len();
        self.outcome = self.rules.outcome();
        Ok(())
    }

    /// Moves the Displayed Position, and reports whether it changed.
    pub fn navigate(&mut self, destination: Destination) -> bool {
        let wanted = match destination {
            Destination::Backward => self.cursor.saturating_sub(1),
            Destination::Forward => (self.cursor + 1).min(self.moves.len()),
            Destination::Start => 0,
            Destination::End => self.moves.len(),
        };
        self.go_to(wanted)
    }

    /// Replays or takes back moves until the Displayed Position is the one
    /// after `wanted` moves.
    fn go_to(&mut self, wanted: usize) -> bool {
        if wanted == self.cursor {
            return false;
        }
        while self.cursor > wanted {
            assert!(self.rules.pop(), "a played move can always be taken back");
            self.cursor -= 1;
        }
        while self.cursor < wanted {
            let uci = self.moves[self.cursor].uci.clone();
            assert!(self.rules.push(&uci), "a played move can always be replayed");
            self.cursor += 1;
        }
        true
    }
}

impl Default for Game {
    fn default() -> Self {
        Game::standard()
    }
}

/// How a result reads to a player, for example "White wins by checkmate".
pub fn result_label(outcome: Outcome) -> String {
    let reason = match outcome.termination.name() {
        "checkmate" => "checkmate",
        "stalemate" => "stalemate",
        "insufficient_material" => "insufficient material",
        "fifty_move_rule" => "the fifty-move rule",
        "threefold_repetition" => "threefold repetition",
        "variant_rule" => "the rules of this variant",
        "time_forfeit" => "time forfeit",
        _ => return "In progress".to_owned(),
    };
    match outcome.winner {
        Winner::White => format!("White wins by {reason}"),
        Winner::Black => format!("Black wins by {reason}"),
        Winner::Draw => format!("Draw by {reason}"),
        Winner::None => "In progress".to_owned(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::rules::Termination;

    fn play(game: &mut Game, moves: &[&str]) {
        for pair in moves {
            let (from, to) = pair.split_at(2);
            game.play(from, to, None).unwrap_or_else(|error| panic!("{pair}: {error:?}"));
        }
    }

    #[test]
    fn a_new_game_starts_at_the_standard_position_with_white_to_move() {
        let mut game = Game::standard();
        assert!(game.white_to_move());
        assert!(game.moves().is_empty());
        assert_eq!(game.cursor(), 0);
        assert!(!game.reviewing());
        assert_eq!(game.offers().len(), 20);
        assert!(!game.outcome().is_over());
    }

    #[test]
    fn playing_a_move_records_its_san() {
        let mut game = Game::standard();
        play(&mut game, &["e2e4", "e7e5", "g1f3"]);
        let sans: Vec<&str> = game.moves().iter().map(|played| played.san.as_str()).collect();
        assert_eq!(sans, ["e4", "e5", "Nf3"]);
        assert_eq!(game.cursor(), 3);
    }

    #[test]
    fn an_illegal_move_is_refused_and_changes_nothing() {
        let mut game = Game::standard();
        assert_eq!(game.play("e2", "e5", None), Err(MoveRejected::Illegal));
        assert_eq!(game.play("e4", "e5", None), Err(MoveRejected::Illegal));
        assert!(game.moves().is_empty());
        assert!(game.white_to_move());
    }

    #[test]
    fn each_move_carries_the_number_and_side_the_engine_gave_it() {
        let mut game = Game::standard();
        play(&mut game, &["e2e4", "e7e5", "g1f3"]);
        let numbered: Vec<(u32, &str)> =
            game.moves().iter().map(|played| (played.number, played.side)).collect();
        assert_eq!(numbered, [(1, "white"), (1, "black"), (2, "white")]);
    }

    #[test]
    fn flag_fall_completes_the_game_and_freezes_its_moves() {
        let mut game = Game::standard();
        play(&mut game, &["e2e4"]);
        assert!(game.complete_on_time(Side::Black));
        assert_eq!(game.outcome().winner, Winner::White);
        assert_eq!(game.outcome().termination, Termination::TimeForfeit);
        assert_eq!(game.play("e7", "e5", None), Err(MoveRejected::GameOver));
        assert_eq!(game.moves().len(), 1);
        assert!(!game.complete_on_time(Side::White));
    }

    #[test]
    fn flag_fall_is_a_draw_when_the_other_side_cannot_mate() {
        let mut game = Game::new_from("4k2r/8/8/8/8/8/8/4K3 w - - 0 1");
        assert!(game.complete_on_time(Side::Black));
        assert_eq!(game.outcome().winner, Winner::Draw);
        assert_eq!(game.outcome().termination, Termination::TimeForfeit);
    }

    #[test]
    fn navigating_backward_and_forward_changes_the_displayed_position() {
        let mut game = Game::standard();
        play(&mut game, &["e2e4", "e7e5"]);
        let latest = game.fen();

        assert!(game.navigate(Destination::Backward));
        assert!(game.reviewing());
        assert!(!game.white_to_move());
        assert_ne!(game.fen(), latest);
        assert_eq!(game.last_move().unwrap().san, "e4");

        assert!(game.navigate(Destination::Start));
        assert_eq!(game.cursor(), 0);
        assert!(game.last_move().is_none());
        assert!(game.fen().starts_with("rnbqkbnr/pppppppp"));
        // Already at the start, so there is nowhere further back to go.
        assert!(!game.navigate(Destination::Backward));

        assert!(game.navigate(Destination::Forward));
        assert_eq!(game.cursor(), 1);

        assert!(game.navigate(Destination::End));
        assert_eq!(game.fen(), latest);
        assert!(!game.reviewing());
        assert!(!game.navigate(Destination::Forward));
    }

    #[test]
    fn navigating_never_loses_a_move() {
        let mut game = Game::standard();
        play(&mut game, &["e2e4", "e7e5", "g1f3", "b8c6"]);
        game.navigate(Destination::Start);
        game.navigate(Destination::End);
        let sans: Vec<&str> = game.moves().iter().map(|played| played.san.as_str()).collect();
        assert_eq!(sans, ["e4", "e5", "Nf3", "Nc6"]);
    }

    #[test]
    fn no_move_may_be_played_while_reviewing() {
        let mut game = Game::standard();
        play(&mut game, &["e2e4", "e7e5"]);
        game.navigate(Destination::Backward);
        assert_eq!(game.play("g1", "f3", None), Err(MoveRejected::Reviewing));
        assert!(game.offers().is_empty());
        assert_eq!(game.moves().len(), 2);
    }

    #[test]
    fn a_promotion_offers_a_choice_and_plays_the_chosen_piece() {
        let mut game = Game::new_from("7r/6P1/8/8/8/8/6k1/4K3 w - - 0 1");

        let offers = game.offers();
        let straight = offers
            .iter()
            .find(|offer| offer.from == "g7" && offer.to == "g8")
            .expect("the pawn on g7 can promote on g8");
        assert_eq!(straight.promotions, ["queen", "rook", "bishop", "knight"]);
        let capture = offers
            .iter()
            .find(|offer| offer.from == "g7" && offer.to == "h8")
            .expect("the pawn on g7 can promote by capturing on h8");
        assert_eq!(capture.promotions, ["queen", "rook", "bishop", "knight"]);

        game.play("g7", "h8", Some("knight")).unwrap();
        assert_eq!(game.moves().last().unwrap().san, "gxh8=N");
        assert!(game.fen().starts_with("7N/"));
    }

    #[test]
    fn a_promotion_must_name_a_piece_a_pawn_may_become() {
        let mut game = Game::new_from("8/4P3/8/8/8/8/6k1/4K3 w - - 0 1");
        assert_eq!(game.play("e7", "e8", Some("king")), Err(MoveRejected::Illegal));
        assert_eq!(game.play("e7", "e8", None), Err(MoveRejected::Illegal));
        game.play("e7", "e8", Some("queen")).unwrap();
        assert_eq!(game.moves().last().unwrap().san, "e8=Q");
    }

    #[test]
    fn a_finished_game_accepts_no_more_moves() {
        let mut game = Game::standard();
        play(&mut game, &["f2f3", "e7e5", "g2g4", "d8h4"]);
        assert_eq!(game.outcome().termination, Termination::Checkmate);
        assert_eq!(result_label(game.outcome()), "Black wins by checkmate");
        assert_eq!(game.play("e1", "f2", None), Err(MoveRejected::GameOver));
        assert!(game.offers().is_empty());
    }

    #[test]
    fn a_finished_game_stays_finished_while_its_moves_are_reviewed() {
        let mut game = Game::standard();
        play(&mut game, &["f2f3", "e7e5", "g2g4", "d8h4"]);
        game.navigate(Destination::Start);
        assert_eq!(game.outcome().termination, Termination::Checkmate);
        assert_eq!(game.moves().len(), 4);
    }

    #[test]
    fn results_read_the_way_a_player_would_say_them() {
        let mut game = Game::new_from("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        assert_eq!(result_label(game.outcome()), "Draw by stalemate");
        assert!(game.offers().is_empty());
    }
}

#[cfg(test)]
impl Game {
    /// A game that starts from `fen`, for tests that need a position a real
    /// game would take many moves to reach.
    fn new_from(fen: &str) -> Self {
        let mut rules = Rules::new("standard", Some(fen)).expect("a usable test position");
        let outcome = rules.outcome();
        let start_fen = rules.fen();
        Game {
            rules,
            start_fen,
            moves: Vec::new(),
            cursor: 0,
            outcome,
        }
    }
}
