import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The slice's mode next to the frequency: tap to choose another from the
// app's buttons (the current one is the dark, checked button).
Item {
    id: modeButton
    property int fontSize: 32
    implicitWidth: row.implicitWidth
    implicitHeight: Math.max(row.implicitHeight, Theme.touchTarget)

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        Label {
            text: Engine.mode
            color: Theme.accent
            font.pixelSize: modeButton.fontSize
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: "▾"
            color: Theme.accent
            font.pixelSize: Math.max(13, Math.round(modeButton.fontSize * 0.55))
        }
    }
    TapHandler { onTapped: chooser.open() }

    Popup {
        id: chooser
        objectName: "modeChooser"
        y: modeButton.height + 4
        margins: Theme.spacing
        padding: Theme.spacing
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.surfaceRaised
            radius: Theme.radius
        }
        contentItem: GridLayout {
            columns: Math.min(4, Math.max(1, Engine.modes.length))
            columnSpacing: 4
            rowSpacing: 4
            Repeater {
                model: Engine.modes
                delegate: AppButton {
                    required property string modelData
                    text: modelData
                    checked: Engine.mode === modelData
                    focusPolicy: Qt.NoFocus
                    Layout.preferredWidth: Math.max(Theme.buttonWidth * 0.9, 64)
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: {
                        Engine.mode = modelData
                        chooser.close()
                    }
                }
            }
        }
    }
}
