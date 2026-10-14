import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// CW: straight key (hold), CWX text via the radio's keyer, speed, pitch,
// break-in and the receive decoder. Shared by the tablet panel and drawer.
ColumnLayout {
    id: cw
    spacing: Theme.spacing / 2
    implicitWidth: 200

    // The callsign {CALL} stands for: the radio's own (SmartSDR Radio Setup),
    // set here when it is missing or wrong.
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.connectionState === "connected"
        Label {
            text: qsTr("Callsign")
            color: Theme.textDim
            font.pixelSize: 15
        }
        Label {
            Layout.fillWidth: true
            text: Engine.callsign.length > 0 ? Engine.callsign : qsTr("not set")
            color: Engine.callsign.length > 0 ? Theme.text : Theme.warning
            font.pixelSize: 17
            font.bold: true
        }
        AppButton {
            text: Engine.callsign.length > 0 ? qsTr("Change") : qsTr("Set")
            leftPadding: 12
            rightPadding: 12
            focusPolicy: Qt.NoFocus
            onClicked: {
                callEditor.text = Engine.callsign
                callEditor.open()
            }
        }
    }

    // Memories: tap sends (CWX), press and hold edits. {CALL} = the radio's
    // callsign. The straight key is the KEY button in PTT's place.
    GridLayout {
        Layout.fillWidth: true
        visible: Engine.cwxAvailable
        columns: 3
        columnSpacing: Theme.spacing / 2
        rowSpacing: Theme.spacing / 2
        Repeater {
            model: Engine.cwMemories
            delegate: AppButton {
                required property int index
                required property string modelData
                text: "M" + (index + 1) + "  " + modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.preferredHeight: Theme.touchTarget
                focusPolicy: Qt.NoFocus
                onClicked: Engine.sendCwMemory(index)
                onPressAndHold: {
                    memoryEditor.index = index
                    memoryEditor.text = modelData
                    memoryEditor.open()
                }
            }
        }
    }

    // CWX: text sent by the radio's keyer at the set speed.
    RowLayout {
        Layout.fillWidth: true
        visible: Engine.cwxAvailable
        TextField {
            id: cwxText
            Layout.fillWidth: true
            placeholderText: qsTr("CW text to send")
            inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
            font.pixelSize: 16
            onAccepted: send.clicked()
        }
        AppButton {
            id: send
            text: qsTr("Send")
            enabled: cwxText.text.length > 0
            focusPolicy: Qt.NoFocus
            onClicked: {
                Engine.sendCwx(cwxText.text)
                cwxText.text = ""
            }
        }
        AppButton { text: qsTr("Stop"); focusPolicy: Qt.NoFocus; onClicked: Engine.stopCwx() }
    }

    Label { text: qsTr("Speed  %1 WPM").arg(Engine.cwSpeed); color: Theme.textDim; font.pixelSize: 15 }
    Slider {
        Layout.fillWidth: true
        from: 5
        to: 60
        stepSize: 1
        value: Engine.cwSpeed
        onMoved: Engine.cwSpeed = Math.round(value)
    }
    Label { text: qsTr("Pitch  %1 Hz").arg(Engine.cwPitch); color: Theme.textDim; font.pixelSize: 15 }
    Slider {
        Layout.fillWidth: true
        from: 300
        to: 1000
        stepSize: 10
        value: Engine.cwPitch
        onMoved: Engine.cwPitch = Math.round(value)
    }
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            text: qsTr("Break-in")
            checked: Engine.cwBreakIn
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.cwBreakIn = !Engine.cwBreakIn
        }
        AppButton {
            text: qsTr("Decoder")
            checked: Engine.cwDecoderEnabled
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.cwDecoderEnabled = !Engine.cwDecoderEnabled
        }
    }

    // Decoded text (CW modes only).
    Rectangle {
        Layout.fillWidth: true
        visible: Engine.cwDecoderEnabled
        implicitHeight: 130
        radius: Theme.radius
        color: Theme.background
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 6
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: Engine.cwMode ? (Engine.cwDecoderStats.length > 0 ? Engine.cwDecoderStats : qsTr("Listening…"))
                                        : qsTr("Decoder runs in CW mode")
                    color: Theme.textDim
                    font.pixelSize: 13
                    Layout.fillWidth: true
                }
                AppButton {
                    text: qsTr("Clear")
                    focusPolicy: Qt.NoFocus
                    onClicked: Engine.clearCwDecoded()
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: Engine.cwDecodedText
                color: Theme.text
                font.family: "monospace"
                font.pixelSize: 14
                wrapMode: Text.WrapAnywhere
                elide: Text.ElideLeft
                verticalAlignment: Text.AlignBottom
                clip: true
            }
        }
    }

    // Sets the radio's callsign.
    Dialog {
        id: callEditor
        objectName: "callsignEditor"
        property alias text: callField.text
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(420, parent ? parent.width - 24 : 400)
        modal: true
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.surfaceRaised }
        onOpened: callField.forceActiveFocus()
        contentItem: ColumnLayout {
            spacing: Theme.spacing
            Label {
                Layout.fillWidth: true
                text: qsTr("Your callsign, stored in the radio (as SmartSDR's Radio Setup). {CALL} in the CW memories sends it.")
                color: Theme.textDim
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }
            TextField {
                id: callField
                Layout.fillWidth: true
                font.pixelSize: 20
                placeholderText: qsTr("e.g. OH6OKSA")
                inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                onAccepted: callSave.clicked()
            }
            RowLayout {
                Layout.fillWidth: true
                AppButton { text: qsTr("Cancel"); Layout.fillWidth: true; onClicked: callEditor.close() }
                AppButton {
                    id: callSave
                    text: qsTr("Save")
                    Layout.fillWidth: true
                    enabled: callField.text.trim().length > 0
                    onClicked: {
                        Engine.setCallsign(callField.text)
                        callEditor.close()
                    }
                }
            }
        }
    }

    // Edits one memory.
    Dialog {
        id: memoryEditor
        property int index: 0
        property alias text: memoryField.text
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(420, parent ? parent.width - 24 : 400)
        modal: true
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.surfaceRaised }
        onOpened: memoryField.forceActiveFocus()
        contentItem: ColumnLayout {
            spacing: Theme.spacing
            Label {
                text: qsTr("Memory M%1   ({CALL} = your callsign)").arg(memoryEditor.index + 1)
                color: Theme.textDim
                font.pixelSize: 14
            }
            TextField {
                id: memoryField
                Layout.fillWidth: true
                font.pixelSize: 17
                inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
                onAccepted: save.clicked()
            }
            RowLayout {
                Layout.fillWidth: true
                AppButton { text: qsTr("Cancel"); Layout.fillWidth: true; onClicked: memoryEditor.close() }
                AppButton {
                    id: save
                    text: qsTr("Save")
                    Layout.fillWidth: true
                    onClicked: {
                        Engine.setCwMemory(memoryEditor.index, memoryField.text)
                        memoryEditor.close()
                    }
                }
            }
        }
    }
}
