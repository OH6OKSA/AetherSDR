pragma Singleton
import QtQuick

// Colours, plus the app's adaptive metrics: everything sized through Theme
// follows the window (rotation, Split View, Stage Manager, folding devices)
// without per-device rules. Main.qml feeds in the window size.
QtObject {
    readonly property color background: "#0f1419"
    readonly property color surface: "#1a222b"
    readonly property color surfaceRaised: "#243040"
    readonly property color text: "#e6edf3"
    readonly property color textDim: "#8b98a5"
    readonly property color accent: "#3fb6ff"
    readonly property color transmit: "#e5484d"
    readonly property color pttReady: "#7a1f22"
    readonly property color warning: "#f5a524"
    readonly property color passband: "#ffd75e"

    // ---- Window (set by Main.qml) ----
    property real windowWidth: 1366
    property real windowHeight: 1024

    // ---- Classes, as Apple's size classes ----
    readonly property real shortSide: Math.min(windowWidth, windowHeight)
    readonly property bool landscape: windowWidth > windowHeight
    // Compact width: an iPhone, a folded phone, a narrow Split View pane.
    readonly property bool compactWidth: windowWidth < 700
    // Compact height: an iPhone (or small pane) in landscape.
    readonly property bool compactHeight: windowHeight < 500
    // Very low: a phone's outer screen in landscape, small iPhones in
    // landscape. Meters fall back to bars to leave room for the display.
    readonly property bool tightHeight: windowHeight < 400
    // The phone layout (drawer controls, strip PTT) for either.
    readonly property bool phoneLayout: compactWidth || compactHeight

    // ---- Continuous metrics from the short side ----
    // 1.0 on an 11" iPad (834 pt); about 0.9 on an iPhone, 1.1 on a 13" iPad.
    readonly property real scale: Math.max(0.85, Math.min(1.15, 0.7 + shortSide / 2800))
    // Never under Apple's 44 pt minimum touch target.
    readonly property int touchTarget: Math.max(44, Math.round(48 * scale))
    // Standard button (the band buttons' size); PTT and KEY are larger.
    readonly property int buttonWidth: Math.round(56 * scale)
    readonly property int buttonFont: Math.max(12, Math.round(14 * scale))
    readonly property int labelFont: Math.max(12, Math.round(15 * scale))
    readonly property int smallFont: Math.max(10, Math.round(12 * scale))
    readonly property int spacing: Math.round(12 * scale)
    readonly property int radius: 8
}
