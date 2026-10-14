import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// AGC from the info panel: the mode (off/slow/med/fast; picking one keeps
// the popup open), the AGC threshold (AGC-T) and the panadapter's RF gain.
Popup {
    id: chooser
    objectName: "agcChooser"
    margins: Theme.spacing
    padding: Theme.spacing
    width: Math.min(420, (parent ? parent.width : 420))
    background: Rectangle {
        color: Theme.surface
        border.color: Theme.surfaceRaised
        radius: Theme.radius
    }
    contentItem: ColumnLayout {
        spacing: Theme.spacing / 2
        Label {
            text: qsTr("AGC")
            color: Theme.textDim
            font.pixelSize: Math.round(Theme.buttonFont * 0.85)
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: Engine.agcModes
                delegate: AppButton {
                    required property string modelData
                    text: modelData
                    checked: Engine.agcMode === modelData
                    focusPolicy: Qt.NoFocus
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: Engine.agcMode = modelData
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("AGC-T  %1").arg(Engine.agcThreshold)
                color: Theme.textDim
                font.pixelSize: 14
                Layout.preferredWidth: 90
            }
            Slider {
                Layout.fillWidth: true
                enabled: Engine.connectionState === "connected" && Engine.agcMode !== "off"
                from: 0; to: 100; stepSize: 1
                value: Engine.agcThreshold
                onMoved: Engine.agcThreshold = Math.round(value)
            }
        }
        // RF gain (preamp / attenuator) of the panadapter, in the radio's steps.
        RowLayout {
            Layout.fillWidth: true
            visible: Engine.connectionState === "connected"
            Label {
                text: qsTr("RF gain  %1 dB").arg(Engine.rfGain)
                color: Theme.textDim
                font.pixelSize: 14
                Layout.preferredWidth: 90
            }
            Slider {
                Layout.fillWidth: true
                from: Engine.rfGainLow
                to: Engine.rfGainHigh
                stepSize: Engine.rfGainStep
                snapMode: Slider.SnapAlways
                value: Engine.rfGain
                onMoved: Engine.rfGain = Math.round(value)
            }
        }
    }
}
