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
        id: chips
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
                        // Compact (phone): the mode is beside the frequency already.
            text: modelData.letter + "  " + modelData.frequency.toFixed(3)
                  + (bar.compact ? "" : " " + modelData.mode) + (modelData.tx ? "  TX" : "")
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
    // The app shows one panadapter; any others still stream to the device
    // (bandwidth, packet loss) and hold the radio's panadapter slots.
    AppButton {
        text: qsTr("Close %1 other pans").arg(Engine.panCount - 1)
        visible: Engine.panCount > 1
        leftPadding: 6
        rightPadding: 6
        Layout.preferredHeight: Theme.touchTarget
        focusPolicy: Qt.NoFocus
        onClicked: Engine.closeOtherPanadapters()
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
