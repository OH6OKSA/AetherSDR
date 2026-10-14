import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// A small chooser: a title over the options as the app's buttons, the
// current one checked. Picking one emits picked() and closes.
Popup {
    id: chooser
    property string title: ""
    property var options: []
    property string current: ""
    signal picked(string name)

    margins: Theme.spacing
    padding: Theme.spacing
    background: Rectangle {
        color: Theme.surface
        border.color: Theme.surfaceRaised
        radius: Theme.radius
    }
    contentItem: ColumnLayout {
        spacing: Theme.spacing / 2
        Label {
            visible: chooser.title.length > 0
            text: chooser.title
            color: Theme.textDim
            font.pixelSize: Math.round(Theme.buttonFont * 0.85)
        }
        GridLayout {
            columns: Math.min(4, Math.max(1, chooser.options.length))
            columnSpacing: 4
            rowSpacing: 4
            Repeater {
                model: chooser.options
                delegate: AppButton {
                    required property string modelData
                    text: modelData
                    checked: chooser.current === modelData
                    focusPolicy: Qt.NoFocus
                    Layout.preferredWidth: Math.max(Theme.buttonWidth * 0.9, 64)
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: {
                        chooser.picked(modelData)
                        chooser.close()
                    }
                }
            }
        }
    }
}
