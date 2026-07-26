import QtQuick

// One square and whatever piece the core placed on it.
//
// Colours come from the Built-in Palette. A later ticket derives the Board
// Theme from the Quattro Palette; a later ticket replaces these glyphs with
// real Piece Set artwork.
Rectangle {
    id: square

    property real size: 80
    property bool light: false
    // The core's piece identifier, for example "white_king"; empty when the
    // square is empty.
    property string piece: ""

    readonly property var glyphs: ({
        "white_king": "♔", "white_queen": "♕", "white_rook": "♖",
        "white_bishop": "♗", "white_knight": "♘", "white_pawn": "♙",
        "black_king": "♚", "black_queen": "♛", "black_rook": "♜",
        "black_bishop": "♝", "black_knight": "♞", "black_pawn": "♟"
    })

    width: size
    height: size
    color: light ? "#ebecd0" : "#739552"

    Text {
        anchors.centerIn: parent
        visible: square.piece !== ""
        text: square.glyphs[square.piece] ?? ""
        color: square.piece.startsWith("white_") ? "#ffffff" : "#101418"
        font.pixelSize: square.size * 0.78
        // The glyphs are drawn filled; an outline keeps white pieces legible
        // on the light squares.
        style: Text.Outline
        styleColor: square.piece.startsWith("white_") ? "#101418" : "transparent"
    }
}
