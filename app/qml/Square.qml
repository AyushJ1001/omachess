import QtQuick
import QtQuick.Window
import Omachess

// One square, whatever piece the core placed on it, and the marks that tell a
// player what they may do with it.
//
// Square colours come from the Built-in Palette; a later ticket derives the
// Board Theme from the Quattro Palette. The piece is drawn from Piece Set
// artwork whose light and dark identities belong to the artwork itself — this
// file never recolours a piece.
Rectangle {
    id: square

    // The coordinate the core gave this square, for example "e4".
    property string squareName: ""
    property real size: 80
    property bool light: false
    // The core's piece identifier, for example "white_king"; empty when the
    // square is empty.
    property string piece: ""

    // The piece here has been picked up, so it is drawn in hand instead.
    property bool inHand: false
    // This is the square a piece was picked up from.
    property bool selected: false
    // The picked-up piece may be dropped here.
    property bool target: false
    // This square belongs to the move that produced the position on screen.
    property bool lastMove: false

    // Whether the Piece Set artwork for this square's piece is loaded and
    // drawn, and which file it came from. An empty square has no artwork.
    readonly property bool artworkReady: artwork.status === Image.Ready
    readonly property string artworkSource: artwork.source

    width: size
    height: size
    color: light ? "#ebecd0" : "#739552"

    // The move just played, so a player can see what changed.
    Rectangle {
        anchors.fill: parent
        color: "#f6f669"
        opacity: square.lastMove ? 0.42 : 0
    }

    // The square a piece was picked up from.
    Rectangle {
        anchors.fill: parent
        color: "#2b9fd8"
        opacity: square.selected ? 0.45 : 0
    }

    Image {
        id: artwork
        anchors.fill: parent
        anchors.margins: square.size * 0.02
        visible: square.piece !== "" && !square.inHand
        source: PieceSet.artwork(square.piece)
        // Vector artwork, rasterised at the size it is actually drawn, so it
        // stays crisp at every board size and under fractional scaling.
        sourceSize.width: Math.max(1, Math.round(square.size * Screen.devicePixelRatio))
        sourceSize.height: sourceSize.width
        fillMode: Image.PreserveAspectFit
        mipmap: true
    }

    // Where the picked-up piece may be dropped: a dot on an empty square, a
    // ring around a piece that would be captured.
    Rectangle {
        anchors.centerIn: parent
        visible: square.target && square.piece === ""
        width: square.size * 0.3
        height: width
        radius: width / 2
        color: "#1c2b1c"
        opacity: 0.32
    }

    Rectangle {
        anchors.fill: parent
        visible: square.target && square.piece !== ""
        color: "transparent"
        border.color: "#1c2b1c"
        border.width: Math.max(2, square.size * 0.08)
        opacity: 0.38
    }
}
