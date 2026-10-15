import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Panadapter and waterfall of the active pan with the slice marker, filter
// passband and all tuning gestures (tap, drag, Pencil, pinch, wheel). Shared
// by the tablet and phone layouts.
Rectangle {
    id: panArea
    // Width of the right meter above (from the page): the RF GAIN label
    // matches it; 0 sizes the label to its text.
    property real rfLabelWidth: 0
    color: Theme.surface
    radius: Theme.radius
    clip: true

    SpectrumItem {
        id: spectrum
        anchors.fill: parent
        engine: Engine
        // The 3D surface needs height; the desktop gives it about half. It
        // stretches with its area, so it keeps iPad landscape's shape (height
        // 0.158 of the width) in every orientation and the waterfall takes
        // the rest; never lower than two touch targets (phones in portrait).
        spectrumFraction: Engine.spectrum3D
            ? Math.min(0.5, (Math.max(width * 0.158, Theme.touchTarget * 2) + bandPlanHeight)
                            / Math.max(1, height))
            : 0.4
    }

    // 3D stacked trace on the GPU in place of the trace (the waterfall stays
    // below), as the desktop's 3DSS. The floor sits a little under the noise
    // so the noise reads dark blue, as there.
    Spectrum3DItem {
        visible: Engine.spectrum3D
        engine: Engine
        x: 0
        y: 0
        width: parent.width
        // Ends above the band plan strip, which stays visible under it.
        height: Math.round(parent.height * spectrum.spectrumFraction - spectrum.bandPlanHeight)
        floorDbm: spectrum.traceBottomDbm
        rangeDb: Math.max(20, Math.min(100, spectrum.traceTopDbm - spectrum.traceBottomDbm))
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
        visible: !Engine.spectrum3D
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
            // Bounded: a non-finite scale (no data yet) must never spin here.
            model: {
                const ticks = []
                const top = scaleStrip.topDbm
                const bottom = scaleStrip.bottomDbm
                if (!isFinite(top) || !isFinite(bottom) || top <= bottom) return ticks
                for (let d = Math.ceil(bottom / scaleStrip.step) * scaleStrip.step;
                     d <= top && ticks.length < 30; d += scaleStrip.step) ticks.push(d)
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


    // Zoom out / in, bottom left of the waterfall as in AetherSDR. Together
    // the size of one step button below (eight across the same width), each
    // half of it.
    Row {
        id: zoomRow
        readonly property real stepWidth: (panArea.width - 7 * Theme.spacing / 2) / 8
        z: 5
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 6
        spacing: 2
        Repeater {
            model: [{ label: "−", factor: 2.0 }, { label: "+", factor: 0.5 }]
            delegate: AppButton {
                required property var modelData
                text: modelData.label
                font.pixelSize: Math.round(Theme.buttonFont * 1.3)
                width: Math.max(32, zoomRow.stepWidth / 2 - 1)
                height: Theme.touchTarget
                padding: 0
                opacity: 0.85
                focusPolicy: Qt.NoFocus
                onClicked: Engine.zoom(modelData.factor)
            }
        }
    }

    // RF gain at the top right of the spectrum, under the right meter, as
    // the desktop shows the preamp/attenuator; tap for the AGC / AGC-T /
    // RF gain chooser (the same as the info panel's AGC).
    Rectangle {
        id: rfLabel
        objectName: "rfGainLabel"
        z: 5
        visible: Engine.connectionState === "connected"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 6
        // Under the meter, edge to edge with it.
        anchors.rightMargin: panArea.rfLabelWidth > 0 ? 0 : 6
        // Half the right meter above it (the page passes its width).
        width: panArea.rfLabelWidth > 0 ? Math.max(rfText.implicitWidth * 0.6 + 12, panArea.rfLabelWidth * 0.5) : rfText.implicitWidth + 16
        height: Math.max(rfText.implicitHeight + 8, Math.round(Theme.touchTarget * 0.7))
        radius: 4
        color: rfArea.pressed ? Theme.surfaceRaised : Theme.surface
        opacity: 0.9
        Text {
            id: rfText
            anchors.centerIn: parent
            width: parent.width - 10
            horizontalAlignment: Text.AlignHCenter
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 9
            text: qsTr("RF GAIN %1%2 dB").arg(Engine.rfGain > 0 ? "+" : "").arg(Engine.rfGain)
            color: Engine.rfGain > 0 ? "#3fb6ff" : Engine.rfGain < 0 ? Theme.warning : Theme.text
            font.pixelSize: Theme.buttonFont
            font.bold: true
        }
        MouseArea {
            id: rfArea
            anchors.fill: parent
            anchors.margins: -6
            onClicked: rfChooser.open()
        }
        AgcChooser {
            id: rfChooser
            objectName: "rfGainChooser"
            width: Math.min(420, panArea.width - 24)
            x: rfLabel.width - width
            y: rfLabel.height + 4
        }
    }

    // "+ Slice" / "+ Pan", bottom right of the waterfall, the size of the
    // zoom buttons (wider only where the label needs it, iPhone).
    Row {
        id: addRow
        z: 5
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 6
        spacing: 2
        readonly property real cellWidth: Math.max(32, zoomRow.stepWidth / 2 - 1)
        // The app shows one panadapter; any others still stream to the device
        // (bandwidth, packet loss) and hold the radio's panadapter slots.
        AppButton {
            visible: Engine.panCount > 1
            text: qsTr("Close %1 other pans").arg(Engine.panCount - 1)
            font.pixelSize: Math.round(Theme.buttonFont * 0.85)
            leftPadding: 6
            rightPadding: 6
            height: Theme.touchTarget
            opacity: 0.85
            focusPolicy: Qt.NoFocus
            onClicked: Engine.closeOtherPanadapters()
        }
        Repeater {
            model: [{ label: qsTr("+ Slice"), pan: false }, { label: qsTr("+ Pan"), pan: true }]
            delegate: AppButton {
                required property var modelData
                visible: modelData.pan ? Engine.canAddPanadapter : Engine.canAddSlice
                text: modelData.label
                font.pixelSize: Math.round(Theme.buttonFont * 0.85)
                leftPadding: 2
                rightPadding: 2
                width: Math.max(addRow.cellWidth, implicitContentWidth + 6)
                height: Theme.touchTarget
                opacity: 0.85
                focusPolicy: Qt.NoFocus
                onClicked: modelData.pan ? Engine.addPanadapter() : Engine.addSlice()
            }
        }
    }

    // Notices and refusals (Engine.lastError) pop up in the middle of the
    // display, where they cannot be missed; a tap or 6 s dismisses them.
    // A popup, so it also shows over an open controls drawer.
    Popup {
        id: noticePopup
        objectName: "noticePopup"
        property string text: ""
        parent: panArea
        x: Math.round((panArea.width - width) / 2)
        y: Math.round((panArea.height - height) / 2)
        width: Math.min(panArea.width - 24, Math.max(240, noticeText.implicitWidth + 2 * Theme.spacing + 2))
        padding: Theme.spacing
        modal: false
        focus: false
        // Never closes on a press elsewhere: that press may be PTT or a
        // tune, and must reach it. A tap on the notice or 6 s closes it.
        closePolicy: Popup.NoAutoClose
        background: Rectangle {
            radius: Theme.radius
            color: Theme.surface
            border.color: Theme.warning
            border.width: 2
            opacity: 0.97
        }
        contentItem: Text {
            id: noticeText
            text: noticePopup.text
            color: Theme.text
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Math.round(Theme.buttonFont * 1.05)
            MouseArea { anchors.fill: parent; onClicked: noticePopup.close() }
        }
        Timer {
            id: noticeTimer
            interval: 6000
            onTriggered: noticePopup.close()
        }
        Connections {
            target: Engine
            function onLastErrorChanged() {
                // The no-slice notice already says it, with a retry.
                if (Engine.lastError.length > 0 && !noSliceNotice.visible) {
                    noticePopup.text = Engine.lastError
                    noticePopup.open()
                    noticeTimer.restart()
                } else if (Engine.lastError.length === 0) {
                    noticePopup.close()
                }
            }
        }
    }

    // Connected but without a slice (the radio refused one, e.g. all its
    // slices held by other programs): nothing can tune, so say so over the
    // display, with a retry. A moment's grace while the first slice arrives.
    readonly property bool sliceless: Engine.connectionState === "connected" && Engine.slices.length === 0
    Timer {
        id: slicelessGrace
        interval: 4000
        running: panArea.sliceless
        onRunningChanged: if (!running) noSliceNotice.shown = false
        onTriggered: noSliceNotice.shown = true
    }
    Rectangle {
        id: noSliceNotice
        objectName: "noSliceNotice"
        property bool shown: false
        property bool preview: false   // screenshots (--preview-no-slice)
        visible: (shown && panArea.sliceless) || preview
        z: 8
        anchors.centerIn: parent
        width: Math.min(parent.width - 24, 460)
        height: noSliceColumn.implicitHeight + 2 * Theme.spacing
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.warning
        ColumnLayout {
            id: noSliceColumn
            anchors.fill: parent
            anchors.margins: Theme.spacing
            spacing: Theme.spacing / 2
            Label {
                Layout.fillWidth: true
                text: qsTr("No slice")
                color: Theme.warning
                font.pixelSize: Math.round(Theme.buttonFont * 1.2)
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.text
                text: Engine.lastError.length > 0 ? Engine.lastError
                      : qsTr("The radio has not given this app a slice yet, so it cannot tune.")
            }
            AppButton {
                text: qsTr("Try again")
                leftPadding: 16
                rightPadding: 16
                Layout.preferredHeight: Theme.touchTarget
                focusPolicy: Qt.NoFocus
                onClicked: Engine.addSlice()
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
