import QtQuick

// Three bars, the menu ("☰") symbol drawn rather than taken from a font:
// Android's fonts reachable from Qt lack U+2630 and drew a box.
Item {
    id: icon
    property color color: "#202020"
    implicitWidth: 20
    implicitHeight: 16
    Column {
        anchors.centerIn: parent
        spacing: Math.round(icon.height / 5)
        Repeater {
            model: 3
            Rectangle {
                width: icon.width
                height: Math.max(2, Math.round(icon.height / 7))
                radius: height / 2
                color: icon.color
            }
        }
    }
}
