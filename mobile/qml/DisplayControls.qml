import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Display levels: waterfall black level and colour gain (on top of the
// auto-tracked noise floor; Auto off holds it), and the spectrum trace's
// range (Auto, or reference level and range). Remembered on this device.
ColumnLayout {
    spacing: Theme.spacing / 2
    implicitWidth: 200

    // Both meter faces at once: bars or analog gauges (each meter's own
    // chooser sets just that one).
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            text: qsTr("Bar meters")
            checked: !Engine.analogLeft && !Engine.analogRight
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.analogMeters = false
        }
        AppButton {
            text: qsTr("Analog meters")
            checked: Engine.analogMeters
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.analogMeters = true
        }
    }
    // 2D waterfall or the 3D stacked trace.
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            text: qsTr("2D waterfall")
            checked: !Engine.spectrum3D
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.spectrum3D = false
        }
        AppButton {
            text: qsTr("3D")
            checked: Engine.spectrum3D
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.spectrum3D = true
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Waterfall"); color: Theme.textDim; font.pixelSize: 16; Layout.fillWidth: true }
        AppButton {
            text: qsTr("Auto")
            checked: Engine.wfAutoLevel
            focusPolicy: Qt.NoFocus
            onClicked: Engine.wfAutoLevel = !Engine.wfAutoLevel
        }
    }
    Label { text: qsTr("Black level  %1").arg(Engine.wfBlackLevel); color: Theme.textDim; font.pixelSize: 15 }
    Slider {
        Layout.fillWidth: true
        from: 0; to: 100; stepSize: 1
        value: Engine.wfBlackLevel
        onMoved: Engine.wfBlackLevel = Math.round(value)
    }
    Label { text: qsTr("Colour gain  %1").arg(Engine.wfColorGain); color: Theme.textDim; font.pixelSize: 15 }
    Slider {
        Layout.fillWidth: true
        from: 0; to: 100; stepSize: 1
        value: Engine.wfColorGain
        onMoved: Engine.wfColorGain = Math.round(value)
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacing / 2
        Label { text: qsTr("Spectrum"); color: Theme.textDim; font.pixelSize: 16; Layout.fillWidth: true }
        AppButton {
            text: qsTr("Auto")
            checked: Engine.traceAutoRange
            focusPolicy: Qt.NoFocus
            onClicked: Engine.traceAutoRange = !Engine.traceAutoRange
        }
    }
    Label {
        text: qsTr("Reference  %1 dBm").arg(Engine.traceRefDbm)
        color: Theme.textDim
        opacity: Engine.traceAutoRange ? 0.5 : 1
        font.pixelSize: 15
    }
    Slider {
        Layout.fillWidth: true
        enabled: !Engine.traceAutoRange
        from: -140; to: 0; stepSize: 1
        value: Engine.traceRefDbm
        onMoved: Engine.traceRefDbm = Math.round(value)
    }
    Label {
        text: qsTr("Range  %1 dB").arg(Engine.traceRangeDb)
        color: Theme.textDim
        opacity: Engine.traceAutoRange ? 0.5 : 1
        font.pixelSize: 15
    }
    Slider {
        Layout.fillWidth: true
        enabled: !Engine.traceAutoRange
        from: 20; to: 150; stepSize: 5
        value: Engine.traceRangeDb
        onMoved: Engine.traceRangeDb = Math.round(value)
    }
}
