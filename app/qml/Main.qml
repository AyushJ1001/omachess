import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omachess

// The Omachess workspace window.
//
// Everything on screen comes from WorkspaceSession, which is filled by core
// events. This file decides how a game looks, never what it contains.
ApplicationWindow {
    id: workspace

    // An ordinary resizable window: no fixed size, no compositor hints, so
    // dwindle and scrolling layouts can tile it like any other application.
    width: 1024
    height: 720
    minimumWidth: 480
    minimumHeight: 360
    visible: true
    title: qsTr("Omachess")
    color: Theme.background

    Component.onCompleted: WorkspaceSession.describeBoard()

    header: ToolBar {
        background: Rectangle { color: Theme.panel }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 12

            Label {
                objectName: "variantLabel"
                text: qsTr("Standard chess")
                color: Theme.foreground
            }

            Label {
                objectName: "statusLabel"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                color: Theme.foreground
                // A finished game reports its result; an unfinished one reports
                // whose turn it is, and whether that side is in check.
                text: WorkspaceSession.gameOver
                      ? WorkspaceSession.resultLabel + " (" + WorkspaceSession.resultScore + ")"
                      : (WorkspaceSession.sideToMove === "white"
                         ? qsTr("White to move") : qsTr("Black to move"))
                        + (WorkspaceSession.inCheck ? qsTr(" — in check") : "")
                font.bold: WorkspaceSession.gameOver
                elide: Text.ElideRight
            }

            // Board Theme: follow the Quattro Palette, or pin an Omachess-owned set.
            ComboBox {
                id: boardThemePicker
                objectName: "boardThemePicker"
                model: Theme.boardThemeIds
                displayText: qsTr("Board: %1").arg(currentText)
                implicitWidth: 140
                Component.onCompleted: currentIndex = model.indexOf(Theme.boardThemeId)
                onActivated: Theme.setBoardTheme(model[currentIndex])
                Connections {
                    target: Theme
                    function onThemeChanged() {
                        boardThemePicker.currentIndex =
                            boardThemePicker.model.indexOf(Theme.boardThemeId)
                    }
                }
            }

            // Click targets for journeys: opacity 0 but visible so the control
            // channel can press them under the offscreen QPA.
            Repeater {
                model: Theme.boardThemeIds
                Button {
                    required property string modelData
                    objectName: "boardTheme:" + modelData
                    opacity: 0
                    width: 1
                    height: 1
                    onClicked: Theme.setBoardTheme(modelData)
                }
            }

            // Piece Set selection is independent of any palette.
            ComboBox {
                id: pieceSetPicker
                objectName: "pieceSetPicker"
                model: Theme.pieceSetIds
                displayText: qsTr("Pieces: %1").arg(currentText)
                implicitWidth: 140
                Component.onCompleted: currentIndex = model.indexOf(Theme.pieceSetId)
                onActivated: Theme.setPieceSet(model[currentIndex])
            }

            Repeater {
                model: Theme.pieceSetIds
                Button {
                    required property string modelData
                    objectName: "pieceSet:" + modelData
                    opacity: 0
                    width: 1
                    height: 1
                    onClicked: Theme.setPieceSet(modelData)
                }
            }

            Button {
                objectName: "flipButton"
                text: qsTr("Flip board (F)")
                onClicked: WorkspaceSession.flipBoard()
            }
        }
    }

    // The workspace surface holds keyboard focus, so keyboard-first play works
    // as soon as the window is open.
    Item {
        id: surface
        anchors.fill: parent
        focus: true

        // Player intent leaves the workspace here, and comes back as a core
        // event carrying the board to draw.
        Keys.onPressed: function (event) {
            switch (event.key) {
            case Qt.Key_F:
                WorkspaceSession.flipBoard()
                break
            case Qt.Key_Left:
                WorkspaceSession.navigate("backward")
                break
            case Qt.Key_Right:
                WorkspaceSession.navigate("forward")
                break
            case Qt.Key_Home:
                WorkspaceSession.navigate("start")
                break
            case Qt.Key_End:
                WorkspaceSession.navigate("end")
                break
            default:
                return
            }
            event.accepted = true
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 16

            // The board takes the space the move list leaves, and stays
            // square inside it.
            Item {
                id: boardArea
                Layout.fillWidth: true
                Layout.fillHeight: true

                Board {
                    id: board
                    anchors.centerIn: parent
                    side: Math.max(0, Math.min(boardArea.width, boardArea.height))

                    onPromotionRequested: function (from, to, roles) {
                        promotion.ask(from, to, roles)
                    }
                }
            }

            // The Game Record as a player reads it: the moves in SAN, and where
            // in them the board currently is.
            ColumnLayout {
                id: record
                Layout.preferredWidth: 220
                Layout.maximumWidth: 260
                Layout.fillHeight: true
                spacing: 8

                Label {
                    text: qsTr("Moves")
                    font.bold: true
                    color: Theme.foreground
                }

                ListView {
                    id: moves
                    objectName: "moveList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: WorkspaceSession.moveList
                    // Follow play, and follow the player while they navigate.
                    currentIndex: WorkspaceSession.cursor - 1
                    onCountChanged: positionViewAtIndex(count - 1, ListView.Contain)

                    delegate: ItemDelegate {
                        required property int index
                        required property var modelData

                        objectName: "move:" + (index + 1)
                        width: moves.width
                        // The position after this move is the one on screen.
                        highlighted: index + 1 === WorkspaceSession.cursor
                        text: (modelData.side === "white"
                               ? modelData.number + ". "
                               : modelData.number + "... ") + modelData.san
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Button {
                        objectName: "startButton"
                        Layout.fillWidth: true
                        text: qsTr("⏮")
                        enabled: WorkspaceSession.cursor > 0
                        onClicked: WorkspaceSession.navigate("start")
                    }
                    Button {
                        objectName: "backwardButton"
                        Layout.fillWidth: true
                        text: qsTr("◀")
                        enabled: WorkspaceSession.cursor > 0
                        onClicked: WorkspaceSession.navigate("backward")
                    }
                    Button {
                        objectName: "forwardButton"
                        Layout.fillWidth: true
                        text: qsTr("▶")
                        enabled: WorkspaceSession.reviewing
                        onClicked: WorkspaceSession.navigate("forward")
                    }
                    Button {
                        objectName: "endButton"
                        Layout.fillWidth: true
                        text: qsTr("⏭")
                        enabled: WorkspaceSession.reviewing
                        onClicked: WorkspaceSession.navigate("end")
                    }
                }

                Label {
                    objectName: "reviewLabel"
                    Layout.fillWidth: true
                    visible: WorkspaceSession.reviewing
                    wrapMode: Text.WordWrap
                    color: Theme.foreground
                    text: qsTr("Reviewing an earlier position — play continues at the last move.")
                }
            }
        }
    }

    // The choice a promoting pawn needs before its move exists.
    Dialog {
        id: promotion
        objectName: "promotionDialog"
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.CloseOnEscape
        title: qsTr("Promote to")

        property string from: ""
        property string to: ""
        property var roles: []

        function ask(fromSquare, toSquare, offered) {
            from = fromSquare
            to = toSquare
            roles = offered
            open()
        }

        onRejected: board.cancel()

        RowLayout {
            spacing: 8

            Repeater {
                model: promotion.roles

                Button {
                    required property string modelData

                    objectName: "promote:" + modelData
                    implicitWidth: 64
                    implicitHeight: 64
                    // The choice is shown in the Piece Set the player is
                    // playing with, so it reads as the piece it will become.
                    icon.source: PieceSet.artwork(
                        WorkspaceSession.sideToMove + "_" + modelData)
                    icon.width: 44
                    icon.height: 44
                    ToolTip.text: modelData
                    ToolTip.visible: hovered
                    onClicked: {
                        promotion.close()
                        board.promote(promotion.from, promotion.to, modelData)
                    }
                }
            }
        }
    }
}
