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
        Layout.preferredHeight: bar.compact ? 36 : 40
        orientation: ListView.Horizontal
        spacing: 4
        clip: true
        model: Engine.slices
        delegate: AppButton {
            id: chip
            required property var modelData
            height: bar.compact ? 36 : 40
            checked: modelData.active
            leftPadding: 8
            rightPadding: 8
            font.pixelSize: bar.compact ? 12 : 14
            text: modelData.letter + "  " + modelData.frequency.toFixed(3) + " " + modelData.mode
                  + (modelData.tx ? "  TX" : "")
            focusPolicy: Qt.NoFocus
            onClicked: Engine.selectSlice(modelData.id)
            onPressAndHold: sliceMenu.popup()
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
        font.pixelSize: bar.compact ? 12 : 14
        leftPadding: 6
        rightPadding: 6
        Layout.preferredHeight: bar.compact ? 36 : 40
        focusPolicy: Qt.NoFocus
        onClicked: Engine.addSlice()
    }
    AppButton {
        text: qsTr("+ Pan")
        visible: Engine.canAddPanadapter
        font.pixelSize: bar.compact ? 12 : 14
        leftPadding: 6
        rightPadding: 6
        Layout.preferredHeight: bar.compact ? 36 : 40
        focusPolicy: Qt.NoFocus
        onClicked: Engine.addPanadapter()
    }
}
