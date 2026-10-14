import QtQuick
import AetherMobile

// A button that keeps stepping while held, faster the longer it is held:
// stepped(n) on the press (n = 0) and then on every repeat (n = 1, 2, ...).
// The host turns n into a step size (e.g. 10 Hz, then 50, then 100).
AppButton {
    id: button
    signal stepped(int n)
    property int count: 0
    focusPolicy: Qt.NoFocus
    onPressed: {
        count = 0
        stepped(0)
        repeat.interval = 400
        repeat.start()
    }
    onReleased: repeat.stop()
    onCanceled: repeat.stop()
    Timer {
        id: repeat
        repeat: true
        onTriggered: {
            button.count += 1
            // Start at 8 steps a second, reach 20 a second after ~1.5 s.
            interval = button.count < 12 ? 120 : 50
            button.stepped(button.count)
        }
    }
}
