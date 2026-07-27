import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omachess

// The Omachess hybrid cockpit: Personal Library rail · board · right rail,
// with tabs for the records currently open.
//
// Everything on screen comes from WorkspaceSession, which is filled by core
// events. This file decides how a game looks, never what it contains.
ApplicationWindow {
    id: workspace

    // An ordinary resizable window: no fixed size, no compositor hints, so
    // dwindle and scrolling layouts can tile it like any other application.
    width: 1100
    height: 720
    minimumWidth: 640
    minimumHeight: 480
    visible: true
    title: qsTr("Omachess")
    color: Theme.background

    Component.onCompleted: WorkspaceSession.describeBoard()

    // Fail-closed Live Store open: the workspace cannot play without it.
    Rectangle {
        anchors.fill: parent
        visible: WorkspaceSession.storeError.length > 0
        z: 10
        color: Theme.background

        Label {
            objectName: "storeErrorLabel"
            anchors.centerIn: parent
            width: parent.width * 0.8
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            text: WorkspaceSession.storeError
        }
    }

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
                text: WorkspaceSession.positionSetup
                      ? qsTr("Position Setup — %1").arg(WorkspaceSession.positionClass)
                      : WorkspaceSession.gameOver
                      ? WorkspaceSession.resultLabel + " (" + WorkspaceSession.resultScore + ")"
                      : (WorkspaceSession.sideToMove === "white"
                         ? qsTr("White to move") : qsTr("Black to move"))
                        + (WorkspaceSession.inCheck ? qsTr(" — in check") : "")
                font.bold: WorkspaceSession.gameOver
                elide: Text.ElideRight
            }

            Button {
                objectName: "newGameButton"
                text: qsTr("New game")
                onClicked: WorkspaceSession.newGame()
            }

            Button {
                objectName: "positionSetupButton"
                text: qsTr("Position Setup")
                onClicked: WorkspaceSession.beginPositionSetup()
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
            spacing: 0

            // ── Personal Library rail ────────────────────────────────────
            Rectangle {
                id: libraryRail
                objectName: "libraryRail"
                Layout.preferredWidth: 220
                Layout.maximumWidth: 260
                Layout.fillHeight: true
                color: Theme.panel

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Label {
                        objectName: "libraryHeading"
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        Layout.topMargin: 10
                        Layout.bottomMargin: 8
                        text: qsTr("Personal Library")
                        font.bold: true
                        font.pixelSize: 11
                        font.capitalization: Font.AllUppercase
                        color: Theme.muted
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.muted
                        opacity: 0.4
                    }

                    ListView {
                        id: libraryList
                        objectName: "libraryList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: WorkspaceSession.libraryRecords

                        delegate: ItemDelegate {
                            required property var modelData
                            required property int index

                            id: libraryItem
                            objectName: "library:" + modelData.id
                            width: libraryList.width
                            highlighted: modelData.id === WorkspaceSession.activeRecordId

                            contentItem: ColumnLayout {
                                spacing: 2

                                Label {
                                    objectName: "libraryTitle:" + modelData.id
                                    Layout.fillWidth: true
                                    text: modelData.title
                                    color: Theme.foreground
                                    elide: Text.ElideRight
                                    font.bold: true
                                }

                                Label {
                                    objectName: "libraryMeta:" + modelData.id
                                    Layout.fillWidth: true
                                    text: {
                                        const kind = modelData.kind === "analysis"
                                                   ? qsTr("Analysis") : qsTr("Played")
                                        const score = modelData.resultScore
                                        return score && score.length > 0
                                               ? kind + " · " + score
                                               : kind
                                    }
                                    color: Theme.muted
                                    font.pixelSize: 11
                                }
                            }

                            background: Rectangle {
                                color: libraryItem.highlighted ? Theme.selection
                                       : (libraryItem.hovered ? Theme.selection : "transparent")
                                opacity: libraryItem.highlighted || libraryItem.hovered ? 1 : 0
                            }

                            onClicked: WorkspaceSession.openRecord(modelData.id)
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: libraryList.count === 0
                            text: qsTr("No records yet")
                            color: Theme.muted
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.muted
                opacity: 0.4
            }

            // ── Centre: tabs + full-size board ───────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Open-record tabs.
                Rectangle {
                    objectName: "tabBar"
                    Layout.fillWidth: true
                    Layout.preferredHeight: WorkspaceSession.openTabs.length > 0 ? 36 : 0
                    visible: WorkspaceSession.openTabs.length > 0
                    color: Theme.panel
                    clip: true

                    Row {
                        id: tabRow
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        spacing: 2

                        Repeater {
                            model: WorkspaceSession.openTabs

                            Rectangle {
                                required property var modelData
                                required property int index

                                id: tabChip
                                objectName: "tab:" + modelData.id
                                height: parent.height - 4
                                anchors.verticalCenter: parent.verticalCenter
                                width: tabLabel.implicitWidth + closeTabButton.width + 20
                                radius: 4
                                color: modelData.id === WorkspaceSession.activeRecordId
                                       ? Theme.background : "transparent"
                                border.color: modelData.id === WorkspaceSession.activeRecordId
                                              ? Theme.muted : "transparent"
                                border.width: 1

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 4
                                    spacing: 4

                                    Label {
                                        id: tabLabel
                                        objectName: "tabTitle:" + modelData.id
                                        Layout.fillWidth: true
                                        text: modelData.title
                                        color: Theme.foreground
                                        elide: Text.ElideRight
                                        font.bold: modelData.id === WorkspaceSession.activeRecordId
                                    }

                                    Button {
                                        id: closeTabButton
                                        objectName: "closeTab:" + modelData.id
                                        Layout.preferredWidth: 22
                                        Layout.preferredHeight: 22
                                        flat: true
                                        text: "×"
                                        onClicked: WorkspaceSession.closeTab(modelData.id)
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    z: -1
                                    onClicked: WorkspaceSession.openRecord(modelData.id)
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    visible: WorkspaceSession.openTabs.length > 0
                    color: Theme.muted
                    opacity: 0.4
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12

                        // Offered after restart when a prior Game Record can be
                        // restored and open tabs did not already restore it.
                        Frame {
                            objectName: "restoreCard"
                            visible: WorkspaceSession.restoreAvailable
                            Layout.fillWidth: true

                            RowLayout {
                                anchors.fill: parent
                                spacing: 12

                                Label {
                                    objectName: "restoreLabel"
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: WorkspaceSession.restoreLabel
                                }

                                Button {
                                    objectName: "restoreButton"
                                    text: qsTr("Restore")
                                    onClicked: WorkspaceSession.restoreRecord()
                                }

                                Button {
                                    objectName: "dismissRestoreButton"
                                    text: qsTr("Dismiss")
                                    onClicked: WorkspaceSession.dismissRestore()
                                }
                            }
                        }

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

                        Frame {
                            visible: WorkspaceSession.positionSetup
                            Layout.fillWidth: true

                            ColumnLayout {
                                anchors.fill: parent

                                RowLayout {
                                    Layout.fillWidth: true
                                    TextField {
                                        id: fenInput
                                        objectName: "fenInput"
                                        Layout.fillWidth: true
                                        text: WorkspaceSession.setupFen
                                        placeholderText: qsTr("FEN")
                                    }
                                    Button {
                                        objectName: "applyFenButton"
                                        text: qsTr("Apply FEN")
                                        onClicked: WorkspaceSession.setSetupFen(fenInput.text)
                                    }
                                }

                                Label {
                                    objectName: "fenErrorLabel"
                                    visible: WorkspaceSession.setupError.length > 0
                                    text: WorkspaceSession.setupError
                                    color: Theme.red
                                }

                                Label {
                                    objectName: "positionClassLabel"
                                    text: WorkspaceSession.positionClass
                                    font.bold: true
                                }

                                Label {
                                    objectName: "positionCapabilitiesLabel"
                                    text: WorkspaceSession.positionCapabilities
                                }

                                Button {
                                    objectName: "startSetupGameButton"
                                    text: qsTr("Start Played Game")
                                    enabled: WorkspaceSession.positionClass === "Rule-valid Position"
                                    onClicked: WorkspaceSession.startSetupGame()
                                }

                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Repeater {
                                        model: ["white_king", "white_queen", "white_rook",
                                                "white_bishop", "white_knight", "white_pawn",
                                                "black_king", "black_queen", "black_rook",
                                                "black_bishop", "black_knight", "black_pawn"]
                                        Button {
                                            required property string modelData
                                            objectName: "tray:" + modelData
                                            text: modelData.replace("_", " ")
                                            onClicked: board.setupPiece = modelData
                                        }
                                    }
                                    Button {
                                        objectName: "removePieceTool"
                                        text: qsTr("Remove")
                                        onClicked: board.setupPiece = "__remove"
                                    }
                                    Button {
                                        objectName: "relocatePieceTool"
                                        text: qsTr("Relocate")
                                        onClicked: board.setupPiece = "__move"
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.muted
                opacity: 0.4
            }

            // ── Right rail (moves now; Live Position Analysis later) ─────
            Rectangle {
                id: rightRail
                objectName: "rightRail"
                Layout.preferredWidth: 240
                Layout.maximumWidth: 280
                Layout.fillHeight: true
                color: Theme.panel

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    Label {
                        objectName: "rightRailHeading"
                        text: WorkspaceSession.positionSetup ? qsTr("Position Setup") : qsTr("Moves")
                        font.bold: true
                        font.pixelSize: 11
                        font.capitalization: Font.AllUppercase
                        color: Theme.muted
                    }

                    ListView {
                        id: moves
                        objectName: "moveList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: WorkspaceSession.moveList
                        visible: !WorkspaceSession.positionSetup
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
                        visible: !WorkspaceSession.positionSetup

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
                        visible: !WorkspaceSession.positionSetup && WorkspaceSession.reviewing
                        wrapMode: Text.WordWrap
                        color: Theme.foreground
                        text: qsTr("Reviewing an earlier position — play continues at the last move.")
                    }
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
