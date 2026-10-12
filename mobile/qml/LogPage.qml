import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Recent app log for testers: copy it and send it to the developer.
Popup {
    id: popup
    modal: true
    anchors.centerIn: parent
    width: parent.width * 0.95
    height: parent.height * 0.85
    background: Rectangle { color: Theme.surface; radius: Theme.radius }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacing

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Log (%1 lines)").arg(LogBuffer.lines.length)
                color: Theme.text
                font.pixelSize: 22
                Layout.fillWidth: true
            }
            AppButton { text: qsTr("Copy"); onClicked: LogBuffer.copyToClipboard() }
            AppButton { text: qsTr("Clear"); onClicked: LogBuffer.clear() }
            AppButton { text: qsTr("Close"); onClicked: popup.close() }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: LogBuffer.lines
            onCountChanged: positionViewAtEnd()
            delegate: Text {
                required property string modelData
                width: ListView.view.width
                text: modelData
                color: modelData.indexOf("PTT") >= 0 || modelData.indexOf("zoom") >= 0
                       ? Theme.warning : Theme.textDim
                font.family: "monospace"
                font.pixelSize: 12
                wrapMode: Text.WrapAnywhere
            }
        }
    }
}
