import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Connected stations (multiFLEX, as the desktop's MultiFlexDialog): every
// GUI client on the radio, this app marked. Disconnect ends another
// station's session, which frees its slices and panadapters; it asks first.
Popup {
    id: dialog
    objectName: "stationsDialog"
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 2 * Theme.spacing : 480, 480)
    padding: Theme.spacing
    property int confirmHandle: 0
    property string confirmName: ""
    background: Rectangle {
        color: Theme.surface
        border.color: Theme.surfaceRaised
        radius: Theme.radius
    }
    onClosed: confirmHandle = 0
    contentItem: ColumnLayout {
        spacing: Theme.spacing / 2
        Label {
            text: qsTr("Connected stations")
            color: Theme.text
            font.pixelSize: Math.round(Theme.buttonFont * 1.1)
            font.bold: true
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.textDim
            font.pixelSize: Math.round(Theme.buttonFont * 0.8)
            text: qsTr("Programs connected to the radio. Each holds its own slices and panadapters; "
                       + "disconnecting one frees them for this app.")
        }
        Repeater {
            model: Engine.stations
            delegate: RowLayout {
                id: row
                required property var modelData
                Layout.fillWidth: true
                spacing: Theme.spacing / 2
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: row.modelData.station + (row.modelData.ours ? qsTr("  (this app)") : "")
                        color: row.modelData.ours ? Theme.accent : Theme.text
                        font.pixelSize: Theme.buttonFont
                        font.bold: true
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: row.modelData.program + (row.modelData.transmitting ? qsTr(" · transmitting") : "")
                        color: row.modelData.transmitting ? Theme.transmit : Theme.textDim
                        font.pixelSize: Math.round(Theme.buttonFont * 0.8)
                    }
                }
                AppButton {
                    visible: !row.modelData.ours
                    text: qsTr("Disconnect")
                    leftPadding: 12
                    rightPadding: 12
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: {
                        dialog.confirmHandle = row.modelData.handle
                        dialog.confirmName = row.modelData.station
                    }
                }
            }
        }
        // Confirmation in place: the other station loses its session.
        Rectangle {
            visible: dialog.confirmHandle !== 0
            Layout.fillWidth: true
            implicitHeight: confirmColumn.implicitHeight + 2 * Theme.spacing
            radius: Theme.radius
            color: Theme.surfaceRaised
            border.color: Theme.warning
            ColumnLayout {
                id: confirmColumn
                anchors.fill: parent
                anchors.margins: Theme.spacing
                spacing: Theme.spacing / 2
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.text
                    text: qsTr("Disconnect %1? Its operator loses the radio until they connect again.")
                          .arg(dialog.confirmName)
                }
                RowLayout {
                    AppButton {
                        text: qsTr("Disconnect")
                        Layout.fillWidth: true
                        onClicked: {
                            Engine.disconnectStation(dialog.confirmHandle)
                            dialog.confirmHandle = 0
                        }
                    }
                    AppButton {
                        text: qsTr("Cancel")
                        Layout.fillWidth: true
                        onClicked: dialog.confirmHandle = 0
                    }
                }
            }
        }
        AppButton {
            text: qsTr("Close")
            Layout.alignment: Qt.AlignRight
            leftPadding: 16
            rightPadding: 16
            onClicked: dialog.close()
        }
    }
}
