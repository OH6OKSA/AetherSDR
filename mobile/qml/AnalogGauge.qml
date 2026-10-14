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
    // Size unit: 1.0 at a 66 px face; text and strokes follow it.
    readonly property real unit: height / 66
    // Room for the inner dBm scale and the title only on larger faces.
    readonly property bool roomy: unit >= 1.3

    // Eased needle, so meter jitter reads like a real movement.
    property real shown: live ? Math.max(min, Math.min(max, value)) : min
    Behavior on shown { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }
    onShownChanged: face.requestPaint()
    onWidthChanged: face.requestPaint()
    onHeightChanged: face.requestPaint()
    onKindChanged: face.requestPaint()
    onLiveChanged: face.requestPaint()

    // Scale marks: [value, label, red?]
    readonly property var marks: {
        if (kind === "S") {
            // S1 = -121 dBm, 6 dB per S unit; S9 = -73; +20 = -53; +40 = -33.
            return [[-121, "1", false], [-109, "3", false], [-97, "5", false], [-85, "7", false],
                    [-73, "9", false], [-53, "+20", true], [-33, "+40", true]]
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
            const fontPx = Math.max(7, Math.round(8.5 * gauge.unit))
            const header = Math.round(15 * gauge.unit)
            // A wide, shallow arc whose centre sits at the bottom middle, as
            // on the AetherSDR meter: the needle hub is a dark half disc there.
            const half = 50 * Math.PI / 180
            // Radius from the width; the centre sits below the face so the
            // arc is wide and shallow, with the hub's top showing at the bottom.
            const top = header + fontPx + 4
            const radius = Math.max(10, (w / 2 - fontPx * 2.2) / Math.sin(half))
            const cx = w / 2
            const cy = top + radius
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

            // Needle.
            const na = angleOf(gauge.shown)
            ctx.strokeStyle = gauge.live ? "#ffffff" : "#8b98a5"
            ctx.lineWidth = Math.max(1.5, 1.6 * gauge.unit)
            ctx.beginPath()
            ctx.moveTo(cx, cy)
            ctx.lineTo(cx + Math.cos(na) * (radius + 3), cy + Math.sin(na) * (radius + 3))
            ctx.stroke()

            // Hub: a dark half disc with a warm glow, as on the desktop.
            const hub = Math.max(10, cy - h + 9 * gauge.unit)
            const glow = ctx.createRadialGradient(cx, cy, hub * 0.6, cx, cy, hub * 2.2)
            glow.addColorStop(0, "rgba(255,170,90,0.35)")
            glow.addColorStop(1, "rgba(255,170,90,0)")
            ctx.fillStyle = glow
            ctx.beginPath(); ctx.arc(cx, cy, hub * 2.2, Math.PI, 2 * Math.PI, false); ctx.fill()
            ctx.fillStyle = "#05070a"
            ctx.beginPath(); ctx.arc(cx, cy, hub, Math.PI, 2 * Math.PI, false); ctx.fill()
        }
    }

    // Header: reading's S unit or kind (left, blue), title (middle), value (right).
    Text {
        x: 2; y: 0
        text: gauge.kind === "S" && gauge.live ? gauge.text.trim().split(/\s+/)[0] : gauge.kind
        color: "#3fb6ff"
        font.pixelSize: Math.max(10, Math.round(12 * gauge.unit))
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
        text: gauge.live ? (gauge.kind === "S" ? gauge.text.trim().split(/\s+/).slice(1).join(" ") : gauge.text) : "–"
        color: gauge.live && gauge.value >= gauge.red ? Theme.transmit : Theme.text
        font.pixelSize: Math.max(10, Math.round(12 * gauge.unit))
        font.bold: true
    }
}
