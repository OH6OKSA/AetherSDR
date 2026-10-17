import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Network and performance, as the desktop's diagnostics: quality, link
// timing, packet integrity per stream, traffic, and the app's CPU and frame
// rate. Refreshed once a second while open.
Popup {
    id: dialog
    objectName: "networkDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(460, (parent ? parent.width : 460) - 24)
    modal: true
    padding: Theme.spacing
    property var d: ({})
    function refresh() { d = Engine.networkDetails() }
    onOpened: refresh()
    Timer { interval: 1000; repeat: true; running: dialog.opened; onTriggered: dialog.refresh() }
    background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.surfaceRaised }

    component Row2: RowLayout {
        property string label
        property string value
        property color valueColor: Theme.text
        Layout.fillWidth: true
        Label { text: parent.label; color: Theme.textDim; font.pixelSize: 14; Layout.fillWidth: true }
        Label { text: parent.value; color: parent.valueColor; font.pixelSize: 14; font.bold: true }
    }
    component Section: Label {
        color: Theme.accent
        font.pixelSize: 13
        font.bold: true
        Layout.topMargin: 6
    }

    contentItem: ColumnLayout {
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Network & performance"); color: Theme.text; font.pixelSize: 18; font.bold: true; Layout.fillWidth: true }
            NetworkBadge { quality: dialog.d.quality || "Off"; rttMs: dialog.d.rttMs || 0 }
        }
        Row2 { label: qsTr("Connection"); value: dialog.d.path || "–" }
        Row2 { label: qsTr("Radio"); value: dialog.d.radio || "–" }

        Section { text: qsTr("LINK TIMING") }
        Row2 { label: qsTr("Round trip (RTT)"); value: (dialog.d.rttMs || 0) + " ms" }
        Row2 { label: qsTr("Audio jitter"); value: (dialog.d.jitterMs || 0) + " ms" }
        Row2 {
            label: qsTr("Audio packet gap")
            value: qsTr("%1 ms · max %2 ms").arg(dialog.d.audioGapMs || 0).arg(dialog.d.audioGapMaxMs || 0)
        }

        Section { text: qsTr("PACKET INTEGRITY") }
        Row2 {
            label: qsTr("Recent loss (%1 s)").arg(dialog.d.lossWindowS || 0)
            value: qsTr("%1 %  (%2 / %3)").arg((dialog.d.lossPercent || 0).toFixed(2))
                       .arg(dialog.d.lossDrops || 0).arg(dialog.d.lossPackets || 0)
            valueColor: (dialog.d.lossPercent || 0) >= 2 ? Theme.transmit
                      : (dialog.d.lossPercent || 0) >= 0.5 ? Theme.warning : Theme.text
        }
        Row2 {
            label: qsTr("Session lost packets")
            value: (dialog.d.sessionDrops || 0) + " / " + (dialog.d.sessionPackets || 0)
        }
        Repeater {
            model: dialog.d.streams || []
            delegate: Row2 {
                required property var modelData
                label: "   " + modelData.name
                value: modelData.errors + " / " + modelData.packets
            }
        }

        Section { text: qsTr("TRAFFIC") }
        Row2 {
            label: qsTr("Receiving")
            value: qsTr("%1 kbit/s · %2 MB").arg(Math.round(dialog.d.rxKbps || 0)).arg((dialog.d.rxMB || 0).toFixed(1))
        }
        Row2 {
            label: qsTr("Sending")
            value: qsTr("%1 kbit/s · %2 MB").arg(Math.round(dialog.d.txKbps || 0)).arg((dialog.d.txMB || 0).toFixed(1))
        }
        Section { text: qsTr("PERFORMANCE") }
        Row2 {
            label: qsTr("App CPU, all %1 cores").arg(Engine.cpuCores)
            value: Math.round(Engine.cpuPercent) + " %"
            valueColor: Engine.cpuPercent >= 80 ? Theme.transmit : Engine.cpuPercent >= 50 ? Theme.warning : Theme.text
        }
        // All threads summed, one core = 100 %: 102 % is about one core's
        // worth of work, not one core full. The busiest thread tells that.
        Row2 {
            label: qsTr("App CPU, one core = 100 %")
            value: Math.round(Engine.cpuPercentOfCore) + " %"
        }
        Row2 {
            visible: Engine.busyThreads.length > 0
            label: qsTr("Busiest threads")
            value: Engine.busyThreads.slice(0, 2).join(", ")
            valueColor: Engine.busyThreads.length > 0 && parseInt(Engine.busyThreads[0].split(" ").slice(-2)[0]) >= 90
                        ? Theme.warning : Theme.text
        }
        Row2 {
            label: qsTr("Screen frames drawn")
            value: Engine.uiFps + " /s"
            valueColor: Engine.uiFps > 0 && Engine.uiFps < 20 ? Theme.warning : Theme.text
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("App CPU adds up all the app's threads; a thread near 100 % is one core full. %1 shows apps no GPU load: frames drawn per second falling well below the display's rate means the graphics are not keeping up.")
                  .arg(Qt.platform.os === "ios" ? "iOS" : Qt.platform.os === "android" ? "Android" : qsTr("The system"))
            color: Theme.textDim
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
        AppButton {
            text: qsTr("Close")
            Layout.alignment: Qt.AlignRight
            Layout.topMargin: 6
            leftPadding: 16
            rightPadding: 16
            onClicked: dialog.close()
        }
    }
}
