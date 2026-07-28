import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Ui

// A deliberately thin shell control surface. Background Job truth remains in
// the worker, so recreating this item after an omarchy-shell restart is safe.
BarWidget {
    id: root
    moduleName: "com.omachess.background-controls"

    property var jobs: []
    property var previousStates: ({})
    property bool popupOpen: false
    readonly property color foreground: bar ? bar.foreground : Color.foreground
    readonly property color popupBackground: Color.popups.background
    readonly property color popupBorder: Color.popups.border
    readonly property bool hasJobs: jobs.length > 0
    readonly property int activeJobs: jobs.filter(function(job) {
        return job.state === "running" || job.state === "paused" || job.state === "queued"
    }).length

    visible: hasJobs
    implicitWidth: visible ? (vertical ? barSize : 150) : 0
    implicitHeight: visible ? (vertical ? 150 : barSize) : 0

    function withAlpha(color, alpha) {
        return Qt.rgba(color.r, color.g, color.b, alpha)
    }

    function refreshJobs() {
        if (!jobsProcess.running)
            jobsProcess.running = true
    }

    function applyJobs(encoded) {
        let nextJobs
        try {
            nextJobs = JSON.parse(String(encoded).trim())
        } catch (error) {
            return
        }
        if (!Array.isArray(nextJobs))
            return

        const nextStates = ({})
        for (const job of nextJobs) {
            if (job.id === undefined)
                continue
            nextStates[job.id] = job.state
            const oldState = previousStates[job.id]
            const terminal = job.state === "complete" || job.state === "failed"
            if ((oldState === undefined || oldState !== job.state) && terminal)
                notifyProcess.enqueue(job.state, job.id, job.recordId || "", job.updatedAt || "")
        }
        previousStates = nextStates
        jobs = nextJobs
        if (jobs.length === 0)
            popupOpen = false
    }

    function hasControl(job, control) {
        return Array.isArray(job.controls) && job.controls.indexOf(control) >= 0
    }

    function percent(job) {
        if (!job.total || job.total <= 0)
            return 0
        return Math.max(0, Math.min(100, Math.round(100 * job.checkpoint / job.total)))
    }

    function openRecord(job) {
        if (!job.recordId)
            return
        Quickshell.execDetached(["omachess", "--record", String(job.recordId)])
        popupOpen = false
    }

    function sendControl(action, job) {
        controlProcess.command = ["omachess-background-control", "control", action, String(job.id)]
        controlProcess.running = true
        popupOpen = false
    }

    Timer {
        interval: 1000
        repeat: true
        running: true
        triggeredOnStart: true
        onTriggered: root.refreshJobs()
    }

    Process {
        id: jobsProcess
        command: ["omachess-background-control", "jobs"]
        stdout: StdioCollector { id: jobsOutput }
        onExited: function(exitCode) {
            if (exitCode === 0)
                root.applyJobs(jobsOutput.text)
        }
    }

    Process {
        id: controlProcess
        command: []
        onExited: function(exitCode) {
            if (exitCode === 0)
                root.refreshJobs()
        }
    }

    // Notifications intentionally go through the packaged helper. The helper
    // uses the stable desktop identity and probes notification capabilities.
    QtObject {
        id: notifyProcess
        property var queue: []
        function enqueue(state, id, recordId, updatedAt) {
            queue = queue.concat([{state: state, id: id, recordId: recordId, updatedAt: updatedAt}])
            pump()
        }
        function pump() {
            if (notification.running || queue.length === 0)
                return
            const item = queue[0]
            queue = queue.slice(1)
            startFor(item.state, item.id, item.recordId, item.updatedAt)
        }
        function startFor(state, id, recordId, updatedAt) {
            const title = state === "failed" ? "Background analysis failed" : "Background analysis complete"
            const body = recordId === "" ? "An Omachess Background Job changed state."
                                         : "Job " + id + " for record " + recordId + "."
            notification.command = ["omachess-background-control", "notify", state === "failed" ? "failed" : "complete", String(id), String(updatedAt), title, body]
            notification.running = true
        }
    }

    Process {
        id: notification
        command: []
        onExited: notifyProcess.pump()
    }

    Rectangle {
        id: chip
        objectName: "backgroundControlsChip"
        anchors.centerIn: parent
        width: chipLabel.implicitWidth + 24
        height: Math.max(barSize - 8, 30)
        radius: height / 2
        color: mouse.containsMouse ? root.withAlpha(Color.accent, 0.28)
                                    : root.withAlpha(root.foreground, 0.10)

        Text {
            id: chipLabel
            anchors.centerIn: parent
            color: root.foreground
            text: root.activeJobs > 0 ? root.activeJobs + " jobs" : root.jobs.length + " done"
            font.pixelSize: 12
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.popupOpen = !root.popupOpen
        }
    }

    KeyboardPanel {
        id: jobsPanel
        anchorItem: chip
        owner: root
        bar: root.bar
        open: root.popupOpen
        contentWidth: 390
        contentHeight: Math.min(520, jobsColumn.implicitHeight + 32)

        Rectangle {
            anchors.fill: parent
            radius: 12
            color: root.popupBackground
            border.color: root.popupBorder
            border.width: 1

            ColumnLayout {
                id: jobsColumn
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    text: "Omachess Background Jobs"
                    color: root.foreground
                    font.bold: true
                    font.pixelSize: 15
                }

                Repeater {
                    model: root.jobs
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 6

                        Text {
                            Layout.fillWidth: true
                            text: modelData.kind === "computer_analysis" ? "Computer analysis" : modelData.kind
                            color: root.foreground
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: (modelData.recordId || "No record") + " · " + modelData.state
                            color: root.withAlpha(root.foreground, 0.68)
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.checkpoint + " / " + modelData.total + " · " + root.percent(modelData) + "%"
                            color: root.withAlpha(root.foreground, 0.68)
                            font.pixelSize: 11
                        }
                        ProgressBar {
                            Layout.fillWidth: true
                            from: 0
                            to: 100
                            value: root.percent(modelData)
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Button {
                                objectName: "backgroundJobPause:" + modelData.id
                                text: "Pause"
                                visible: root.hasControl(modelData, "pause")
                                onClicked: root.sendControl("pause", modelData)
                            }
                            Button {
                                objectName: "backgroundJobResume:" + modelData.id
                                text: "Resume"
                                visible: root.hasControl(modelData, "resume")
                                onClicked: root.sendControl("resume", modelData)
                            }
                            Button {
                                objectName: "backgroundJobCancel:" + modelData.id
                                text: "Cancel"
                                visible: root.hasControl(modelData, "cancel")
                                onClicked: root.sendControl("cancel", modelData)
                            }
                            Button {
                                objectName: "backgroundJobDismiss:" + modelData.id
                                text: "Dismiss"
                                visible: root.hasControl(modelData, "dismiss")
                                onClicked: root.sendControl("dismiss", modelData)
                            }
                            Button {
                                objectName: "backgroundJobOpen:" + modelData.id
                                text: "Open record"
                                visible: root.hasControl(modelData, "open")
                                onClicked: root.openRecord(modelData)
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: root.withAlpha(root.foreground, 0.10)
                        }
                    }
                }
            }
        }
    }

    IpcHandler {
        target: root.moduleName
        function toggle() {
            root.popupOpen = !root.popupOpen
        }
        function close() {
            root.popupOpen = false
        }
        function refresh() {
            root.refreshJobs()
        }
    }
}
