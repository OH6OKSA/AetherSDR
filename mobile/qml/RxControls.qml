import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Receive extras of the active slice: RIT, XIT, squelch and antennas.
// Shared by the tablet side panel and the controls drawer.
ColumnLayout {
    id: rx
    spacing: Theme.spacing / 2
    implicitWidth: 200

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
            checkable: true
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
            checkable: true
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

    // Antennas: only ports the radio publishes.
    Label {
        visible: Engine.rxAntennas.length > 0
        text: qsTr("RX antenna")
        color: Theme.textDim
        font.pixelSize: 14
    }
    Flow {
        Layout.fillWidth: true
        spacing: 4
        visible: Engine.rxAntennas.length > 0
        Repeater {
            model: Engine.rxAntennas
            delegate: AppButton {
                required property string modelData
                text: modelData
                checkable: true
                checked: Engine.rxAntenna === modelData
                leftPadding: 6
                rightPadding: 6
                font.pixelSize: 13
                focusPolicy: Qt.NoFocus
                onClicked: Engine.rxAntenna = modelData
            }
        }
    }
    Label {
        visible: Engine.txAntennas.length > 0
        text: qsTr("TX antenna")
        color: Theme.textDim
        font.pixelSize: 14
    }
    Flow {
        Layout.fillWidth: true
        spacing: 4
        visible: Engine.txAntennas.length > 0
        Repeater {
            model: Engine.txAntennas
            delegate: AppButton {
                required property string modelData
                text: modelData
                checkable: true
                checked: Engine.txAntenna === modelData
                leftPadding: 6
                rightPadding: 6
                font.pixelSize: 13
                focusPolicy: Qt.NoFocus
                onClicked: Engine.txAntenna = modelData
            }
        }
    }
}
