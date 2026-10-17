import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The radio's memory channels (as AetherSDR's Memory panel): tap one to tune
// to it, save the current slice under a name, press and hold to edit its
// name, frequency and mode or to delete it.
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
        Layout.fillWidth: true
        text: Engine.memories.length === 0 ? qsTr("No memories on the radio yet.")
            : Engine.memoriesWritable ? qsTr("Tap to tune; press and hold to edit or delete.")
            : qsTr("Tap to tune.")
        color: Theme.textDim
        font.pixelSize: 14
        wrapMode: Text.WordWrap
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
                if (Engine.memoriesWritable) {
                    editDialog.edit(modelData)
                }
            }
        }
    }

    Dialog {
        id: editDialog
        objectName: "memoryEditDialog"
        property int index: -1
        property string mode: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent.width - 24)
        height: Math.min(implicitHeight, parent.height - 16)
        modal: true
        padding: Theme.spacing
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.surfaceRaised }

        function edit(m) {
            index = m.index
            nameEdit.text = m.name
            freqEdit.text = m.freq.toFixed(6).replace(/0{1,3}$/, "")
            mode = m.mode
            open()
        }
        // Screenshots (--popup memoryEditDialog): the first memory, or a
        // sample that cannot be saved (index -1) when there is none.
        function preview() {
            edit(Engine.memories.length > 0 ? Engine.memories[0]
                 : { index: -1, name: "Net 80 m", freq: 3.699, mode: "LSB" })
        }
        // MHz typed with a dot or a comma; NaN when it is not a frequency.
        readonly property real freqValue: {
            const v = Number(freqEdit.text.trim().replace(",", "."))
            return freqEdit.text.trim().length > 0 && v > 0 && v < 1000 ? v : NaN
        }
        readonly property var modeChoices: {
            const list = Engine.modes.slice()
            if (mode.length > 0 && list.indexOf(mode) < 0) list.push(mode)
            return list
        }
        function save() {
            if (isNaN(freqValue)) return
            Engine.editMemory(index, nameEdit.text, freqValue, mode)
            close()
        }

        contentItem: Flickable {
            implicitHeight: editColumn.implicitHeight
            contentHeight: editColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ColumnLayout {
                id: editColumn
                width: parent.width
                spacing: Theme.spacing / 2

                Label {
                    text: qsTr("Edit memory")
                    color: Theme.text
                    font.pixelSize: Theme.labelFont
                    font.bold: true
                }
                Label { text: qsTr("Name"); color: Theme.textDim; font.pixelSize: Theme.smallFont }
                TextField {
                    id: nameEdit
                    Layout.fillWidth: true
                    font.pixelSize: 15
                    onAccepted: freqEdit.forceActiveFocus()
                }
                Label { text: qsTr("Frequency (MHz)"); color: Theme.textDim; font.pixelSize: Theme.smallFont }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: freqEdit
                        Layout.fillWidth: true
                        font.pixelSize: 15
                        font.family: "monospace"
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        color: isNaN(editDialog.freqValue) ? "#b3261e" : "#1a1a1a"
                        onAccepted: editDialog.save()
                    }
                    AppButton {
                        text: qsTr("Current")
                        focusPolicy: Qt.NoFocus
                        Layout.preferredWidth: Math.max(implicitWidth + 16, 76)
                        onClicked: {
                            freqEdit.text = Engine.frequencyMhz.toFixed(6).replace(/0{1,3}$/, "")
                            editDialog.mode = Engine.mode
                        }
                    }
                }
                Label { text: qsTr("Mode"); color: Theme.textDim; font.pixelSize: Theme.smallFont }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: 4
                    rowSpacing: 4
                    Repeater {
                        model: editDialog.modeChoices
                        delegate: AppButton {
                            required property string modelData
                            text: modelData
                            checked: editDialog.mode === modelData
                            focusPolicy: Qt.NoFocus
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.touchTarget
                            onClicked: editDialog.mode = modelData
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacing / 2
                    spacing: Theme.spacing / 2
                    AppButton {
                        text: qsTr("Delete")
                        focusPolicy: Qt.NoFocus
                        Layout.preferredWidth: Math.max(implicitWidth + 16, 76)
                        onClicked: {
                            confirmDelete.index = editDialog.index
                            confirmDelete.label = nameEdit.text.length > 0 ? nameEdit.text : freqEdit.text
                            editDialog.close()
                            confirmDelete.open()
                        }
                    }
                    Item { Layout.fillWidth: true }
                    AppButton {
                        text: qsTr("Cancel")
                        focusPolicy: Qt.NoFocus
                        Layout.preferredWidth: Math.max(implicitWidth + 16, 76)
                        onClicked: editDialog.close()
                    }
                    AppButton {
                        text: qsTr("Save")
                        focusPolicy: Qt.NoFocus
                        Layout.preferredWidth: Math.max(implicitWidth + 16, 76)
                        enabled: !isNaN(editDialog.freqValue)
                        onClicked: editDialog.save()
                    }
                }
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
