import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The radio's profiles, as in SmartSDR/AetherSDR: choose one to load, save
// the current settings under a name, delete the selected one.
ColumnLayout {
    id: profiles
    spacing: Theme.spacing
    implicitWidth: 200

    component ProfileSection: ColumnLayout {
        id: section
        property string type
        property string title
        property var names: []
        property string active
        property string hint
        Layout.fillWidth: true
        spacing: Theme.spacing / 2

        Label { text: section.title; color: Theme.textDim; font.pixelSize: 16 }
        ComboBox {
            id: picker
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.touchTarget
            model: section.names
            currentIndex: section.names.indexOf(section.active)
            displayText: currentIndex >= 0 ? currentText : qsTr("(none)")
            font.pixelSize: Theme.buttonFont
            onActivated: (index) => Engine.loadProfile(section.type, section.names[index])
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: qsTr("Save as…")
                font.pixelSize: 15
                onAccepted: saveButton.clicked()
            }
            AppButton {
                id: saveButton
                text: qsTr("Save")
                enabled: nameField.text.trim().length > 0
                focusPolicy: Qt.NoFocus
                onClicked: {
                    Engine.saveProfile(section.type, nameField.text.trim())
                    nameField.text = ""
                }
            }
            AppButton {
                text: qsTr("Delete")
                enabled: picker.currentIndex >= 0
                focusPolicy: Qt.NoFocus
                onClicked: {
                    confirmDelete.type = section.type
                    confirmDelete.name = picker.currentText
                    confirmDelete.open()
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: section.hint.length > 0
            text: section.hint
            color: Theme.textDim
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
    }

    ProfileSection {
        type: "global"
        title: qsTr("Global profile")
        names: Engine.globalProfiles
        active: Engine.activeGlobalProfile
        hint: qsTr("Loading a global profile changes bands, panadapters and slices. Save overwrites a profile of the same name.")
    }
    ProfileSection {
        type: "transmit"
        title: qsTr("TX profile")
        names: Engine.txProfiles
        active: Engine.activeTxProfile
        hint: qsTr("Save creates a new profile; to replace one, delete it first.")
    }
    ProfileSection {
        type: "mic"
        title: qsTr("Mic profile")
        names: Engine.micProfiles
        active: Engine.activeMicProfile
    }

    Dialog {
        id: confirmDelete
        property string type
        property string name
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Delete profile “%1”?").arg(name)
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: Engine.deleteProfile(type, name)
    }
}
