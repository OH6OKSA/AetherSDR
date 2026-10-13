import QtQuick
import QtQuick.Controls
import AetherMobile

// Tuning lock next to the frequency. Flashes when a tune is refused.
AppButton {
    id: lock
    text: Engine.locked ? "🔒" : "🔓"
    font.pixelSize: 20
    implicitWidth: Theme.touchTarget
    checked: Engine.locked
    focusPolicy: Qt.NoFocus
    onClicked: Engine.locked = !Engine.locked

    Rectangle {
        id: flash
        anchors.fill: parent
        radius: Theme.radius
        color: Theme.warning
        opacity: 0
        NumberAnimation on opacity { id: flashAnim; from: 0.8; to: 0; duration: 500; running: false }
    }
    Connections {
        target: Engine
        function onTuneBlocked() { flashAnim.restart() }
    }
}
