import QtQuick
import QtQuick.Controls

// The app's button: the Basic style's colours with rounded corners. One
// size everywhere (the band buttons'); only PTT and KEY are larger.
Button {
    id: control
    // Never takes the keyboard focus: a tapped button would otherwise catch
    // the Magic Keyboard's Space (pressing itself instead of PTT) and arrows.
    focusPolicy: Qt.NoFocus
    font.pixelSize: Theme.buttonFont
    leftPadding: 4
    rightPadding: 4

    contentItem: Text {
        text: control.text
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        color: control.checked ? "#ffffff" : "#1a1a1a"
        opacity: control.enabled ? 1.0 : 0.4
    }

    background: Rectangle {
        implicitWidth: Theme.buttonWidth
        implicitHeight: Theme.touchTarget
        radius: Theme.radius
        color: control.checked ? (control.down ? "#4a4b4c" : "#353637")
             : control.down ? "#bdbdbd" : "#e0e0e0"
        opacity: control.enabled ? 1.0 : 0.5
    }
}
