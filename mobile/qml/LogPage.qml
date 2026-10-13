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
                text: qsTr("Log (%1 lines)").arg(popup.opened ? LogBuffer.lines.length : 0)
                color: Theme.text
                font.pixelSize: 22
                Layout.fillWidth: true
            }
            AppButton { text: qsTr("Share"); onClicked: LogBuffer.share() }
            AppButton {
                id: copyButton
                text: copiedTimer.running ? qsTr("Copied ✓") : qsTr("Copy")
                onClicked: {
                    LogBuffer.copyToClipboard()
                    copiedTimer.restart()
                }
                Timer { id: copiedTimer; interval: 2000 }
            }
            AppButton { text: qsTr("Clear"); onClicked: LogBuffer.clear() }
            AppButton { text: qsTr("Close"); onClicked: popup.close() }
        }

        // Developer link (off by default): streams this log to a developer's
        // PC and lets it take screenshots. It cannot transmit.
        RowLayout {
            Layout.fillWidth: true
            Switch {
                id: devSwitch
                checked: DevLink.enabled
                onToggled: DevLink.enabled = checked
            }
            Label { text: qsTr("Developer link"); color: Theme.text; font.pixelSize: 15 }
            TextField {
                Layout.fillWidth: true
                text: DevLink.host
                placeholderText: qsTr("PC address")
                inputMethodHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText
                font.pixelSize: 15
                onEditingFinished: DevLink.host = text
            }
            Label {
                text: DevLink.status
                color: DevLink.status === "connected" ? Theme.accent : Theme.textDim
                font.pixelSize: 13
                elide: Text.ElideRight
                Layout.maximumWidth: 160
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            // Only while the page is open: every new line replaces the model,
            // and rebuilding wrapped text rows is main-thread work.
            model: popup.opened ? LogBuffer.lines : []
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
