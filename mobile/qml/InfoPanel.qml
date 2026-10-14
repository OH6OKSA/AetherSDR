import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The active slice at a glance and in reach, after the slice flag of
// SmartSDR-style clients: slice letter, TX tag, RX antenna (blue), TX
// antenna (red), filter width, the speaker and the tuning lock; then the
// DSP flags (lit when on) and AGC, which can run long. Each is a
// finger-sized target with its choices behind it: antennas open their
// chooser, the width and the flags the filter/DSP chooser, AGC its modes;
// the speaker mutes, the lock locks.
// Two rows next to the frequency on a tablet; one row elsewhere.
Item {
    id: info
    property bool singleLine: false

    readonly property var activeSlice: {
        for (const s of Engine.slices) {
            if (s.active) return s
        }
        return null
    }
    readonly property real fontPx: singleLine ? Math.round(Theme.buttonFont * 0.95)
                                              : Math.round(Theme.buttonFont * 1.15)
    // Height of a row, and so of every target in it.
    readonly property real rowH: singleLine ? Theme.touchTarget : Math.round(Theme.touchTarget * 0.8)
    readonly property real gap: Math.round(fontPx * 0.45)
    readonly property color dim: "#6b7785"
    readonly property color rxColor: "#3fb6ff"

    function widthLabel(hz) {
        if (hz <= 0) return ""
        return hz >= 1000 ? (hz / 1000).toFixed(1).replace(/\.0$/, "") + "K" : hz + ""
    }
    // Flags in a fixed order, lit when on; mode-dependent ones only where
    // they apply (APF and QSK in CW, BC in voice); the extra radio filters
    // only while on.
    readonly property var flags: {
        Engine.dspKeys.length   // re-evaluate on DSP changes
        const out = []
        for (const k of ["NB", "NR", "ANF"]) {
            out.push({ text: k, on: Engine.dspOn(k) })
        }
        for (const k of ["NRL", "ANFL", "ANFT", "NRS", "RNN", "NRF"]) {
            if (Engine.dspOn(k)) out.push({ text: k, on: true })
        }
        if (Engine.dspKeys.indexOf("APF") >= 0) out.push({ text: "APF", on: Engine.dspOn("APF") })
        if (Engine.dspKeys.indexOf("BC") >= 0) out.push({ text: "BC", on: Engine.dspOn("BC") })
        if (Engine.cwMode) out.push({ text: "QSK", on: Engine.cwBreakIn })
        return out
    }
    readonly property string agcText: {
        const m = Engine.agcMode.toLowerCase()
        return m === "off" ? "AGC-OFF" : "AGC-" + m.charAt(0).toUpperCase()
    }

    implicitWidth: lines.implicitWidth
    implicitHeight: lines.implicitHeight

    // A tappable word: the row's full height, a little wider than the text,
    // pressed feedback.
    component Hit: Item {
        id: hit
        property alias text: label.text
        property alias color: label.color
        property real fontPx: 16
        property real rowH: 40
        signal tapped()
        implicitWidth: label.implicitWidth + 12
        implicitHeight: rowH
        Rectangle {
            anchors.fill: parent
            radius: 6
            color: "#ffffff"
            opacity: area.pressed ? 0.15 : 0
        }
        Text {
            id: label
            anchors.centerIn: parent
            font.pixelSize: hit.fontPx
            font.bold: true
        }
        MouseArea {
            id: area
            anchors.fill: parent
            onClicked: hit.tapped()
        }
    }

    Grid {
        id: lines
        anchors.verticalCenter: parent.verticalCenter
        columns: info.singleLine ? 2 : 1
        columnSpacing: info.gap
        rowSpacing: 0
        verticalItemAlignment: Grid.AlignVCenter

        // Slice letter, TX, antennas, filter width, speaker, lock.
        Row {
            spacing: 0
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                rightPadding: info.gap
                Rectangle {
                    width: letterText.implicitWidth + 10
                    height: letterText.implicitHeight + 4
                    radius: 4
                    color: "#2f7fd6"
                    Text {
                        id: letterText
                        anchors.centerIn: parent
                        text: info.activeSlice ? info.activeSlice.letter : "–"
                        font.pixelSize: info.fontPx
                        font.bold: true
                        color: "#ffffff"
                    }
                }
                Rectangle {
                    visible: !!info.activeSlice && info.activeSlice.tx
                    width: txTagText.implicitWidth + 10
                    height: txTagText.implicitHeight + 4
                    radius: 4
                    color: Theme.transmit
                    Text {
                        id: txTagText
                        anchors.centerIn: parent
                        text: "TX"
                        font.pixelSize: info.fontPx
                        font.bold: true
                        color: "#ffffff"
                    }
                }
            }
            Hit {
                visible: Engine.rxAntenna.length > 0
                text: Engine.rxAntenna
                color: info.rxColor
                fontPx: info.fontPx
                rowH: info.rowH
                onTapped: rxChooser.open()
            }
            Hit {
                visible: Engine.txAntenna.length > 0 && Engine.txAntennas.length > 0
                text: Engine.txAntenna
                color: Theme.transmit
                fontPx: info.fontPx
                rowH: info.rowH
                onTapped: txChooser.open()
            }
            Hit {
                visible: Engine.filterPresets.length > 0
                text: info.widthLabel(Engine.filterWidth)
                color: info.rxColor
                fontPx: info.fontPx
                rowH: info.rowH
                onTapped: filterChooser.open()
            }
            // Speaker: tap to mute.
            Item {
                implicitWidth: Math.max(info.rowH * 0.9, speaker.width + 12)
                implicitHeight: info.rowH
                Canvas {
                    id: speaker
                    anchors.centerIn: parent
                    width: Math.round(info.fontPx * 1.4)
                    height: Math.round(info.fontPx * 1.2)
                    property bool muted: Engine.muted
                    onMutedChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const w = width, h = height
                        ctx.fillStyle = muted ? Theme.transmit : info.rxColor
                        ctx.strokeStyle = ctx.fillStyle
                        ctx.beginPath()
                        ctx.moveTo(0, h * 0.35); ctx.lineTo(w * 0.22, h * 0.35); ctx.lineTo(w * 0.5, h * 0.08)
                        ctx.lineTo(w * 0.5, h * 0.92); ctx.lineTo(w * 0.22, h * 0.65); ctx.lineTo(0, h * 0.65)
                        ctx.closePath(); ctx.fill()
                        ctx.lineWidth = Math.max(1.5, h * 0.1)
                        if (muted) {
                            ctx.beginPath(); ctx.moveTo(w * 0.62, h * 0.3); ctx.lineTo(w * 0.95, h * 0.7); ctx.stroke()
                            ctx.beginPath(); ctx.moveTo(w * 0.95, h * 0.3); ctx.lineTo(w * 0.62, h * 0.7); ctx.stroke()
                        } else {
                            ctx.beginPath(); ctx.arc(w * 0.5, h / 2, w * 0.25, -0.8, 0.8); ctx.stroke()
                            ctx.beginPath(); ctx.arc(w * 0.5, h / 2, w * 0.45, -0.8, 0.8); ctx.stroke()
                        }
                    }
                }
                MouseArea { anchors.fill: parent; onClicked: Engine.muted = !Engine.muted }
            }
            // Tuning lock: tap to lock; flashes when a tune is refused.
            Item {
                implicitWidth: Math.max(info.rowH * 0.9, lockIcon.width + 12)
                implicitHeight: info.rowH
                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: Theme.warning
                    opacity: 0
                    NumberAnimation on opacity { id: lockFlashAnim; from: 0.8; to: 0; duration: 500; running: false }
                }
                Canvas {
                    id: lockIcon
                    anchors.centerIn: parent
                    width: Math.round(info.fontPx * 1.0)
                    height: Math.round(info.fontPx * 1.25)
                    property bool locked: Engine.locked
                    onLockedChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const w = width, h = height
                        const col = locked ? Theme.warning : info.rxColor
                        ctx.strokeStyle = col
                        ctx.fillStyle = col
                        const lw = Math.max(1.5, w * 0.13)
                        ctx.lineWidth = lw
                        const bodyTop = h * 0.48
                        if (locked) {
                            ctx.fillRect(0, bodyTop, w, h - bodyTop)
                        } else {
                            ctx.strokeRect(lw / 2, bodyTop, w - lw, h - bodyTop - lw / 2)
                        }
                        // Shackle: closed over the body, or swung open.
                        const r = w * 0.3
                        const cx = locked ? w / 2 : w * 0.72
                        ctx.beginPath()
                        ctx.moveTo(cx - r, locked ? bodyTop : h * 0.36)
                        ctx.lineTo(cx - r, h * 0.3)
                        ctx.arc(cx, h * 0.3, r, Math.PI, 0, false)
                        ctx.lineTo(cx + r, bodyTop)
                        ctx.stroke()
                    }
                }
                MouseArea { anchors.fill: parent; onClicked: Engine.locked = !Engine.locked }
                Connections {
                    target: Engine
                    function onTuneBlocked() { lockFlashAnim.restart() }
                }
            }
        }

        // DSP flags, AGC.
        Row {
            spacing: 0
            // DSP flags: tap for the filter/DSP chooser.
            Item {
                implicitWidth: flagRow.implicitWidth + 12
                implicitHeight: info.rowH
                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: "#ffffff"
                    opacity: flagArea.pressed ? 0.15 : 0
                }
                Row {
                    id: flagRow
                    anchors.centerIn: parent
                    spacing: info.gap
                    Repeater {
                        model: info.flags
                        delegate: Text {
                            required property var modelData
                            text: modelData.text
                            font.pixelSize: info.fontPx
                            font.bold: modelData.on
                            color: modelData.on ? Theme.text : info.dim
                        }
                    }
                }
                MouseArea { id: flagArea; anchors.fill: parent; onClicked: filterChooser.open() }
            }
            Hit {
                text: info.agcText
                color: info.rxColor
                fontPx: info.fontPx
                rowH: info.rowH
                onTapped: agcChooser.open()
            }
        }
    }

    // ---- choosers ----
    FilterChooser {
        id: filterChooser
        y: info.height + 4
    }
    ChooserPopup {
        id: rxChooser
        objectName: "rxAntennaChooser"
        title: qsTr("RX antenna")
        options: Engine.rxAntennas
        current: Engine.rxAntenna
        y: info.height + 4
        onPicked: (name) => Engine.rxAntenna = name
    }
    ChooserPopup {
        id: txChooser
        objectName: "txAntennaChooser"
        title: qsTr("TX antenna")
        options: Engine.txAntennas
        current: Engine.txAntenna
        y: info.height + 4
        onPicked: (name) => Engine.txAntenna = name
    }
    ChooserPopup {
        id: agcChooser
        objectName: "agcChooser"
        title: qsTr("AGC")
        options: Engine.agcModes
        current: Engine.agcMode
        y: info.height + 4
        onPicked: (name) => Engine.agcMode = name
    }
}
