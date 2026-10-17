import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Receive extras of the active slice: RIT, XIT, squelch and the notch
// filters (antennas, AGC, AGC-T and RF gain are on the info panel). Shared by the tablet side panel
// and the drawer.
ColumnLayout {
    id: rx
    spacing: Theme.spacing / 2
    implicitWidth: 200

    // RIT / XIT rows: on/off, -/+ (hold to run faster), value (tap resets to 0).
    component OffsetRow: RowLayout {
        id: row
        property string label
        property bool on
        property int hz
        signal toggled(bool on)
        signal changed(int hz)
        Layout.fillWidth: true
        property int acc: 0
        function step(n, dir) {
            if (n === 0) acc = hz
            acc += dir * (n < 10 ? 10 : n < 25 ? 50 : 100)
            changed(acc)
        }
        AppButton {
            text: row.label
            checked: row.on
            Layout.preferredWidth: 60
            leftPadding: 2
            rightPadding: 2
            focusPolicy: Qt.NoFocus
            onClicked: row.toggled(!row.on)
        }
        // Hold to run: 10 Hz steps, then 50 Hz, then 100 Hz the longer it is
        // held. Counted from the value at the press, not the radio's echo.
        RepeatButton {
            text: "−"
            Layout.preferredWidth: 44
            onStepped: (n) => row.step(n, -1)
        }
        Label {
            text: (row.hz > 0 ? "+" : "") + row.hz + " Hz"
            color: row.on ? Theme.text : Theme.textDim
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 15
            Layout.fillWidth: true
            TapHandler { onTapped: row.changed(0) }
        }
        RepeatButton {
            text: "+"
            Layout.preferredWidth: 44
            onStepped: (n) => row.step(n, 1)
        }
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

    // Tracking notch filters: the global switch and +TNF (at the passband's
    // centre, as the desktop); holding a finger on the display adds one
    // there, a tap on a notch opens its settings.
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.tnfAvailable
        AppButton {
            text: qsTr("TNF")
            checked: Engine.tnfEnabled
            Layout.preferredWidth: 60
            leftPadding: 2
            rightPadding: 2
            onClicked: Engine.tnfEnabled = !Engine.tnfEnabled
        }
        AppButton {
            text: qsTr("+ TNF")
            enabled: Engine.canAddTnf
            Layout.preferredWidth: 72
            onClicked: Engine.addTnf(0)
        }
        Label {
            Layout.fillWidth: true
            text: Engine.tnfs.length === 0 ? qsTr("Hold the display to add a notch.")
                : Engine.tnfs.length === 1 ? qsTr("1 notch; tap it on the display to adjust.")
                : qsTr("%1 notches; tap one on the display to adjust.").arg(Engine.tnfs.length)
            color: Theme.textDim
            font.pixelSize: Theme.smallFont
            wrapMode: Text.WordWrap
        }
    }
}
