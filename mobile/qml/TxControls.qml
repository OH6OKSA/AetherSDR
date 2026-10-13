import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Transmit settings shared by the tablet side panel and the controls drawer:
// tune carrier, ATU, speech processor, monitor, power and microphone.
ColumnLayout {
    id: tx
    spacing: Theme.spacing / 2
    // Never ask for more width than the column gives; rows share what is there.
    implicitWidth: 200

    // ---- Tune and ATU (both transmit; routed like PTT) ----
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            id: tuneButton
            text: Engine.tuning ? qsTr("TUNE ●") : qsTr("TUNE")
            enabled: Engine.tuneAvailable || Engine.tuning
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            background: Rectangle {
                implicitHeight: Theme.touchTarget
                radius: Theme.radius
                color: Engine.tuning ? Theme.transmit : tuneButton.down ? "#bdbdbd" : "#e0e0e0"
                opacity: tuneButton.enabled ? 1.0 : 0.5
            }
            onClicked: Engine.toggleTune()
        }
        AppButton {
            text: Engine.atuTuned ? qsTr("ATU ✓") : qsTr("ATU")
            visible: Engine.hasTuner
            enabled: Engine.tuneAvailable
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.atuPressed()
        }
        AppButton {
            text: qsTr("MEM")
            visible: Engine.hasTunerMemories
            checked: Engine.atuMemories
            Layout.preferredWidth: 56
            leftPadding: 2
            rightPadding: 2
            focusPolicy: Qt.NoFocus
            onClicked: Engine.atuMemories = !Engine.atuMemories
        }
    }
    Label {
        visible: Engine.hasTuner && Engine.atuStatusText.length > 0
        text: qsTr("ATU: %1").arg(Engine.atuStatusText)
        color: Theme.textDim
        font.pixelSize: 13
    }
    Label { text: qsTr("Tune power  %1").arg(Engine.tunePower); color: Theme.textDim; font.pixelSize: 15 }
    Slider {
        Layout.fillWidth: true
        from: 0
        to: 100
        stepSize: 1
        value: Engine.tunePower
        enabled: Engine.tuneAvailable || Engine.tuning
        onMoved: Engine.tunePower = Math.round(value)
    }

    // ---- Power ----
    Label { text: qsTr("RF power  %1").arg(Engine.rfPower); color: Theme.textDim; font.pixelSize: 15 }
    Slider {
        Layout.fillWidth: true
        from: 0
        to: 100
        stepSize: 1
        value: Engine.rfPower
        enabled: Engine.transmitAvailable || Engine.canUseDeviceMicrophone || Engine.tuneAvailable
        onMoved: Engine.rfPower = Math.round(value)
    }

    // ---- Speech processor (radio side) ----
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            text: qsTr("PROC")
            checked: Engine.procEnabled
            Layout.preferredWidth: 64
            focusPolicy: Qt.NoFocus
            onClicked: Engine.procEnabled = !Engine.procEnabled
        }
        // Flex: three steps NOR / DX / DX+; other radios get a slider.
        Repeater {
            model: Engine.procLevelMax === 2 ? ["NOR", "DX", "DX+"] : []
            delegate: AppButton {
                required property int index
                required property string modelData
                text: modelData
                checked: Engine.procLevel === index
                enabled: Engine.procEnabled
                Layout.fillWidth: true
                leftPadding: 2
                rightPadding: 2
                focusPolicy: Qt.NoFocus
                onClicked: Engine.procLevel = index
            }
        }
        Slider {
            visible: Engine.procLevelMax !== 2
            Layout.fillWidth: true
            from: 0
            to: Math.max(1, Engine.procLevelMax)
            stepSize: 1
            value: Engine.procLevel
            enabled: Engine.procEnabled
            onMoved: Engine.procLevel = Math.round(value)
        }
    }

    // ---- Monitor ----
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            text: qsTr("MON")
            checked: Engine.monitor
            Layout.preferredWidth: 64
            focusPolicy: Qt.NoFocus
            onClicked: Engine.monitor = !Engine.monitor
        }
        Slider {
            Layout.fillWidth: true
            from: 0
            to: 100
            stepSize: 1
            value: Engine.monitorGain
            onMoved: Engine.monitorGain = Math.round(value)
        }
    }

    // ---- Microphone ----
    Label {
        text: qsTr("Radio mic level  %1").arg(Engine.radioMicLevel)
        color: Theme.textDim
        font.pixelSize: 15
    }
    Slider {
        Layout.fillWidth: true
        from: 0
        to: 100
        stepSize: 1
        value: Engine.radioMicLevel
        onMoved: Engine.radioMicLevel = Math.round(value)
    }
    Label {
        text: qsTr("Device mic gain  %1").arg(Engine.micGain)
        color: Theme.textDim
        font.pixelSize: 15
    }
    Slider {
        Layout.fillWidth: true
        from: 0
        to: 100
        stepSize: 1
        value: Engine.micGain
        enabled: Engine.transmitAvailable
        onMoved: Engine.micGain = Math.round(value)
    }
    AppButton {
        Layout.fillWidth: true
        visible: Engine.canUseDeviceMicrophone
        text: qsTr("Use this device's microphone")
        onClicked: Engine.useDeviceMicrophone()
    }

    // TX audio passband (low and high cut of the transmitted audio).
    Label {
        text: qsTr("TX filter  %1 – %2 Hz").arg(Engine.txFilterLow).arg(Engine.txFilterHigh)
        color: Theme.textDim
        font.pixelSize: 15
        Layout.topMargin: Theme.spacing / 2
    }
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Low"); color: Theme.textDim; font.pixelSize: 13 }
        Slider {
            Layout.fillWidth: true
            enabled: Engine.connectionState === "connected"
            from: 0; to: 1000; stepSize: 10
            value: Engine.txFilterLow
            onMoved: Engine.txFilterLow = Math.round(value)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("High"); color: Theme.textDim; font.pixelSize: 13 }
        Slider {
            Layout.fillWidth: true
            enabled: Engine.connectionState === "connected"
            from: 1000; to: 5000; stepSize: 50
            value: Engine.txFilterHigh
            onMoved: Engine.txFilterHigh = Math.round(value)
        }
    }
}
