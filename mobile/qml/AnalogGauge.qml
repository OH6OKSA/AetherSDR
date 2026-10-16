import QtQuick
import AetherMobile

// Analog meter face (SmartSDR/AetherSDR style): an arc scale with ticks and
// labels, a needle and the reading. S-meter: S1…S9 then +20/+40 in red;
// other meters: five labels from min to max, red from the alarm level.
Item {
    id: gauge
    property string kind: "S"
    property real value: 0
    property real min: 0
    property real max: 1
    property real red: 1e9
    property bool live: false
    property string text: ""
    property bool compact: false
    // Peak hold value (drawn as a thin amber needle when above the reading).
    property real peak: min
    // Size unit, from the height: text and strokes follow it. The host can
    // size the width to fitWidth; a narrower face shrinks the unit too.
    readonly property real unit: Math.min(height / 95, width / 140)
    // Room for the inner dBm scale and the title only on large faces.
    readonly property bool roomy: width >= 260 && height >= 130

    // Geometry shared by the canvas and fitWidth (pivot at the bottom
    // middle, the arc spanning 132 degrees above it).
    readonly property real fontPx: Math.max(7, Math.round(8.5 * unit))
    readonly property real headerFont: Math.max(10, Math.round(12 * unit))
    readonly property real header: Math.round(headerFont * 1.25)
    // The scale labels sit in a band just under the header.
    readonly property real arcTop: header + fontPx * 1.5 + 3
    readonly property real halfAngle: 66 * Math.PI / 180
    // Width that holds the arc and its labels at this height, and the
    // header (name + a value like "-102 dBm"): no empty sides.
    readonly property real fitWidth: {
        const u = height / 95
        const f = Math.max(7, Math.round(8.5 * u))
        const hf = Math.max(10, Math.round(12 * u))
        const r = Math.max(10, height - 3 - (Math.round(hf * 1.25) + f * 1.5 + 3))
        const arc = 2 * ((r + 4 + f * 0.8) * Math.sin(halfAngle) + f * 1.0)
        return Math.ceil(Math.max(arc, hf * 8.5))
    }

    // Pivot and radius, shared by the scale canvas and the needle.
    readonly property real pivotX: width / 2
    readonly property real pivotY: height - 3
    readonly property real radius: Math.max(10, Math.min(pivotY - arcTop,
        (width / 2 - fontPx * 1.0) / Math.sin(halfAngle) - 4 - fontPx * 0.8))
    function angleOf(v) {
        const span = max - min
        return -Math.PI / 2 - halfAngle
               + 2 * halfAngle * Math.max(0, Math.min(1, (v - min) / (span || 1)))
    }

    // Eased needle, so meter jitter reads like a real movement. Only the
    // needle item turns with the reading: the scale canvas is painted when
    // the size or meter changes, not on every reading (a JS repaint per
    // meter update on the UI thread made receive audio stutter).
    property real shown: live ? Math.max(min, Math.min(max, value)) : min
    Behavior on shown { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }
    onWidthChanged: face.requestPaint()
    onHeightChanged: face.requestPaint()
    onKindChanged: face.requestPaint()
    onLiveChanged: face.requestPaint()
    onMinChanged: face.requestPaint()
    onMaxChanged: face.requestPaint()
    onRedChanged: face.requestPaint()

    // Scale marks: [value, label, red?]
    readonly property var marks: {
        if (kind === "S") {
            // S1 = -121 dBm, 6 dB per S unit; S9 = -73; +20 = -53; +40 = -33;
            // +60 = -13, the scale's end, so the labels reach both ends.
            return [[-121, "1", false], [-109, "3", false], [-97, "5", false], [-85, "7", false],
                    [-73, "9", false], [-53, "+20", true], [-33, "+40", true], [-13, "+60", true]]
        }
        const out = []
        for (let i = 0; i <= 4; ++i) {
            const v = min + (max - min) * i / 4
            const label = Math.abs(max - min) <= 10 ? v.toFixed(1) : Math.round(v).toString()
            out.push([v, label, v >= red])
        }
        return out
    }
    readonly property real redFrom: kind === "S" ? -73 : red

    readonly property string title: kind === "S" ? qsTr("S-Meter") : kind
    // Inner scale under the arc (the desktop meter's second scale): dBm on
    // the S-meter, none on the others.
    readonly property var innerMarks: kind === "S"
        ? [[-120, "-120", false], [-100, "-100", false], [-80, "-80", false], [-60, "-60", true], [-40, "-40", true]]
        : []

    Canvas {
        id: face
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative
        onPaint: {
            const ctx = getContext("2d")
            const w = width, h = height
            ctx.reset()
            const fontPx = gauge.fontPx
            // A wide, shallow arc whose centre sits at the bottom middle, as
            // on the AetherSDR meter: the needle hub is a dark half disc there.
            // The needle's pivot at the bottom middle, as on the AetherSDR
            // meter; the arc spans 132 degrees above it.
            const half = gauge.halfAngle
            // Clear of the header text: the top scale labels sit above the arc.
            const top = gauge.arcTop
            const cx = gauge.pivotX
            const cy = gauge.pivotY
            const radius = gauge.radius
            const start = -Math.PI / 2 - half
            const end = -Math.PI / 2 + half
            const span = gauge.max - gauge.min
            const angleOf = v => start + (end - start) * Math.max(0, Math.min(1, (v - gauge.min) / (span || 1)))
            const redAngle = angleOf(gauge.redFrom)

            // Outer scale: thin white line, red past the alarm point.
            ctx.lineWidth = 2
            ctx.strokeStyle = "#e6edf3"
            ctx.beginPath(); ctx.arc(cx, cy, radius, start, Math.min(redAngle, end), false); ctx.stroke()
            if (redAngle < end) {
                ctx.strokeStyle = "#e5484d"
                ctx.beginPath(); ctx.arc(cx, cy, radius, redAngle, end, false); ctx.stroke()
            }
            // Inner band: thick blue, red past the alarm point.
            const inner = radius - 5 * gauge.unit
            ctx.lineWidth = Math.max(2, 3 * gauge.unit)
            ctx.strokeStyle = "#2f7fd6"
            ctx.beginPath(); ctx.arc(cx, cy, inner, start, Math.min(redAngle, end), false); ctx.stroke()
            if (redAngle < end) {
                ctx.strokeStyle = "#c93a3f"
                ctx.beginPath(); ctx.arc(cx, cy, inner, redAngle, end, false); ctx.stroke()
            }

            ctx.textAlign = "center"
            ctx.textBaseline = "middle"
            // Outer ticks and labels above the arc.
            ctx.font = "bold " + fontPx + "px sans-serif"
            for (const m of gauge.marks) {
                const a = angleOf(m[0]), c = Math.cos(a), s = Math.sin(a)
                const col = m[2] ? "#ff5a5f" : "#e6edf3"
                ctx.strokeStyle = col
                ctx.lineWidth = 1.5
                ctx.beginPath()
                ctx.moveTo(cx + c * radius, cy + s * radius)
                ctx.lineTo(cx + c * (radius + 4), cy + s * (radius + 4))
                ctx.stroke()
                ctx.fillStyle = col
                ctx.fillText(m[1], cx + c * (radius + 4 + fontPx * 0.8), cy + s * (radius + 4 + fontPx * 0.8))
            }
            // Inner labels below the band.
            ctx.font = fontPx + "px sans-serif"
            for (const m of (gauge.roomy ? gauge.innerMarks : [])) {
                const a = angleOf(m[0]), c = Math.cos(a), s = Math.sin(a)
                ctx.fillStyle = m[2] ? "#ff5a5f" : "#c8d3de"
                ctx.fillText(m[1], cx + c * (inner - fontPx * 1.3), cy + s * (inner - fontPx * 1.3))
            }

            // Hub glow (under the needle item); the dark cap is drawn over it.
            const hub = Math.max(4, radius * 0.13)
            const glow = ctx.createRadialGradient(cx, cy, hub * 0.6, cx, cy, hub * 2.2)
            glow.addColorStop(0, "rgba(255,170,90,0.35)")
            glow.addColorStop(1, "rgba(255,170,90,0)")
            ctx.fillStyle = glow
            ctx.beginPath(); ctx.arc(cx, cy, hub * 2.2, Math.PI, 2 * Math.PI, false); ctx.fill()
        }
    }

    // The needle: a thin bar turned about the pivot (no repaint).
    Rectangle {
        id: needle
        x: gauge.pivotX
        y: gauge.pivotY - height / 2
        width: gauge.radius + 3
        height: Math.max(1.5, 1.6 * gauge.unit)
        antialiasing: true
        color: gauge.live ? "#ffffff" : "#8b98a5"
        transformOrigin: Item.Left
        rotation: gauge.angleOf(gauge.shown) * 180 / Math.PI
    }
    // Peak hold: a thin amber needle where the highest recent reading was.
    Rectangle {
        visible: gauge.live && gauge.peak > gauge.shown + (gauge.max - gauge.min) * 0.01
        x: gauge.pivotX
        y: gauge.pivotY - height / 2
        width: gauge.radius + 3
        height: Math.max(1, 1.0 * gauge.unit)
        antialiasing: true
        color: "#f0b429"
        opacity: 0.8
        transformOrigin: Item.Left
        rotation: gauge.angleOf(Math.max(gauge.min, Math.min(gauge.max, gauge.peak))) * 180 / Math.PI
    }
    // The needle's balance cap: a small dark half disc over the pivot.
    Item {
        readonly property real hub: Math.max(4, gauge.radius * 0.13)
        x: gauge.pivotX - hub
        y: gauge.pivotY - hub
        width: 2 * hub
        height: hub
        clip: true
        Rectangle {
            width: parent.width
            height: parent.width
            radius: width / 2
            color: "#05070a"
        }
    }

    // Header: the S unit or the meter's name (left, blue), title (middle,
    // large faces), the value alone (right): the reading's text starts with
    // the name ("PA 16 °C", "Mic -20 dB"), which repeated it.
    readonly property string shortName: kind === "PA temp" ? qsTr("PA Temp") : kind
    readonly property var parts: text.trim().split(/\s+/)
    readonly property string valueText: {
        if (!live) return "–"
        if (kind === "S") return parts.slice(1).join(" ")
        return /^[A-Za-z]+$/.test(parts[0]) ? parts.slice(1).join(" ") : parts.join(" ")
    }
    Text {
        id: nameText
        x: 2; y: 0
        text: gauge.kind === "S" && gauge.live ? gauge.parts[0] : gauge.shortName
        color: "#3fb6ff"
        font.pixelSize: gauge.headerFont
        font.bold: true
    }
    Text {
        visible: gauge.roomy
        anchors.horizontalCenter: parent.horizontalCenter
        y: 2
        text: gauge.title
        color: Theme.textDim
        font.pixelSize: Math.max(8, Math.round(9 * gauge.unit))
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: 12
        y: 0
        // Never over the name: at most the space left of it.
        width: Math.min(implicitWidth, gauge.width - nameText.implicitWidth - 20)
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideLeft
        text: gauge.valueText
        color: gauge.live && gauge.value >= gauge.red ? Theme.transmit : Theme.text
        font.pixelSize: gauge.headerFont
        font.bold: true
    }
}
