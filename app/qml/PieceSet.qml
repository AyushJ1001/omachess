pragma Singleton
import QtQuick
import Omachess

// The Piece Set in use: the artwork every piece is drawn from.
//
// A player chooses a Piece Set independently of any palette, so which set is in
// use lives on Theme rather than at each place a piece is drawn. The light and
// dark identities belong to the artwork, so nothing here carries a colour.
QtObject {
    // The artwork for one of the core's piece identifiers, for example
    // "white_king". An empty identifier has no artwork.
    function artwork(piece) {
        return piece === "" || piece.indexOf("_fairy_") >= 0
             ? "" : Theme.pieceSetPath + piece + ".svg"
    }
}
