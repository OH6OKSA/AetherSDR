import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The radio's memory channels (as AetherSDR's Memory panel): tap one to tune
// to it, save the current slice under a name, press and hold to delete.
ColumnLayout {
    id: mem
    spacing: Theme.spacing / 2
    implicitWidth: 200

    RowLayout {
        Layout.fillWidth: true
        TextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: qsTr("Name for the current frequency")
            font.pixelSize: 15
            onAccepted: saveButton.clicked()
        }
        AppButton {
            id: saveButton
            text: qsTr("Save")
            enabled: Engine.connectionState === "connected"
            focusPolicy: Qt.NoFocus
            onClicked: {
                Engine.saveMemory(nameField.text)
                nameField.text = ""
            }
        }
    }
    Label {
        visible: Engine.memories.length === 0
        text: qsTr("No memories on the radio yet.")
        color: Theme.textDim
        font.pixelSize: 14
    }
    Repeater {
        model: Engine.memories
        delegate: AppButton {
            required property var modelData
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            contentItem: RowLayout {
                spacing: Theme.spacing
                Label {
                    text: (modelData.freq).toFixed(4)
                    font.family: "monospace"
                    font.pixelSize: 15
                    color: "#1a1a1a"
                }
                Label { text: modelData.mode; font.pixelSize: 13; color: "#2a5c8a" }
                Label {
                    text: modelData.name
                    font.pixelSize: 14
                    color: "#1a1a1a"
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            onClicked: Engine.recallMemory(modelData.index)
            onPressAndHold: {
                confirmDelete.index = modelData.index
                confirmDelete.label = modelData.name.length > 0 ? modelData.name : modelData.freq.toFixed(4)
                confirmDelete.open()
            }
        }
    }

    Dialog {
        id: confirmDelete
        property int index: -1
        property string label
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Delete memory “%1”?").arg(label)
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: Engine.removeMemory(index)
    }
}
