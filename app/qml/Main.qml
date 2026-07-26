import QtQuick
import QtQuick.Controls
import Omachess

// The Omachess workspace window.
//
// Everything on screen comes from WorkspaceSession, which is filled by core
// events. This file decides how the board looks, never what it contains.
ApplicationWindow {
    id: workspace

    // An ordinary resizable window: no fixed size, no compositor hints, so
    // dwindle and scrolling layouts can tile it like any other application.
    width: 960
    height: 720
    minimumWidth: 420
    minimumHeight: 360
    visible: true
    title: qsTr("Omachess")

    Component.onCompleted: WorkspaceSession.describeBoard()

    header: ToolBar {
        Label {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Standard chess")
        }

        Button {
            objectName: "flipButton"
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Flip board (F)")
            onClicked: WorkspaceSession.flipBoard()
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
            if (event.key === Qt.Key_F) {
                WorkspaceSession.flipBoard()
                event.accepted = true
            }
        }

        Board {
            anchors.centerIn: parent
            // Square, and always inside the space the window has left.
            side: Math.max(0, Math.min(parent.width, parent.height) - 32)
        }
    }
}
