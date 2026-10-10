pragma Singleton
import QtQuick

// Placeholder palette for the prototype; the visual canon is the
// maintainer's call (AGENTS.md) and replaces these once the touch UI is
// reviewed.
QtObject {
    readonly property color background: "#0f1419"
    readonly property color surface: "#1a222b"
    readonly property color surfaceRaised: "#243040"
    readonly property color text: "#e6edf3"
    readonly property color textDim: "#8b98a5"
    readonly property color accent: "#3fb6ff"
    readonly property color transmit: "#e5484d"
    readonly property color warning: "#f5a524"

    readonly property int touchTarget: 48
    readonly property int spacing: 12
    readonly property int radius: 8
}
