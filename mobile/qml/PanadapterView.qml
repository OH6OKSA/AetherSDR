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

    function mhzAt(x) {
        return Engine.panCenterMhz + (x / panArea.width - 0.5) * Engine.panBandwidthMhz
    }

    // Press and hold, then drag: the whole view moves (pan centre) and the
    // slice stays where it is. Reset by the next press.
    property bool holdToPan: false

    // Tap (finger, Pencil or pointer) tunes to the touched point on
    // the tuning-step grid. A long press arms view panning.
    TapHandler {
        longPressThreshold: 0.4
        onPressedChanged: if (pressed) panArea.holdToPan = false
        onLongPressed: panArea.holdToPan = true
        onTapped: (eventPoint) => {
            if (Engine.panBandwidthMhz > 0) {
                Engine.tuneToSnapped(panArea.mhzAt(eventPoint.position.x))
            }
        }
    }

    // Finger drag moves the tuning in the drag's direction (right = up in
    // frequency), one pan width per screen width; after a long press it
    // moves the view instead. Holding the finger in the edge zone keeps
    // going, faster the closer to the edge.
    DragHandler {
        id: drag
        target: null
        acceptedDevices: PointerDevice.TouchScreen | PointerDevice.Mouse | PointerDevice.TouchPad
        xAxis.enabled: true
        yAxis.enabled: false
        property bool panning: false
        property real startMhz: 0
        property real edgeMhz: 0
        readonly property real span: Engine.panBandwidthMhz > 0 ? Engine.panBandwidthMhz : 0.2
        readonly property real edgeZone: 0.12
        // -1..1: how deep the finger is in the left/right edge zone.
        readonly property real edgeDepth: {
            if (!active) return 0
            const f = centroid.position.x / panArea.width
            if (f < edgeZone) return -Math.min(1, (edgeZone - f) / edgeZone)
            if (f > 1 - edgeZone) return Math.min(1, (f - 1 + edgeZone) / edgeZone)
            return 0
        }
        function apply() {
            const mhz = startMhz + translation.x / panArea.width * span + edgeMhz
            if (panning) Engine.panTo(mhz)
            else Engine.tuneTo(mhz)
        }
        onActiveChanged: {
            if (active) {
                panning = panArea.holdToPan
                startMhz = panning ? Engine.panCenterMhz : Engine.frequencyMhz
                edgeMhz = 0
            } else {
                panArea.holdToPan = false
            }
        }
        onTranslationChanged: apply()
    }
    // Edge auto-scroll: from a quarter span per second at the zone's inner
    // border up to three spans per second at the very edge.
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
    Label {
        visible: panArea.holdToPan
        anchors.horizontalCenter: parent.horizontalCenter
        y: 8
        z: 4
        text: qsTr("◀  move view  ▶")
        color: Theme.text
        font.pixelSize: 14
        padding: 6
        background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radius; opacity: 0.9 }
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
