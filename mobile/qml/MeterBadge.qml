import QtQuick
import AetherMobile

// One selectable meter on the slice row (tap to choose what it shows).
// The host gives the dial height and the most width it may take; an analog
// face is as wide as its dial and labels need, a bar face as wide as allowed.
Rectangle {
    id: badge
    property alias side: meterView.side
    property real dialHeight: 66
    property real maxWidth: 240
    implicitWidth: meterView.analog ? Math.min(maxWidth, meterView.analogFitWidth + 8)
                            : Math.min(maxWidth, Math.max(110, dialHeight * 1.6))
    implicitHeight: meterView.implicitHeight + 8
    radius: Theme.radius
    color: Theme.surface
    MeterView {
        id: meterView
        compact: true
        analogAllowed: badge.dialHeight >= 46
        faceHeight: badge.dialHeight
        anchors.fill: parent
        anchors.margins: 4
    }
}
