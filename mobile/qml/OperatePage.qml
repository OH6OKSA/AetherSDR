import QtQuick
import QtQuick.Controls
import AetherMobile
import QtQuick.Layouts

Page {
    id: page
    focus: true

    // Hardware keyboard (Magic Keyboard): arrows tune by step (Shift x10),
    // +/- zoom, F opens direct entry. Space is hold-to-talk: press keys,
    // release unkeys, auto-repeat is ignored, and losing focus unkeys.
    property bool spacePtt: false
    Keys.onPressed: (event) => {
        if (event.isAutoRepeat && event.key === Qt.Key_Space) {
            event.accepted = true
            return
        }
        const mult = (event.modifiers & Qt.ShiftModifier) ? 10 : 1
        switch (event.key) {
        case Qt.Key_Left: Engine.tuneBySteps(-mult); break
        case Qt.Key_Right: Engine.tuneBySteps(mult); break
        case Qt.Key_Down: Engine.tuneBySteps(-10 * mult); break
        case Qt.Key_Up: Engine.tuneBySteps(10 * mult); break
        case Qt.Key_Plus:
        case Qt.Key_Equal: Engine.zoom(0.5); break
        case Qt.Key_Minus: Engine.zoom(2.0); break
        case Qt.Key_F: freqDialog.open(); break
        case Qt.Key_Space:
            page.spacePtt = true
            Engine.setPtt(true)
            break
        default: return
        }
        event.accepted = true
    }
    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            page.spacePtt = false
            Engine.setPtt(false)
            event.accepted = true
        }
    }
    onActiveFocusChanged: {
        if (!activeFocus && spacePtt) {
            spacePtt = false
            Engine.setPtt(false)
        }
    }
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
            // Zoom buttons narrow or widen the pan around the slice.
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacing / 2
                Button {
                    text: "−"
                    width: 56
                    height: Theme.touchTarget
                    font.pixelSize: 22
                    onClicked: Engine.zoom(2.0)
                }
                Button {
                    text: "+"
                    width: 56
                    height: Theme.touchTarget
                    font.pixelSize: 22
                    onClicked: Engine.zoom(0.5)
                }
                Item { width: Theme.spacing; height: 1 }
                Repeater {
                    model: Engine.bands
                    delegate: Button {
                        required property string modelData
                        text: modelData
                        width: 72
                        height: Theme.touchTarget
                        visible: Engine.bandsAvailable
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

                function mhzAt(x) {
                    return Engine.panCenterMhz + (x / panArea.width - 0.5) * Engine.panBandwidthMhz
                }

                // Tap (finger, Pencil or pointer) tunes to the touched point on
                // the tuning-step grid.
                TapHandler {
                    onTapped: (eventPoint) => {
                        if (Engine.panBandwidthMhz > 0) {
                            Engine.tuneToSnapped(panArea.mhzAt(eventPoint.position.x))
                        }
                    }
                }

                // Finger drag tunes one pan width per screen width.
                DragHandler {
                    target: null
                    acceptedDevices: PointerDevice.TouchScreen | PointerDevice.Mouse | PointerDevice.TouchPad
                    xAxis.enabled: true
                    yAxis.enabled: false
                    property real startMhz: 0
                    onActiveChanged: if (active) startMhz = Engine.frequencyMhz
                    onTranslationChanged: {
                        const span = Engine.panBandwidthMhz > 0 ? Engine.panBandwidthMhz : 0.2
                        Engine.tuneTo(startMhz - translation.x / panArea.width * span)
                    }
                }

                // Apple Pencil drags tune ten times finer, for zero-beating.
                DragHandler {
                    target: null
                    acceptedDevices: PointerDevice.Stylus
                    xAxis.enabled: true
                    yAxis.enabled: false
                    property real startMhz: 0
                    onActiveChanged: if (active) startMhz = Engine.frequencyMhz
                    onTranslationChanged: {
                        const span = Engine.panBandwidthMhz > 0 ? Engine.panBandwidthMhz : 0.2
                        Engine.tuneTo(startMhz - translation.x / panArea.width * span / 10)
                    }
                }

                // Pinch changes the span around the slice; applied on release
                // so the radio gets one request per gesture.
                PinchHandler {
                    target: null
                    rotationAxis.enabled: false
                    onActiveChanged: {
                        if (!active && activeScale > 0) {
                            Engine.zoom(1.0 / activeScale)
                        }
                    }
                }

                // Trackpad or mouse wheel: tune by steps; with Ctrl or Cmd, zoom.
                WheelHandler {
                    target: null
                    onWheel: (event) => {
                        const dy = event.angleDelta.y !== 0 ? event.angleDelta.y : -event.angleDelta.x
                        if (dy === 0) return
                        if (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) {
                            Engine.zoom(dy > 0 ? 0.8 : 1.25)
                        } else {
                            Engine.tuneBySteps(dy > 0 ? 1 : -1)
                        }
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
                focusPolicy: Qt.NoFocus
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

            // Receive DSP of the active slice (radio-side).
            RowLayout {
                Layout.fillWidth: true
                Repeater {
                    model: [{ key: "nr", label: "NR" }, { key: "nb", label: "NB" }, { key: "anf", label: "ANF" }]
                    delegate: Button {
                        required property var modelData
                        text: modelData.label
                        checkable: true
                        checked: Engine[modelData.key]
                        focusPolicy: Qt.NoFocus
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: Engine[modelData.key] = !Engine[modelData.key]
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("AGC"); color: Theme.textDim; font.pixelSize: 16 }
                Repeater {
                    model: Engine.agcModes
                    delegate: Button {
                        required property string modelData
                        text: modelData
                        checkable: true
                        checked: Engine.agcMode === modelData
                        focusPolicy: Qt.NoFocus
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: Engine.agcMode = modelData
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

            // Transmit settings and meters; dimmed until the radio can transmit.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing / 2
                enabled: Engine.transmitAvailable || Engine.canUseDeviceMicrophone
                opacity: enabled ? 1.0 : 0.4

                Label {
                    text: qsTr("RF power  %1").arg(Engine.rfPower)
                    color: Theme.textDim
                    font.pixelSize: 16
                }
                Slider {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    stepSize: 1
                    value: Engine.rfPower
                    onMoved: Engine.rfPower = Math.round(value)
                }
                Label {
                    text: qsTr("Mic gain  %1").arg(Engine.micGain)
                    color: Theme.textDim
                    font.pixelSize: 16
                }
                Slider {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    stepSize: 1
                    value: Engine.micGain
                    onMoved: Engine.micGain = Math.round(value)
                }
                // Mic level as the radio meters it: aim for peaks around -10 dBFS.
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Mic"); color: Theme.textDim; font.pixelSize: 16 }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 12
                        radius: 3
                        color: Theme.surface
                        Rectangle {
                            height: parent.height
                            radius: 3
                            width: parent.width * Math.max(0, Math.min(1, (Engine.micLevelDbfs + 40) / 40))
                            color: Engine.micPeakDbfs > -3 ? Theme.transmit
                                 : Engine.micPeakDbfs > -10 ? Theme.warning : Theme.accent
                        }
                    }
                }
                Label {
                    visible: Engine.transmitting
                    text: qsTr("%1 W   SWR %2")
                          .arg(Engine.forwardPowerW.toFixed(0))
                          .arg(Engine.swrValid ? Engine.swr.toFixed(1) : "–")
                    color: Engine.swrValid && Engine.swr > 2.5 ? Theme.transmit : Theme.text
                    font.pixelSize: 22
                }
            }

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
                focusPolicy: Qt.NoFocus
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

    FrequencyDialog {
        id: freqDialog
        onClosed: page.forceActiveFocus()
    }
    Component.onCompleted: page.forceActiveFocus()
}
