import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// One chip per slice ("A 14.100 USB", TX marked). Tap selects; press and
// hold opens make-TX / close. "+ Slice" and "+ Pan" add more.
RowLayout {
    id: bar
    spacing: 4
    property bool compact: false

    ListView {
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.touchTarget
        orientation: ListView.Horizontal
        spacing: 4
        clip: true
        model: Engine.slices
        delegate: AppButton {
            id: chip
            required property var modelData
            height: Theme.touchTarget
            checked: modelData.active
            leftPadding: 8
            rightPadding: 8
                        text: modelData.letter + "  " + modelData.frequency.toFixed(3) + " " + modelData.mode
                  + (modelData.tx ? "  TX" : "")
            focusPolicy: Qt.NoFocus
            onClicked: Engine.selectSlice(modelData.id)
            // Under the pressed chip, not at the last pointer position.
            onPressAndHold: sliceMenu.popup(chip, 0, chip.height)
            Menu {
                id: sliceMenu
                MenuItem {
                    text: qsTr("Make TX slice")
                    enabled: !chip.modelData.tx
                    onTriggered: Engine.makeTxSlice(chip.modelData.id)
                }
                MenuItem {
                    text: qsTr("Close slice")
                    enabled: Engine.slices.length > 1
                    onTriggered: Engine.removeSlice(chip.modelData.id)
                }
            }
        }
    }
    AppButton {
        text: qsTr("+ Slice")
        visible: Engine.canAddSlice
                leftPadding: 6
        rightPadding: 6
        Layout.preferredHeight: Theme.touchTarget
        focusPolicy: Qt.NoFocus
        onClicked: Engine.addSlice()
    }
    AppButton {
        text: qsTr("+ Pan")
        visible: Engine.canAddPanadapter
                leftPadding: 6
        rightPadding: 6
        Layout.preferredHeight: Theme.touchTarget
        focusPolicy: Qt.NoFocus
        onClicked: Engine.addPanadapter()
    }
}
