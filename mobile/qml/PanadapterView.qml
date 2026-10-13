import QtQuick
import QtQuick.Controls
import AetherMobile

// Panadapter and waterfall of the active pan with the slice marker, filter
// passband and all tuning gestures (tap, drag, Pencil, pinch, wheel). Shared
// by the tablet and phone layouts.
Rectangle {
    id: panArea
    color: Theme.surface
    radius: Theme.radius
    clip: true

    SpectrumItem {
        id: spectrum
        anchors.fill: parent
        engine: Engine
    }

    // 3D stacked trace on the GPU, over the waterfall area.
    Spectrum3DItem {
        visible: Engine.spectrum3D
        engine: Engine
        x: 0
        y: Math.round(parent.height * spectrum.spectrumFraction)
        width: parent.width
        height: parent.height - y
        floorDbm: spectrum.traceBottomDbm + 8
        rangeDb: Math.max(20, Math.min(100, spectrum.traceTopDbm - spectrum.traceBottomDbm - 8))
        colorGain: Engine.wfColorGain
    }

    // Filter passband between the slice's filter edges (Hz offsets from the
    // carrier, so it sits on the correct side for USB/LSB and centred for
    // CW/AM): a fill over the spectrum and bright edge lines through the
    // waterfall, where a fill would vanish into the colours.
    Item {
        id: passband
        visible: Engine.panBandwidthMhz > 0 && Engine.filterHigh > Engine.filterLow
        readonly property real pxPerMhz: parent.width / Engine.panBandwidthMhz
        x: (Engine.frequencyMhz + Engine.filterLow / 1e6 - Engine.panCenterMhz) * pxPerMhz
           + parent.width / 2
        width: Math.max(2, (Engine.filterHigh - Engine.filterLow) / 1e6 * pxPerMhz)
        height: parent.height

        Rectangle {
            width: parent.width
            height: parent.height * spectrum.spectrumFraction
            color: Theme.passband
            opacity: 0.22
        }
        Rectangle {
            x: 0
            width: 1.5
            height: parent.height
            color: Theme.passband
            opacity: 0.9
        }
        Rectangle {
            x: parent.width - width
            width: 1.5
            height: parent.height
            color: Theme.passband
            opacity: 0.9
        }
    }

    // The other slices on this pan: dim markers with their letter; a tap
    // near one selects it.
    readonly property string activePanId: {
        const list = Engine.slices
        for (let i = 0; i < list.length; ++i) {
            if (list[i].active) return list[i].panId
        }
        return ""
    }
    Repeater {
        model: Engine.slices
        delegate: Item {
            required property var modelData
            visible: !modelData.active && modelData.panId === panArea.activePanId && Engine.panBandwidthMhz > 0
            x: (modelData.frequency - Engine.panCenterMhz) / Engine.panBandwidthMhz * panArea.width
               + panArea.width / 2 - width / 2
            width: 36
            height: panArea.height
            z: 2
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 2
                height: parent.height
                color: Theme.textDim
                opacity: 0.8
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 4
                text: modelData.letter + (modelData.tx ? " TX" : "")
                color: Theme.text
                font.pixelSize: 13
                font.bold: true
                background: Rectangle { color: Theme.surfaceRaised; radius: 4; opacity: 0.9 }
                padding: 3
            }
            TapHandler { onTapped: Engine.selectSlice(modelData.id) }
        }
    }

    // Latest decoded CW along the top of the waterfall (just under the
    // trace), where no layout's bottom bar can cover it.
    Rectangle {
        visible: Engine.cwDecoderEnabled && Engine.cwMode && Engine.cwDecodedText.length > 0
        anchors.left: parent.left
        anchors.right: parent.right
        y: parent.height * spectrum.spectrumFraction
        height: 28
        color: "#cc000000"
        z: 3
        Label {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            text: Engine.cwDecodedText
            color: "white"
            font.family: "monospace"
            font.pixelSize: 15
            elide: Text.ElideLeft
            verticalAlignment: Text.AlignVCenter
        }
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

    // dBm scale on the left of the spectrum (the reference level control of
    // SmartSDR/AetherSDR): drag it up or down to move the trace. A drag
    // switches the trace to manual; Auto in the View tab gives it back.
    Item {
        id: scaleStrip
        z: 4
        width: 40
        height: parent.height * spectrum.spectrumFraction
        readonly property real topDbm: spectrum.traceTopDbm
        readonly property real bottomDbm: spectrum.traceBottomDbm
        readonly property real range: Math.max(1, topDbm - bottomDbm)
        // The smallest round step that keeps labels at least 18 px apart.
        readonly property int step: {
            for (const st of [10, 20, 30, 50, 100]) {
                if (st / range * height >= 18) return st
            }
            return 100
        }
        Rectangle { anchors.fill: parent; color: Theme.surface; opacity: scaleDrag.pressed ? 0.85 : 0.55 }
        Repeater {
            model: {
                const ticks = []
                for (let d = Math.ceil(scaleStrip.bottomDbm / scaleStrip.step) * scaleStrip.step;
                     d <= scaleStrip.topDbm; d += scaleStrip.step) ticks.push(d)
                return ticks
            }
            delegate: Label {
                required property int modelData
                x: 3
                y: (scaleStrip.topDbm - modelData) / scaleStrip.range * scaleStrip.height - height / 2
                visible: y > 0 && y < scaleStrip.height - height
                text: modelData
                color: Theme.textDim
                font.pixelSize: 11
            }
        }
        MouseArea {
            id: scaleDrag
            anchors.fill: parent
            preventStealing: true
            property real startTop: 0
            property real startY: 0
            property real span: 90
            onPressed: (mouse) => {
                startTop = scaleStrip.topDbm
                span = scaleStrip.range
                startY = mouse.y
                if (Engine.traceAutoRange) {
                    Engine.traceRangeDb = Math.round(span)
                    Engine.traceRefDbm = Math.round(startTop)
                    Engine.traceAutoRange = false
                }
            }
            // The trace follows the finger: dragging down raises the top level.
            onPositionChanged: (mouse) => {
                Engine.traceRefDbm = Math.round(startTop + (mouse.y - startY) / height * span)
            }
        }
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

    // Finger drag, as on the desktop (SpectrumWidget): started inside the
    // filter passband it tunes (the slice follows the finger); started
    // anywhere else it grabs the display and moves the view, slice
    // unchanged. While tuning, holding the finger in either edge zone keeps
    // going, faster the closer to the edge.
    DragHandler {
        id: drag
        target: null
        acceptedDevices: PointerDevice.TouchScreen | PointerDevice.Mouse | PointerDevice.TouchPad
        xAxis.enabled: true
        yAxis.enabled: false
        property bool tuning: false
        property real startMhz: 0
        property real edgeMhz: 0
        readonly property real span: Engine.panBandwidthMhz > 0 ? Engine.panBandwidthMhz : 0.2
        readonly property real edgeZone: 0.12
        // -1..1: how deep the finger is in the left/right edge zone.
        readonly property real edgeDepth: {
            if (!active || !tuning) return 0
            const f = centroid.position.x / panArea.width
            if (f < edgeZone) return -Math.min(1, (edgeZone - f) / edgeZone)
            if (f > 1 - edgeZone) return Math.min(1, (f - 1 + edgeZone) / edgeZone)
            return 0
        }
        function apply() {
            const shift = translation.x / panArea.width * span
            if (tuning) Engine.tuneTo(startMhz + shift + edgeMhz)
            else Engine.panTo(startMhz - shift)
        }
        onActiveChanged: {
            if (!active) return
            // The passband, widened to a finger's width.
            const x = centroid.pressPosition.x
            const half = Math.max(passband.width, 48) / 2
            const mid = passband.x + passband.width / 2
            tuning = passband.visible && Math.abs(x - mid) <= half
            startMhz = tuning ? Engine.frequencyMhz : Engine.panCenterMhz
            edgeMhz = 0
        }
        onTranslationChanged: apply()
    }
    // Edge auto-scroll while tuning: from a quarter span per second at the
    // zone's inner border up to three spans per second at the very edge.
    Timer {
        interval: 50
        repeat: true
        running: drag.edgeDepth !== 0
        onTriggered: {
            const d = drag.edgeDepth
            const spansPerSecond = 0.25 + 2.75 * d * d
            drag.edgeMhz += Math.sign(d) * spansPerSecond * drag.span * interval / 1000
            drag.apply()
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
            Engine.tuneTo(startMhz + translation.x / panArea.width * span / 10)
        }
    }

    // Pinch changes the span around the slice; applied on release
    // so the radio gets one request per gesture.
    PinchHandler {
        target: null
        rotationAxis.enabled: false
        // A second finger takes the gesture over from the one-finger
        // tuning drag.
        grabPermissions: PointerHandler.CanTakeOverFromAnything
        // activeScale is back to 1 by the time active turns false, so
        // remember the last value seen during the gesture.
        property real lastScale: 1.0
        onActiveScaleChanged: if (active) lastScale = activeScale
        onActiveChanged: {
            if (active) {
                lastScale = 1.0
            } else if (lastScale > 0 && Math.abs(lastScale - 1.0) > 0.05) {
                Engine.zoom(1.0 / lastScale)
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
