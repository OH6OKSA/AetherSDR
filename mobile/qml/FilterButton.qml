import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Receive filter, right of the tuning lock, as on SmartSDR/AetherSDR: the
// button (the lock's size) shows the width in use; tap it for the mode's
// eight widths (the active one checked) and the radio's DSP toggles.
Item {
    id: filters
    Layout.fillWidth: true
    implicitWidth: button.width
    implicitHeight: Theme.touchTarget

    readonly property real cellWidth: Math.max(Theme.buttonWidth * 0.8, 60)
    function label(hz) {
        if (hz >= 1000) {
            return (hz / 1000).toFixed(1).replace(/\.0$/, "") + "k"
        }
        return String(hz)
    }

    AppButton {
        id: button
        text: Engine.filterPresets.length > 0 ? filters.label(Engine.filterWidth) : qsTr("DSP")
        font.pixelSize: Math.round(Theme.buttonFont * 0.9)
        leftPadding: 1
        rightPadding: 1
        width: Theme.touchTarget
        height: Theme.touchTarget
        focusPolicy: Qt.NoFocus
        onClicked: chooser.open()
    }

    Popup {
        id: chooser
        objectName: "filterChooser"
        parent: button
        y: button.height + 4
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
                visible: Engine.filterPresets.length > 0
                text: qsTr("%1 filter").arg(Engine.mode)
                color: Theme.textDim
                font.pixelSize: Math.round(Theme.buttonFont * 0.85)
            }
            GridLayout {
                columns: Math.min(4, Math.max(1, Engine.filterPresets.length))
                columnSpacing: 4
                rowSpacing: 4
                Repeater {
                    model: Engine.filterPresets
                    delegate: AppButton {
                        required property int modelData
                        text: filters.label(modelData)
                        checked: Engine.activeFilterPreset === modelData
                        focusPolicy: Qt.NoFocus
                        Layout.preferredWidth: filters.cellWidth
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: {
                            Engine.setFilterWidth(modelData)
                            chooser.close()
                        }
                    }
                }
            }
            Label {
                text: qsTr("DSP")
                color: Theme.textDim
                font.pixelSize: Math.round(Theme.buttonFont * 0.85)
            }
            // Toggles stay open: several are often set together.
            GridLayout {
                columns: 4
                columnSpacing: 4
                rowSpacing: 4
                Repeater {
                    model: Engine.dspKeys
                    delegate: AppButton {
                        required property string modelData
                        text: modelData
                        // Re-read on every DSP change (dspOn is a method).
                        checked: Engine.dspKeys.length >= 0 && Engine.dspOn(modelData)
                        focusPolicy: Qt.NoFocus
                        Layout.preferredWidth: filters.cellWidth
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: Engine.setDsp(modelData, !checked)
                    }
                }
            }
        }
    }
}
