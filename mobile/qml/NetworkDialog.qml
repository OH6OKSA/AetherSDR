import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Network diagnostics, as the desktop's: quality, link timing, packet
// integrity per stream and traffic. Refreshed once a second while open.
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
            Label { text: qsTr("Network"); color: Theme.text; font.pixelSize: 18; font.bold: true; Layout.fillWidth: true }
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
