import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Receive extras of the active slice: AGC threshold, RIT, XIT, squelch,
// and RF gain (antennas and the AGC mode are on the info panel). Shared by
// the tablet side panel and the drawer.
ColumnLayout {
    id: rx
    spacing: Theme.spacing / 2
    implicitWidth: 200

    // AGC threshold (AGC-T); the AGC mode is on the info panel.
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("AGC-T  %1").arg(Engine.agcThreshold); color: Theme.textDim; font.pixelSize: 14; Layout.preferredWidth: 80 }
        Slider {
            Layout.fillWidth: true
            enabled: Engine.connectionState === "connected" && Engine.agcMode !== "off"
            from: 0; to: 100; stepSize: 1
            value: Engine.agcThreshold
            onMoved: Engine.agcThreshold = Math.round(value)
        }
    }

    // RIT / XIT rows: on/off, -/+ 10 Hz, value (tap resets to 0).
    component OffsetRow: RowLayout {
        id: row
        property string label
        property bool on
        property int hz
        signal toggled(bool on)
        signal changed(int hz)
        Layout.fillWidth: true
        AppButton {
            text: row.label
            checked: row.on
            Layout.preferredWidth: 60
            leftPadding: 2
            rightPadding: 2
            focusPolicy: Qt.NoFocus
            onClicked: row.toggled(!row.on)
        }
        AppButton { text: "−"; Layout.preferredWidth: 44; focusPolicy: Qt.NoFocus; onClicked: row.changed(row.hz - 10) }
        Label {
            text: (row.hz > 0 ? "+" : "") + row.hz + " Hz"
            color: row.on ? Theme.text : Theme.textDim
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 15
            Layout.fillWidth: true
            TapHandler { onTapped: row.changed(0) }
        }
        AppButton { text: "+"; Layout.preferredWidth: 44; focusPolicy: Qt.NoFocus; onClicked: row.changed(row.hz + 10) }
    }

    OffsetRow {
        label: qsTr("RIT")
        on: Engine.ritOn
        hz: Engine.ritHz
        visible: Engine.mode !== "FM"
        onToggled: (on) => Engine.ritOn = on
        onChanged: (hz) => Engine.ritHz = hz
    }
    OffsetRow {
        label: qsTr("XIT")
        on: Engine.xitOn
        hz: Engine.xitHz
        visible: Engine.mode !== "FM"
        onToggled: (on) => Engine.xitOn = on
        onChanged: (hz) => Engine.xitHz = hz
    }

    RowLayout {
        Layout.fillWidth: true
        visible: Engine.squelchAvailable
        AppButton {
            text: qsTr("SQL")
            checked: Engine.squelchOn
            Layout.preferredWidth: 60
            leftPadding: 2
            rightPadding: 2
            focusPolicy: Qt.NoFocus
            onClicked: Engine.squelchOn = !Engine.squelchOn
        }
        Slider {
            Layout.fillWidth: true
            from: 0
            to: 100
            stepSize: 1
            value: Engine.squelchLevel
            enabled: Engine.squelchOn
            onMoved: Engine.squelchLevel = Math.round(value)
        }
    }

    // RF gain (preamp / attenuator) of the panadapter, in the radio's steps.
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.connectionState === "connected"
        Label {
            text: qsTr("RF gain  %1 dB").arg(Engine.rfGain)
            color: Theme.textDim
            font.pixelSize: 14
            Layout.preferredWidth: 110
        }
        Slider {
            Layout.fillWidth: true
            from: Engine.rfGainLow
            to: Engine.rfGainHigh
            stepSize: Engine.rfGainStep
            snapMode: Slider.SnapAlways
            value: Engine.rfGain
            onMoved: Engine.rfGain = Math.round(value)
        }
    }
}
