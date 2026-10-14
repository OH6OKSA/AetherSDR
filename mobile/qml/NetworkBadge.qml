import QtQuick
import AetherMobile

// Network quality at a glance: a coloured dot, the rating and the round
// trip ("● Excellent 4 ms"), in the desktop's colours. compact drops the
// word (phones).
Row {
    id: badge
    property string quality: Engine.networkQuality
    property int rttMs: Engine.networkRttMs
    property bool compact: false
    signal tapped()
    spacing: 5
    readonly property color dotColor: quality === "Excellent" || quality === "Very Good" ? "#00cc66"
                                    : quality === "Good" ? "#00b4d8"
                                    : quality === "Fair" ? "#cc9900"
                                    : quality === "Poor" ? "#cc3333" : "#8aa8c0"
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: 10; height: 10; radius: 5
        color: badge.dotColor
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        visible: !badge.compact
        text: badge.quality
        color: badge.dotColor
        font.pixelSize: 13
        font.bold: true
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        visible: badge.rttMs > 0
        text: badge.rttMs + " ms"
        color: "#8aa8c0"
        font.pixelSize: 12
    }
}
