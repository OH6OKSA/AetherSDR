import QtQuick
import QtQuick.Controls
import AetherMobile
import QtQuick.Layouts

Page {
    id: page
    background: Rectangle { color: Theme.background }

    function formatMhz(mhz) {
        // 14.074.500 style: MHz . kHz . Hz
        const hz = Math.round(mhz * 1e6)
        const mhzPart = Math.floor(hz / 1e6)
        const khzPart = Math.floor((hz % 1e6) / 1e3)
        const hzPart = hz % 1e3
        return mhzPart + "." + String(khzPart).padStart(3, "0") + "." + String(hzPart).padStart(3, "0")
    }

    header: ToolBar {
        background: Rectangle { color: Theme.surface }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacing
            anchors.rightMargin: Theme.spacing
            Label {
                text: Engine.radioName
                color: Theme.textDim
                font.pixelSize: 18
                Layout.fillWidth: true
            }
            ToolButton {
                text: qsTr("Disconnect")
                onClicked: Engine.disconnectRadio()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacing

        // Left: frequency, S-meter and the panadapter area.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing

            RowLayout {
                spacing: 2 * Theme.spacing
                Label {
                    text: page.formatMhz(Engine.frequencyMhz)
                    color: Theme.text
                    font.pixelSize: 56
                    font.family: "monospace"
                    // Tap for direct entry.
                    TapHandler { onTapped: freqDialog.open() }
                }
                Label {
                    text: Engine.mode
                    color: Theme.accent
                    font.pixelSize: 32
                }
                Item { Layout.fillWidth: true }
                ColumnLayout {
                    Label {
                        text: Engine.sMeterText
                        color: Theme.text
                        font.pixelSize: 22
                    }
                    ProgressBar {
                        Layout.preferredWidth: 260
                        from: -130
                        to: -20
                        value: Engine.sMeterDbm
                    }
                }
            }

            // Band stack (Flex): restores the band's last frequency and mode.
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacing / 2
                visible: Engine.bandsAvailable
                Repeater {
                    model: Engine.bands
                    delegate: Button {
                        required property string modelData
                        text: modelData
                        width: 72
                        height: Theme.touchTarget
                        onClicked: Engine.selectBand(modelData)
                    }
                }
            }

            // Panadapter + waterfall land here (SpectrumItem).
            Rectangle {
                id: panArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surface
                radius: Theme.radius
                clip: true

                SpectrumItem {
                    id: spectrum
                    anchors.fill: parent
                    engine: Engine
                }

                // Slice marker: where the active slice sits inside the pan.
                Rectangle {
                    visible: Engine.panBandwidthMhz > 0
                    width: 2
                    height: parent.height
                    color: Theme.warning
                    opacity: 0.8
                    x: (Engine.frequencyMhz - Engine.panCenterMhz) / Engine.panBandwidthMhz * parent.width
                       + parent.width / 2 - width / 2
                }

                // Tap to tune to the touched frequency.
                TapHandler {
                    onTapped: (eventPoint) => {
                        if (Engine.panBandwidthMhz > 0) {
                            Engine.frequencyMhz = Engine.panCenterMhz
                                + (eventPoint.position.x / panArea.width - 0.5) * Engine.panBandwidthMhz
                        }
                    }
                }

                // Horizontal drag tunes: one pan width per screen width.
                DragHandler {
                    id: tuneDrag
                    target: null
                    xAxis.enabled: true
                    yAxis.enabled: false
                    property real startMhz: 0
                    onActiveChanged: if (active) startMhz = Engine.frequencyMhz
                    onTranslationChanged: {
                        const span = Engine.panBandwidthMhz > 0 ? Engine.panBandwidthMhz : 0.2
                        Engine.frequencyMhz = startMhz - translation.x / panArea.width * span
                    }
                }
            }

            RowLayout {
                spacing: Theme.spacing
                Repeater {
                    model: [-1000, -100, -10, 10, 100, 1000]
                    delegate: Button {
                        required property int modelData
                        text: (modelData > 0 ? "+" : "") + (Math.abs(modelData) >= 1000 ? modelData / 1000 + " kHz" : modelData + " Hz")
                        Layout.preferredHeight: Theme.touchTarget
                        Layout.fillWidth: true
                        onClicked: Engine.tuneTo(Engine.frequencyMhz + modelData / 1e6)
                    }
                }
            }
        }

        // Right: controls and PTT.
        ColumnLayout {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            spacing: Theme.spacing

            Label { text: qsTr("Mode"); color: Theme.textDim; font.pixelSize: 16 }
            GridLayout {
                columns: 3
                Layout.fillWidth: true
                Repeater {
                    model: Engine.modes
                    delegate: Button {
                        required property string modelData
                        text: modelData
                        checkable: true
                        checked: Engine.mode === modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: Engine.mode = modelData
                    }
                }
            }

            Label {
                text: qsTr("Filter  %1 Hz").arg(Engine.filterHigh - Engine.filterLow)
                color: Theme.textDim
                font.pixelSize: 16
                visible: Engine.filterPresets.length > 0
            }
            RowLayout {
                Layout.fillWidth: true
                visible: Engine.filterPresets.length > 0
                Repeater {
                    model: Engine.filterPresets
                    delegate: Button {
                        required property int modelData
                        text: modelData >= 1000 ? (modelData / 1000).toFixed(1) + "k" : modelData
                        checkable: true
                        checked: Math.abs((Engine.filterHigh - Engine.filterLow) - modelData) < 60
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: Engine.setFilterWidth(modelData)
                    }
                }
            }

            Label { text: qsTr("AF gain"); color: Theme.textDim; font.pixelSize: 16 }
            RowLayout {
                Slider {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: Engine.afGain
                    onMoved: Engine.afGain = value
                }
                Button {
                    text: Engine.muted ? qsTr("Unmute") : qsTr("Mute")
                    onClicked: Engine.muted = !Engine.muted
                }
            }

            Item { Layout.fillHeight: true }

            Label {
                Layout.fillWidth: true
                visible: !Engine.transmitAvailable
                text: Engine.transmitUnavailableReason
                color: Theme.textDim
                wrapMode: Text.Wrap
            }
            Button {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.touchTarget
                visible: Engine.canUseDeviceMicrophone
                text: qsTr("Use this device's microphone")
                onClicked: Engine.useDeviceMicrophone()
            }
            Label {
                Layout.fillWidth: true
                visible: Engine.lastError.length > 0
                text: Engine.lastError
                color: Theme.warning
                wrapMode: Text.Wrap
            }

            Button {
                id: ptt
                Layout.fillWidth: true
                Layout.preferredHeight: 3 * Theme.touchTarget
                enabled: Engine.transmitAvailable
                text: Engine.transmitting ? qsTr("TX") : qsTr("PTT")
                font.pixelSize: 32
                font.bold: true
                background: Rectangle {
                    radius: Theme.radius
                    color: Engine.transmitting ? Theme.transmit
                         : ptt.enabled ? Theme.surfaceRaised : Theme.surface
                }
                // Hold to transmit; release (or losing the touch) always unkeys.
                onPressed: Engine.setPtt(true)
                onReleased: Engine.setPtt(false)
                onCanceled: Engine.setPtt(false)
            }
        }
    }

    FrequencyDialog { id: freqDialog }
}
