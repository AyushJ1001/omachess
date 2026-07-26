//! The core-owned board state for a Game Record's current position.
//!
//! The workspace never derives squares itself: the core resolves the position
//! and the board orientation into the exact sequence of squares to draw.

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
    /// The starting position of standard chess.
    pub fn standard_start() -> Self {
        let mut squares = [None; 64];
        let back_rank = [
            Role::Rook,
            Role::Knight,
            Role::Bishop,
            Role::Queen,
            Role::King,
            Role::Bishop,
            Role::Knight,
            Role::Rook,
        ];
        for (file, role) in back_rank.iter().enumerate() {
            squares[file] = Some(Piece { color: Color::White, role: *role });
            squares[8 + file] = Some(Piece { color: Color::White, role: Role::Pawn });
            squares[48 + file] = Some(Piece { color: Color::Black, role: Role::Pawn });
            squares[56 + file] = Some(Piece { color: Color::Black, role: *role });
        }
        Position { squares }
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

    #[test]
    fn white_bottom_starts_at_a8_and_ends_at_h1() {
        let rendered = Position::standard_start().rendered(Orientation::WhiteBottom);
        assert_eq!(rendered.len(), 64);
        assert_eq!(rendered[0].name, "a8");
        assert_eq!(rendered[63].name, "h1");
    }

    #[test]
    fn flipping_puts_black_at_the_bottom() {
        let rendered = Position::standard_start().rendered(Orientation::BlackBottom);
        assert_eq!(rendered[0].name, "h1");
        assert_eq!(rendered[63].name, "a8");
    }

    #[test]
    fn a8_holds_a_black_rook_and_is_light() {
        let rendered = Position::standard_start().rendered(Orientation::WhiteBottom);
        assert_eq!(rendered[0].piece.unwrap().id(), "black_rook");
        assert!(rendered[0].light);
    }

    #[test]
    fn the_middle_four_ranks_are_empty() {
        let rendered = Position::standard_start().rendered(Orientation::WhiteBottom);
        assert!(rendered[16..48].iter().all(|square| square.piece.is_none()));
    }
}
