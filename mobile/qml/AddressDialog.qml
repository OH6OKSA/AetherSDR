import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The radio's IP address on the app's own keypad (as frequency entry), so it
// can always be entered: on-screen or hardware keyboard alike. Typed digits
// and dots work too.
Dialog {
    id: dialog
    objectName: "addressDialog"
    modal: true
    anchors.centerIn: parent
    width: Math.min(420, parent.width - 24)
    contentHeight: column.implicitHeight
    height: Math.min(contentHeight + 2 * padding, parent.height - 16)
    padding: Theme.spacing
    background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.surfaceRaised }
    readonly property int keyHeight: Math.max(36, Math.min(60, (parent.height - 16 - 2 * padding - 90 - 5 * Theme.spacing) / 5))

    property string entry: ""
    signal connectRequested(string address)

    function add(c) {
        if (entry.length >= 15) return
        if (c === "." && (entry.length === 0 || entry.endsWith(".") || entry.split(".").length > 3)) return
        entry += c
    }
    readonly property bool complete: /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.test(entry)
        && entry.split(".").every(p => Number(p) <= 255)
    function commit() {
        if (!complete) return
        dialog.close()
        dialog.connectRequested(entry)
    }

    onAboutToShow: entry = Engine.lastAddress
    onOpened: keyCatcher.forceActiveFocus()

    Item {
        id: keyCatcher
        // The operate page leaves the keyboard focus here (see OperatePage).
        readonly property bool keepsKeyboard: true
        Keys.onPressed: (event) => {
            if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
                dialog.add(event.text)
            } else if (event.key === Qt.Key_Period || event.key === Qt.Key_Comma) {
                dialog.add(".")
            } else if (event.key === Qt.Key_Backspace) {
                dialog.entry = dialog.entry.slice(0, -1)
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                dialog.commit()
            } else {
                return
            }
            event.accepted = true
        }
    }

    ColumnLayout {
        id: column
        width: dialog.availableWidth
        spacing: Theme.spacing

        Label {
            text: qsTr("Radio IP address")
            color: Theme.textDim
            font.pixelSize: 15
        }
        Label {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignRight
            text: dialog.entry.length > 0 ? dialog.entry : qsTr("e.g. 192.168.1.50")
            color: dialog.entry.length > 0 ? (dialog.complete ? Theme.text : Theme.warning) : Theme.textDim
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 18
            font.pixelSize: 34
            font.family: "monospace"
        }
        GridLayout {
            columns: 3
            Layout.fillWidth: true
            Repeater {
                model: ["7", "8", "9", "4", "5", "6", "1", "2", "3", ".", "0", "⌫"]
                delegate: AppButton {
                    required property string modelData
                    text: modelData
                    font.pixelSize: 26
                    Layout.fillWidth: true
                    Layout.preferredHeight: dialog.keyHeight
                    onClicked: modelData === "⌫" ? dialog.entry = dialog.entry.slice(0, -1) : dialog.add(modelData)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing
            AppButton {
                Layout.fillWidth: true
                Layout.preferredHeight: dialog.keyHeight
                text: qsTr("Cancel")
                font.pixelSize: 20
                onClicked: dialog.close()
            }
            AppButton {
                Layout.fillWidth: true
                Layout.preferredHeight: dialog.keyHeight
                text: qsTr("Connect")
                font.pixelSize: 22
                enabled: dialog.complete
                onClicked: dialog.commit()
            }
        }
    }
}
