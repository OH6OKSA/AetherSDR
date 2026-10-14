import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The slice row: the two selectable meters at its ends and the slice chips
// between them, two side by side (four slices make 2 x 2, also on an
// iPhone). A chip ("A 14.100 USB" with a red TX tag on the transmit slice)
// selects on tap; press and hold opens make-TX / close. "+ Slice" and
// "+ Pan" are at the bottom right of the waterfall.
RowLayout {
    id: bar
    spacing: Theme.spacing / 2
    property bool compact: false
    // The page gives the row its width (fillWidth); its contents must not
    // ask for more, as the meters size themselves from that width.
    Layout.preferredWidth: 0
    Layout.minimumWidth: 0

    // Chip height: two rows fit beside a meter where there is height.
    readonly property real chipHeight: Theme.touchTarget
    // Meter dial height: two chip rows on a phone, more on a tablet; low
    // (iPhone landscape) keeps it small.
    readonly property real dialHeight: Theme.compactHeight ? Theme.touchTarget * 1.3
                                     : Theme.phoneLayout ? 2 * chipHeight + 4 - 8
                                     : Theme.touchTarget * 2.3
    // The chips' column keeps room for two chips side by side.
    readonly property real chipMinWidth: Theme.phoneLayout ? 96 : 150
    readonly property real chipMaxWidth: Theme.phoneLayout ? 140 : 210
    // Room for each meter: what the two chip columns leave. From the column
    // the row sits in: the row's own width is the layout's output.
    readonly property real meterMaxWidth: Math.max(80, Math.min(240,
        ((bar.parent ? bar.parent.width : 0) - 2 * chipMinWidth - 4 - 4 * bar.spacing) / 2))

    MeterBadge {
        side: "left"
        Layout.alignment: Qt.AlignTop
        dialHeight: bar.dialHeight
        maxWidth: bar.meterMaxWidth
    }

    Item {
        id: chipArea
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        implicitHeight: chips.implicitHeight
        GridLayout {
            id: chips
            anchors.horizontalCenter: parent.horizontalCenter
            // Two columns of equal chips, centred between the meters.
            readonly property real cell: Math.max(0, Math.min(bar.chipMaxWidth,
                                                              (chipArea.width - columnSpacing) / 2))
            width: Engine.slices.length > 1 ? 2 * cell + columnSpacing : cell
            columns: 2
            columnSpacing: 4
            rowSpacing: 4
            Repeater {
                model: Engine.slices
                delegate: AppButton {
                    id: chip
                    required property var modelData
                    Layout.preferredWidth: chips.cell
                    Layout.preferredHeight: bar.chipHeight
                    checked: modelData.active
                    leftPadding: 4
                    rightPadding: 4
                    font.pixelSize: bar.compact ? Math.round(Theme.buttonFont * 0.9) : Theme.buttonFont
                    focusPolicy: Qt.NoFocus
                    onClicked: Engine.selectSlice(modelData.id)
                    // Under the pressed chip, not at the last pointer position.
                    onPressAndHold: sliceMenu.popup(chip, 0, chip.height)
                    contentItem: Item {
                        Row {
                            anchors.centerIn: parent
                            spacing: 5
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                // Compact (phone): the mode is beside the frequency already.
                                text: chip.modelData.letter + " " + chip.modelData.frequency.toFixed(3)
                                      + (bar.compact ? "" : " " + chip.modelData.mode)
                                font: chip.font
                                color: chip.checked ? "#ffffff" : "#1a1a1a"
                            }
                            // The transmit slice: a red tag, not just two letters.
                            Rectangle {
                                visible: chip.modelData.tx
                                anchors.verticalCenter: parent.verticalCenter
                                width: txText.implicitWidth + 8
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
    }

    MeterBadge {
        side: "right"
        Layout.alignment: Qt.AlignTop
        dialHeight: bar.dialHeight
        maxWidth: bar.meterMaxWidth
    }
}
