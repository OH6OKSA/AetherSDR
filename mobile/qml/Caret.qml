import QtQuick

// A solid down-pointing triangle: "this opens a chooser". Drawn, not a
// font glyph, so it is the same clear size on every device.
Canvas {
    id: caret
    property color color: "#1a1a1a"
    property real size: 12
    implicitWidth: size
    implicitHeight: Math.round(size * 0.62)
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.fillStyle = caret.color
        ctx.beginPath()
        ctx.moveTo(0, 0)
        ctx.lineTo(width, 0)
        ctx.lineTo(width / 2, height)
        ctx.closePath()
        ctx.fill()
    }
}
