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

    Canvas {
        id: face
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative
        onPaint: {
            const ctx = getContext("2d")
            const w = width, h = height
            ctx.reset()
            const fontPx = gauge.compact ? 10 : 13
            const top = fontPx + 10
            // Arc of 110° centred on straight up, sized to the face.
            const half = 55 * Math.PI / 180
            // Room at the sides for the end labels (e.g. "0", "120", "+40").
            const radius = Math.max(10, Math.min((w - 2 * (fontPx * 2.4 + 2)) / (2 * Math.sin(half)),
                                                 (h - top - 6) / (1 - Math.cos(half) + 0.25)))
            const cx = w / 2
            const cy = top + radius + 4
            const start = -Math.PI / 2 - half
            const end = -Math.PI / 2 + half
            const span = gauge.max - gauge.min
            const angleOf = v => start + (end - start) * Math.max(0, Math.min(1, (v - gauge.min) / (span || 1)))
            const redAngle = angleOf(gauge.redFrom)

            // Scale band: white/blue below the red zone, red above.
            ctx.lineWidth = gauge.compact ? 3 : 4
            ctx.strokeStyle = "#3fb6ff"
            ctx.beginPath(); ctx.arc(cx, cy, radius, start, Math.min(redAngle, end), false); ctx.stroke()
            if (redAngle < end) {
                ctx.strokeStyle = "#e5484d"
                ctx.beginPath(); ctx.arc(cx, cy, radius, redAngle, end, false); ctx.stroke()
            }

            // Ticks and labels outside the arc.
            ctx.font = fontPx + "px sans-serif"
            ctx.textAlign = "center"
            ctx.textBaseline = "middle"
            for (const m of gauge.marks) {
                const a = angleOf(m[0])
                const c = Math.cos(a), s = Math.sin(a)
                ctx.strokeStyle = m[2] ? "#e5484d" : "#e6edf3"
                ctx.lineWidth = 1.5
                ctx.beginPath()
                ctx.moveTo(cx + c * (radius - 4), cy + s * (radius - 4))
                ctx.lineTo(cx + c * (radius + 4), cy + s * (radius + 4))
                ctx.stroke()
                ctx.fillStyle = m[2] ? "#e5484d" : "#e6edf3"
                ctx.fillText(m[1], cx + c * (radius + fontPx), cy + s * (radius + fontPx))
            }

            // Needle from below the face up to the scale.
            const na = angleOf(gauge.shown)
            ctx.strokeStyle = gauge.live ? "#ffffff" : "#8b98a5"
            ctx.lineWidth = gauge.compact ? 1.5 : 2
            ctx.beginPath()
            ctx.moveTo(cx + Math.cos(na) * radius * 0.25, cy + Math.sin(na) * radius * 0.25)
            ctx.lineTo(cx + Math.cos(na) * (radius + 2), cy + Math.sin(na) * (radius + 2))
            ctx.stroke()
        }
    }

    // Kind (top left) and reading (top right), as on the desktop meter.
    Text {
        x: 2; y: 0
        text: gauge.kind === "S" && gauge.live ? gauge.text.trim().split(/\s+/)[0] : gauge.kind
        color: Theme.accent
        font.pixelSize: gauge.compact ? 12 : 16
        font.bold: true
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: 14
        y: 0
        text: gauge.live ? (gauge.kind === "S" ? gauge.text.trim().split(/\s+/).slice(1).join(" ") : gauge.text) : "–"
        color: gauge.live && gauge.value >= gauge.red ? Theme.transmit : Theme.text
        font.pixelSize: gauge.compact ? 12 : 16
        font.bold: true
    }
}
