import QtQuick
import AetherMobile

// One selectable meter on the slice row (tap to choose what it shows).
// The host gives the dial height and the most width it may take; an analog
// face is as wide as its dial and labels need (lower where the width is
// short, keeping its shape), a bar face as wide as allowed.
Rectangle {
    id: badge
    property alias side: meterView.side
    property real dialHeight: 66
    property real maxWidth: 240
    // An analog face keeps its proportions (about 1.6 : 1, as on a tablet):
    // where the width runs out it gets lower, not squarer.
    readonly property real faceHeight: Math.min(dialHeight, (maxWidth - 8) / 1.6)
    implicitWidth: meterView.analog ? Math.min(maxWidth, meterView.analogFitWidth + 8)
                            : Math.min(maxWidth, Math.max(110, dialHeight * 1.6))
    implicitHeight: meterView.implicitHeight + 8
    radius: Theme.radius
    color: Theme.surface
    MeterView {
        id: meterView
        compact: true
        analogAllowed: badge.faceHeight >= 46
        faceHeight: badge.faceHeight
        anchors.fill: parent
        anchors.margins: 4
    }
}
