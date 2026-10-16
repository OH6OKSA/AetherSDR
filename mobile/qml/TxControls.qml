import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Transmit settings shared by the tablet side panel and the controls drawer:
// tune carrier, MOX, ATU, speech processor, VOX, DEXP, AM carrier, monitor,
// power and microphone.
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
        // MOX: transmit latched until tapped again (the desktop's MOX).
        AppButton {
            id: moxButton
            readonly property bool on: Engine.transmitting && !Engine.tuning
            text: on ? qsTr("MOX ●") : qsTr("MOX")
            enabled: Engine.transmitAvailable || on
            Layout.fillWidth: true
            background: Rectangle {
                implicitHeight: Theme.touchTarget
                radius: Theme.radius
                color: moxButton.on ? Theme.transmit : moxButton.down ? "#bdbdbd" : "#e0e0e0"
                opacity: moxButton.enabled ? 1.0 : 0.5
            }
            onClicked: Engine.toggleMox()
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

    // ---- Phone: VOX, downward expander, AM carrier (the desktop's Phone
    // applet; levels sent when a slider is released) ----
    component PhoneLevel: RowLayout {
        id: pl
        property string key: ""
        property string toggleKey: ""
        property string title: ""
        Layout.fillWidth: true
        AppButton {
            visible: pl.toggleKey.length > 0
            text: pl.title
            checked: !!Engine.phone[pl.toggleKey]
            Layout.preferredWidth: 64
            onClicked: Engine.setPhone(pl.toggleKey, checked ? 0 : 1)
        }
        Label {
            visible: pl.toggleKey.length === 0
            text: pl.title
            color: Theme.textDim
            font.pixelSize: 15
            Layout.preferredWidth: 64
        }
        Slider {
            id: plSlider
            Layout.fillWidth: true
            from: 0; to: 100; stepSize: 1
            onPressedChanged: if (!pressed) Engine.setPhone(pl.key, Math.round(value))
            Binding on value {
                when: !plSlider.pressed
                value: Engine.phone[pl.key] || 0
            }
        }
        Label {
            text: Math.round(plSlider.value)
            color: Theme.textDim
            font.pixelSize: 13
            Layout.preferredWidth: 28
            horizontalAlignment: Text.AlignRight
        }
    }
    PhoneLevel { visible: !Engine.cwMode; key: "voxLevel"; toggleKey: "voxOn"; title: qsTr("VOX") }
    PhoneLevel { visible: !Engine.cwMode; key: "voxDelay"; title: qsTr("Delay") }
    PhoneLevel { visible: !Engine.cwMode; key: "dexpLevel"; toggleKey: "dexpOn"; title: qsTr("DEXP") }
    PhoneLevel {
        visible: ["AM", "SAM", "DSB"].indexOf(Engine.mode) >= 0
        key: "amCarrier"
        title: qsTr("Carrier")
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

    // ---- Voice keyer: the radio's DVK where it is licensed (12 slots of up
    // to 10 s kept in the radio), otherwise the app's own voice macros (12
    // slots of up to 15 s on this device, played into the TX audio). Tap
    // plays (transmits), tap again stops; press and hold for record, listen
    // (radio only), rename and clear. F1-F12 on a keyboard play them in voice
    // modes, Esc stops. ----
    Label {
        visible: !Engine.cwMode && Engine.dvkState !== "unavailable"
        text: Engine.dvkState === "idle" ? qsTr("Voice keyer")
              : Engine.dvkState === "recording" ? qsTr("Voice keyer: recording F%1 (tap it to stop)").arg(Engine.dvkActiveId)
              : Engine.dvkState === "preview" ? qsTr("Voice keyer: listening to F%1").arg(Engine.dvkActiveId)
              : qsTr("Voice keyer: sending F%1").arg(Engine.dvkActiveId)
        color: Engine.dvkState === "idle" ? Theme.textDim : Theme.warning
        font.pixelSize: 15
    }
    GridLayout {
        visible: !Engine.cwMode && Engine.dvkState !== "unavailable"
        Layout.fillWidth: true
        columns: width > 520 ? 4 : 3
        columnSpacing: Theme.spacing / 2
        rowSpacing: Theme.spacing / 2
        Repeater {
            model: Engine.dvkSlots
            delegate: AppButton {
                id: slotButton
                required property var modelData
                readonly property bool active: Engine.dvkActiveId === modelData.id && Engine.dvkState !== "idle"
                text: "F" + modelData.id + "  " + modelData.name
                      + (modelData.seconds > 0 ? "  " + modelData.seconds.toFixed(1) + " s" : "")
                checked: active
                opacity: modelData.seconds > 0 || active ? 1.0 : 0.6
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.preferredHeight: Theme.touchTarget
                onClicked: {
                    if (Engine.dvkState === "recording") Engine.dvkRecord(modelData.id)
                    else if (Engine.dvkState === "preview") Engine.dvkPreview(modelData.id)
                    else if (modelData.seconds > 0 || Engine.dvkState === "playback") Engine.dvkPlay(modelData.id)
                    else dvkMenu.openFor(modelData)
                }
                onPressAndHold: dvkMenu.openFor(modelData)
            }
        }
    }
    Popup {
        id: dvkMenu
        objectName: "dvkMenu"
        property int slotId: 1
        property string slotName: ""
        function openFor(slot) {
            slotId = slot.id
            slotName = slot.name
            nameField.text = slot.name
            open()
        }
        anchors.centerIn: Overlay.overlay
        modal: true
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; border.color: Theme.surfaceRaised; radius: Theme.radius }
        contentItem: ColumnLayout {
            spacing: Theme.spacing / 2
            Label {
                text: qsTr("F%1  %2").arg(dvkMenu.slotId).arg(dvkMenu.slotName)
                color: Theme.text
                font.pixelSize: Theme.buttonFont
                font.bold: true
            }
            Label {
                Layout.maximumWidth: 320
                wrapMode: Text.Wrap
                color: Theme.textDim
                font.pixelSize: 13
                text: Engine.dvkOnRadio
                      ? qsTr("Record speaks into this device's microphone; the radio keeps up to 10 s. "
                             + "Recording and listening do not transmit.")
                      : qsTr("Record speaks into this device's microphone; this device keeps up to 15 s "
                             + "(the radio's own voice keyer needs SmartSDR+). Recording does not transmit.")
            }
            RowLayout {
                AppButton {
                    text: Engine.dvkState === "recording" ? qsTr("Stop recording") : qsTr("Record")
                    Layout.fillWidth: true
                    onClicked: Engine.dvkRecord(dvkMenu.slotId)
                }
                AppButton {
                    visible: Engine.dvkOnRadio
                    text: Engine.dvkState === "preview" ? qsTr("Stop") : qsTr("Listen")
                    Layout.fillWidth: true
                    onClicked: Engine.dvkPreview(dvkMenu.slotId)
                }
            }
            RowLayout {
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Name")
                }
                AppButton {
                    text: qsTr("Rename")
                    onClicked: Engine.dvkRename(dvkMenu.slotId, nameField.text)
                }
            }
            RowLayout {
                AppButton {
                    text: qsTr("Clear")
                    Layout.fillWidth: true
                    onClicked: Engine.dvkClear(dvkMenu.slotId)
                }
                AppButton {
                    text: qsTr("Close")
                    Layout.fillWidth: true
                    onClicked: dvkMenu.close()
                }
            }
        }
    }
}
