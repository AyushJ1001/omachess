//! The core-owned board state for a Game Record's current position.
//!
//! The workspace never derives squares itself: the core resolves the position
//! and the board orientation into the exact sequence of squares to draw.
//!
//! A position here is only ever read out of a FEN that the rules authority
//! produced (see [`crate::rules`]). Reading the engine's own answer is not a
//! second rules implementation: nothing in this module decides what may move.

/// Which side is at the bottom of the board as the player sees it.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Orientation {
    WhiteBottom,
    BlackBottom,
}

impl Orientation {
    pub fn flipped(self) -> Self {
        match self {
            Orientation::WhiteBottom => Orientation::BlackBottom,
            Orientation::BlackBottom => Orientation::WhiteBottom,
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            Orientation::WhiteBottom => "white",
            Orientation::BlackBottom => "black",
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Color {
    White,
    Black,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Role {
    King,
    Queen,
    Rook,
    Bishop,
    Knight,
    Pawn,
}

impl Role {
    fn name(self) -> &'static str {
        match self {
            Role::King => "king",
            Role::Queen => "queen",
            Role::Rook => "rook",
            Role::Bishop => "bishop",
            Role::Knight => "knight",
            Role::Pawn => "pawn",
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Piece {
    pub color: Color,
    pub role: Role,
}

impl Piece {
    /// The piece a FEN placement character stands for.
    fn from_fen_char(character: char) -> Option<Self> {
        let role = match character.to_ascii_lowercase() {
            'k' => Role::King,
            'q' => Role::Queen,
            'r' => Role::Rook,
            'b' => Role::Bishop,
            'n' => Role::Knight,
            'p' => Role::Pawn,
            _ => return None,
        };
        let color = if character.is_ascii_uppercase() { Color::White } else { Color::Black };
        Some(Piece { color, role })
    }

    /// The stable identifier the workspace maps to Piece Set artwork.
    pub fn id(self) -> String {
        let color = match self.color {
            Color::White => "white",
            Color::Black => "black",
        };
        format!("{color}_{}", self.role.name())
    }
}

/// One square as the player sees it, already placed in display order.
pub struct RenderedSquare {
    pub name: String,
    pub light: bool,
    pub piece: Option<Piece>,
}

/// The current position of the played Game Record.
///
/// Squares are stored in board coordinates (index 0 is a1, index 63 is h8) so
/// that orientation stays a presentation concern resolved at render time.
pub struct Position {
    squares: [Option<Piece>; 64],
}

impl Position {
    /// Reads the piece placement out of a FEN produced by the rules authority.
    ///
    /// Returns `None` when the placement field does not describe an 8x8 board
    /// of pieces this build can draw.
    pub fn from_fen(fen: &str) -> Option<Self> {
        let placement = fen.split_whitespace().next()?;
        let mut squares = [None; 64];
        let mut rank = 7usize;
        let mut file = 0usize;

        for character in placement.chars() {
            match character {
                '/' => {
                    if file != 8 {
                        return None;
                    }
                    rank = rank.checked_sub(1)?;
                    file = 0;
                }
                '1'..='9' => {
                    file += character.to_digit(10)? as usize;
                    if file > 8 {
                        return None;
                    }
                }
                _ => {
                    if file >= 8 {
                        return None;
                    }
                    squares[rank * 8 + file] = Some(Piece::from_fen_char(character)?);
                    file += 1;
                }
            }
        }

        if rank != 0 || file != 8 {
            return None;
        }
        Some(Position { squares })
    }

    /// The 64 squares in reading order for `orientation`: the top-left square
    /// of the drawn board first, the bottom-right square last.
    pub fn rendered(&self, orientation: Orientation) -> Vec<RenderedSquare> {
        let mut out = Vec::with_capacity(64);
        for row in 0..8 {
            for column in 0..8 {
                let (file, rank) = match orientation {
                    Orientation::WhiteBottom => (column, 7 - row),
                    Orientation::BlackBottom => (7 - column, row),
                };
                let index = rank * 8 + file;
                out.push(RenderedSquare {
                    name: format!("{}{}", (b'a' + file as u8) as char, rank + 1),
                    light: (file + rank) % 2 == 1,
                    piece: self.squares[index],
                });
            }
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const START: &str = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    fn start() -> Position {
        Position::from_fen(START).unwrap()
    }

    #[test]
    fn white_bottom_starts_at_a8_and_ends_at_h1() {
        let rendered = start().rendered(Orientation::WhiteBottom);
        assert_eq!(rendered.len(), 64);
        assert_eq!(rendered[0].name, "a8");
        assert_eq!(rendered[63].name, "h1");
    }

    #[test]
    fn flipping_puts_black_at_the_bottom() {
        let rendered = start().rendered(Orientation::BlackBottom);
        assert_eq!(rendered[0].name, "h1");
        assert_eq!(rendered[63].name, "a8");
    }

    #[test]
    fn a8_holds_a_black_rook_and_is_light() {
        let rendered = start().rendered(Orientation::WhiteBottom);
        assert_eq!(rendered[0].piece.unwrap().id(), "black_rook");
        assert!(rendered[0].light);
    }

    #[test]
    fn the_middle_four_ranks_are_empty() {
        let rendered = start().rendered(Orientation::WhiteBottom);
        assert!(rendered[16..48].iter().all(|square| square.piece.is_none()));
    }

    #[test]
    fn a_played_move_shows_up_where_the_fen_puts_it() {
        let position =
            Position::from_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1")
                .unwrap();
        let squares: Vec<_> = position.rendered(Orientation::WhiteBottom);
        let named = |name: &str| {
            squares.iter().find(|square| square.name == name).unwrap().piece.map(Piece::id)
        };
        assert_eq!(named("e4").as_deref(), Some("white_pawn"));
        assert_eq!(named("e2"), None);
    }

    #[test]
    fn a_promoted_piece_is_drawn_as_that_piece() {
        let position = Position::from_fen("4Q3/8/8/8/8/8/6k1/4K3 b - - 0 2").unwrap();
        let rendered = position.rendered(Orientation::WhiteBottom);
        assert_eq!(rendered[4].name, "e8");
        assert_eq!(rendered[4].piece.unwrap().id(), "white_queen");
    }

    #[test]
    fn placements_that_are_not_a_drawable_board_are_refused() {
        // Too few ranks, too many files, and a piece this build cannot draw.
        assert!(Position::from_fen("8/8/8/8 w - - 0 1").is_none());
        assert!(Position::from_fen("ppppppppp/8/8/8/8/8/8/8 w - - 0 1").is_none());
        assert!(Position::from_fen("xnbqkbnr/8/8/8/8/8/8/8 w - - 0 1").is_none());
        assert!(Position::from_fen("").is_none());
    }
}
