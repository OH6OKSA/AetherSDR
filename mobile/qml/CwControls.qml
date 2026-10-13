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

    // Straight key: down while held. Release, a cancelled touch or losing
    // the foreground always keys up.
    AppButton {
        id: key
        Layout.fillWidth: true
        Layout.preferredHeight: 56
        text: Engine.cwKeyDown ? qsTr("KEY ●") : qsTr("KEY")
        enabled: Engine.cwKeyAvailable || pressed
        font.pixelSize: 20
        font.bold: true
        focusPolicy: Qt.NoFocus
        background: Rectangle {
            radius: Theme.radius
            color: Engine.cwKeyDown ? Theme.transmit : key.enabled ? Theme.pttReady : Theme.surface
            border.width: 2
            border.color: Theme.transmit
        }
        contentItem: Text {
            text: key.text
            font: key.font
            color: key.enabled ? "white" : Theme.textDim
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        onPressed: Engine.setCwKey(true)
        onReleased: Engine.setCwKey(false, "key released")
        onCanceled: Engine.setCwKey(false, "touch cancelled by the system")
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
            checkable: true
            checked: Engine.cwBreakIn
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.cwBreakIn = !Engine.cwBreakIn
        }
        AppButton {
            text: qsTr("Decoder")
            checkable: true
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
        implicitHeight: 110
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
                    font.pixelSize: 12
                    implicitHeight: 28
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
}
