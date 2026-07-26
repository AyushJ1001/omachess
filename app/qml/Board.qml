import QtQuick
import Omachess

// The board, drawn straight from the squares the core sent.
//
// The repeater walks WorkspaceSession.board in the order the core gave it, so
// flipping is a core decision that arrives as a new square order — the
// workspace never reverses anything itself.
Item {
    id: board

    // The width and height of the whole board.
    property real side: 640

    width: side
    height: side

    Grid {
        id: grid
        anchors.fill: parent
        columns: 8
        rows: 8

        Repeater {
            model: WorkspaceSession.board

            Square {
                objectName: "square:" + model.squareName
                size: board.side / 8
                light: model.light
                piece: model.piece
            }
        }
    }
}
