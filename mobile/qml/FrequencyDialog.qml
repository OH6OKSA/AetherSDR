import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Direct frequency entry. A value below 100 is read as MHz, otherwise kHz,
// so "14.074" and "14074" both tune 14.074 MHz.
Dialog {
    id: dialog
    modal: true
    title: qsTr("Frequency")
    standardButtons: Dialog.Cancel
    anchors.centerIn: parent
    width: 420

    property string entry: ""

    function digit(d) {
        if (entry.length < 12) entry += d
    }
    function commit() {
        const v = parseFloat(entry.replace(",", "."))
        if (!isNaN(v) && v > 0) {
            Engine.tuneTo(v < 100 ? v : v / 1000)
        }
        dialog.close()
    }

    onAboutToShow: entry = ""
    // Typed digits work too on a hardware keyboard.
    onOpened: keyCatcher.forceActiveFocus()

    Item {
        id: keyCatcher
        Keys.onPressed: (event) => {
            if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9) {
                dialog.digit(event.text)
            } else if (event.key === Qt.Key_Period || event.key === Qt.Key_Comma) {
                if (dialog.entry.indexOf(".") < 0) dialog.digit(".")
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
        anchors.fill: parent
        spacing: Theme.spacing

        Label {
            Layout.fillWidth: true
            text: dialog.entry.length > 0 ? dialog.entry : qsTr("MHz or kHz")
            color: dialog.entry.length > 0 ? Theme.text : Theme.textDim
            font.pixelSize: 40
            font.family: "monospace"
            horizontalAlignment: Text.AlignRight
        }

        GridLayout {
            columns: 3
            Layout.fillWidth: true
            Repeater {
                model: ["7", "8", "9", "4", "5", "6", "1", "2", "3", ".", "0", "⌫"]
                delegate: Button {
                    required property string modelData
                    text: modelData
                    font.pixelSize: 26
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    onClicked: {
                        if (modelData === "⌫") {
                            dialog.entry = dialog.entry.slice(0, -1)
                        } else if (modelData !== "." || dialog.entry.indexOf(".") < 0) {
                            dialog.digit(modelData)
                        }
                    }
                }
            }
        }

        Button {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            text: qsTr("Tune")
            font.pixelSize: 24
            enabled: dialog.entry.length > 0
            onClicked: dialog.commit()
        }
    }
}
