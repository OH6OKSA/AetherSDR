import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// A radio with no callsign looks new: offer Radio Setup once (per radio,
// remembered), a few seconds after connecting. Never on a configured radio.
Dialog {
    id: offer
    objectName: "setupSuggestion"
    signal setupRequested()
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(440, parent ? parent.width - 24 : 400)
    modal: true
    padding: Theme.spacing
    background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.accent; border.width: 2 }

    Timer {
        id: check
        interval: 5000
        running: Engine.connectionState === "connected"
        onTriggered: if (Engine.setupSuggested()) offer.open()
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing
        Label {
            text: qsTr("This radio looks new")
            color: Theme.text
            font.pixelSize: 20
            font.bold: true
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("It has no callsign yet. Set up its basics now — callsign, name, transmit limits, CW keyer and audio outputs? You can change them later from Setup.")
            color: Theme.textDim
            font.pixelSize: 15
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            AppButton {
                text: qsTr("Later")
                Layout.fillWidth: true
                onClicked: { Engine.dismissSetupSuggestion(); offer.close() }
            }
            AppButton {
                text: qsTr("Set up")
                Layout.fillWidth: true
                onClicked: {
                    Engine.dismissSetupSuggestion()
                    offer.close()
                    offer.setupRequested()
                }
            }
        }
    }
}
