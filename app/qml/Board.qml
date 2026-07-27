import QtQuick
import QtQuick.Window
import Omachess

// The board, drawn straight from the squares the core sent, and the surface a
// player moves pieces on.
//
// The repeater walks WorkspaceSession.board in the order the core gave it, so
// flipping is a core decision that arrives as a new square order — the
// workspace never reverses anything itself.
//
// Input is pointer-first: press a piece to pick it up, drag it and release it
// on a square to drop it, or press the destination afterwards. Where a piece
// may go comes from the moves the core sent, and the core refuses anything
// else that reaches it, so this file cannot invent a legal move.
Item {
    id: board

    // The width and height of the whole board.
    property real side: 640
    property bool inputEnabled: true
    property int files: WorkspaceSession.workshopActive ? WorkspaceSession.boardFiles : 8
    property int ranks: WorkspaceSession.workshopActive ? WorkspaceSession.boardRanks : 8

    // The square a piece has been picked up from, or "" when the player is
    // holding nothing.
    property string selected: ""
    // Where the held piece may be dropped.
    readonly property var targets: selected === ""
        ? []
        : WorkspaceSession.destinationsFrom(selected)

    // Asks for the piece a promoting pawn should become. Whoever answers calls
    // promote() or cancel().
    signal promotionRequested(string from, string to, var roles)

    property string heldPiece: ""
    property bool dragging: false
    property real dragX: 0
    property real dragY: 0
    property string setupPiece: "__move"
    property string setupFrom: ""

    readonly property real cell: Math.min(width / files, height / ranks)

    width: WorkspaceSession.workshopActive ? side : side
    height: WorkspaceSession.workshopActive ? side * ranks / files : side

    // Any new position means the piece that was picked up is no longer where
    // it was, so nothing stays held across a board change.
    Connections {
        target: WorkspaceSession
        function onBoardChanged() { board.cancel() }
    }

    // Answers a promotionRequested with the player's choice.
    function promote(from, to, role) {
        WorkspaceSession.playMove(from, to, role)
    }

    // Puts down whatever is being held without playing a move.
    function cancel() {
        selected = ""
        heldPiece = ""
        dragging = false
    }

    // Which square is at a point on the board.
    //
    // The core sends its squares in the order they are drawn, so the answer
    // comes from the core's own list rather than from the items currently on
    // screen: a board that has just changed has not laid its squares out yet.
    function squareAt(x, y) {
        if (x < 0 || y < 0 || x >= side || y >= side)
            return ""
        const column = Math.floor(x / cell)
        const row = Math.floor(y / cell)
        return WorkspaceSession.squareNameAt(row * files + column)
    }

    function isTarget(square) {
        return targets.indexOf(square) >= 0
    }

    function pickUp(square, x, y) {
        selected = square
        heldPiece = WorkspaceSession.pieceOn(square)
        dragging = true
        dragX = x
        dragY = y
    }

    // Plays a move, or asks which piece a promoting pawn becomes first.
    function drop(from, to) {
        const roles = WorkspaceSession.promotionsFor(from, to)
        dragging = false
        if (roles.length > 0)
            board.promotionRequested(from, to, roles)
        else
            WorkspaceSession.playMove(from, to, "")
    }

    Grid {
        id: grid
        anchors.fill: parent
        columns: board.files
        rows: board.ranks

        Repeater {
            model: WorkspaceSession.board

            Square {
                objectName: "square:" + model.squareName
                squareName: model.squareName
                size: board.cell
                light: model.light
                piece: model.piece
                selected: board.selected === model.squareName
                // Written out rather than calling isTarget(), so the mark
                // follows the targets it is showing.
                target: board.targets.indexOf(model.squareName) >= 0
                inHand: board.dragging && board.selected === model.squareName
                lastMove: model.squareName === WorkspaceSession.lastMoveFrom
                          || model.squareName === WorkspaceSession.lastMoveTo
                footprint: WorkspaceSession.workshopStep === 4 ? model.footprint : ""
            }
        }
    }

    // The piece being carried, following the pointer.
    Image {
        visible: board.dragging && board.heldPiece !== ""
        source: PieceSet.artwork(board.heldPiece)
        width: board.cell
        height: board.cell
        x: board.dragX - width / 2
        y: board.dragY - height / 2
        sourceSize.width: Math.max(1, Math.round(board.cell * Screen.devicePixelRatio))
        sourceSize.height: sourceSize.width
        fillMode: Image.PreserveAspectFit
        mipmap: true
        z: 1
    }

    MouseArea {
        anchors.fill: parent
        enabled: board.inputEnabled
        acceptedButtons: Qt.LeftButton

        onPressed: function (mouse) {
            const square = board.squareAt(mouse.x, mouse.y)
            if (WorkspaceSession.positionSetup) {
                if (square === "")
                    return
                if (board.setupPiece === "__move") {
                    if (board.setupFrom === "") {
                        if (WorkspaceSession.pieceOn(square) !== "")
                            board.setupFrom = square
                    } else {
                        WorkspaceSession.relocateSetupPiece(board.setupFrom, square)
                        board.setupFrom = ""
                    }
                } else {
                    WorkspaceSession.placeSetupPiece(
                        square, board.setupPiece === "__remove" ? "" : board.setupPiece)
                }
                return
            }
            if (WorkspaceSession.workshopActive
                    && WorkspaceSession.workshopStep === 3) {
                if (square !== "")
                    WorkspaceSession.placeWorkshopPiece(
                                square,
                                board.setupPiece === "__remove" ? "" : board.setupPiece)
                return
            }
            if (square === "") {
                board.cancel()
                return
            }
            // Pressing a destination completes a move that was started by
            // pressing a piece, so click-then-click works as well as drag.
            if (board.selected !== "" && board.isTarget(square)) {
                board.drop(board.selected, square)
                return
            }
            if (WorkspaceSession.canPickUp(square))
                board.pickUp(square, mouse.x, mouse.y)
            else
                board.cancel()
        }

        onPositionChanged: function (mouse) {
            if (!board.dragging)
                return
            board.dragX = mouse.x
            board.dragY = mouse.y
        }

        onReleased: function (mouse) {
            if (!board.dragging)
                return
            const square = board.squareAt(mouse.x, mouse.y)
            if (square !== "" && square !== board.selected && board.isTarget(square)) {
                board.drop(board.selected, square)
                return
            }
            // Released somewhere the piece cannot go: it goes back to its
            // square but stays picked up, so the destination can be pressed
            // instead.
            board.dragging = false
        }
    }
}
