import QtQuick
import QtQuick.Controls
import AetherMobile
import QtQuick.Layouts

Page {
    background: Rectangle { color: Theme.background }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 2 * Theme.spacing
        spacing: Theme.spacing

        Label {
            text: qsTr("AetherSDR")
            color: Theme.text
            font.pixelSize: 34
            font.bold: true
        }
        Label {
            text: Engine.radios.count > 0
                  ? qsTr("Tap a radio to connect")
                  : qsTr("Searching the local network…")
            color: Theme.textDim
            font.pixelSize: 18
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacing / 2
            model: Engine.radios

            delegate: ItemDelegate {
                required property int index
                required property string name
                required property string nickname
                required property string model
                required property string address
                required property string version
                required property string transport
                required property bool inUse

                width: ListView.view.width
                height: 2 * Theme.touchTarget
                enabled: Engine.connectionState === "idle"
                background: Rectangle {
                    color: parent.down ? Theme.surfaceRaised : Theme.surface
                    radius: Theme.radius
                }
                contentItem: ColumnLayout {
                    spacing: 2
                    Label {
                        text: (nickname.length > 0 ? nickname + " — " : "") + (model.length > 0 ? model : name)
                        color: Theme.text
                        font.pixelSize: 22
                    }
                    Label {
                        text: transport === "sim"
                              ? qsTr("Built-in demo radio (receive only)")
                              : address + "   v" + version + (inUse ? qsTr("   in use") : "")
                        color: Theme.textDim
                        font.pixelSize: 16
                    }
                }
                onClicked: Engine.connectToRadio(index)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing
            TextField {
                id: addressField
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.touchTarget
                placeholderText: qsTr("FlexRadio IP address, e.g. 192.168.1.50")
                inputMethodHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText
                font.pixelSize: 18
                onAccepted: Engine.connectToAddress(text)
            }
            Button {
                text: qsTr("Connect")
                Layout.preferredHeight: Theme.touchTarget
                enabled: Engine.connectionState === "idle" && addressField.text.length > 0
                onClicked: Engine.connectToAddress(addressField.text)
            }
        }

        Label {
            Layout.fillWidth: true
            visible: Engine.connectionState !== "idle" || Engine.lastError.length > 0
            text: Engine.connectionState === "connecting"
                  ? qsTr("Connecting…")
                  : Engine.lastError
            color: Engine.connectionState === "connecting" ? Theme.textDim : Theme.warning
            font.pixelSize: 18
            wrapMode: Text.Wrap
        }
    }
}
