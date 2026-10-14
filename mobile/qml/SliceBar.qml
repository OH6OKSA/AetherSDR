import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The slice row: slice chips (one under another), the two selectable meters
// and "+ Slice"/"+ Pan". A chip ("A 14.100 USB" with a red TX tag on the
// transmit slice) selects on tap; press and hold opens make-TX / close.
RowLayout {
    id: bar
    spacing: Theme.spacing / 2
    property bool compact: false
    // The page gives the row its width (fillWidth); its contents must not
    // ask for more, as the meters size themselves from that width.
    Layout.preferredWidth: 0
    Layout.minimumWidth: 0

    // Meter dial height: a touch target and a half on a phone, more on a
    // tablet; low (iPhone landscape) keeps it small.
    readonly property real dialHeight: Theme.compactHeight ? Theme.touchTarget * 1.3
                                     : Theme.phoneLayout ? Theme.touchTarget * 1.5
                                     : Theme.touchTarget * 2.3
    // Room left for each meter between the chips and the buttons. From the
    // column the row sits in: the row's own width is the layout's output.
    readonly property real meterMaxWidth: Math.max(80, Math.min(240,
        ((bar.parent ? bar.parent.width : 0) - chips.implicitWidth - buttons.implicitWidth - 4 * bar.spacing) / 2))

    Column {
        id: chips
        Layout.alignment: Qt.AlignTop
        spacing: 4
        Repeater {
            model: Engine.slices
            delegate: AppButton {
                id: chip
                required property var modelData
                height: Theme.touchTarget
                checked: modelData.active
                leftPadding: 8
                rightPadding: 8
                focusPolicy: Qt.NoFocus
                onClicked: Engine.selectSlice(modelData.id)
                // Under the pressed chip, not at the last pointer position.
                onPressAndHold: sliceMenu.popup(chip, 0, chip.height)
                contentItem: Row {
                    spacing: 6
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        // Compact (phone): the mode is beside the frequency already.
                        text: chip.modelData.letter + "  " + chip.modelData.frequency.toFixed(3)
                              + (bar.compact ? "" : " " + chip.modelData.mode)
                        font: chip.font
                        color: chip.checked ? "#ffffff" : "#1a1a1a"
                    }
                    // The transmit slice: a red tag, not just two letters.
                    Rectangle {
                        visible: chip.modelData.tx
                        anchors.verticalCenter: parent.verticalCenter
                        width: txText.implicitWidth + 10
                        height: txText.implicitHeight + 4
                        radius: 4
                        color: Theme.transmit
                        Text {
                            id: txText
                            anchors.centerIn: parent
                            text: "TX"
                            font.pixelSize: chip.font.pixelSize
                            font.bold: true
                            color: "#ffffff"
                        }
                    }
                }
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
    }

    MeterBadge {
        side: "left"
        Layout.alignment: Qt.AlignTop
        dialHeight: bar.dialHeight
        maxWidth: bar.meterMaxWidth
    }
    Item { Layout.fillWidth: true }
    MeterBadge {
        side: "right"
        Layout.alignment: Qt.AlignTop
        dialHeight: bar.dialHeight
        maxWidth: bar.meterMaxWidth
    }

    Row {
        id: buttons
        Layout.alignment: Qt.AlignTop
        spacing: 4
        AppButton {
            text: qsTr("+ Slice")
            visible: Engine.canAddSlice
            leftPadding: 6
            rightPadding: 6
            height: Theme.touchTarget
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
            height: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            onClicked: Engine.closeOtherPanadapters()
        }
        AppButton {
            text: qsTr("+ Pan")
            visible: Engine.canAddPanadapter
            leftPadding: 6
            rightPadding: 6
            height: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            onClicked: Engine.addPanadapter()
        }
    }
}
