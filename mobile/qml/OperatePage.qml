import QtQuick
import QtQuick.Controls
import AetherMobile
import QtQuick.Layouts

Page {
    id: page
    focus: true

    // Hardware keyboard (Magic Keyboard): arrows tune by step (Shift x10),
    // +/- zoom, F opens direct entry, F1-F12 send the CW memories, Esc stops
    // CW text. Space is hold-to-talk: press keys,
    // release unkeys, auto-repeat is ignored, and losing focus unkeys.
    property bool spacePtt: false
    Keys.onPressed: (event) => {
        if (event.isAutoRepeat && event.key === Qt.Key_Space) {
            event.accepted = true
            return
        }
        const mult = (event.modifiers & Qt.ShiftModifier) ? 10 : 1
        switch (event.key) {
        case Qt.Key_Left: Engine.tuneBySteps(-mult); break
        case Qt.Key_Right: Engine.tuneBySteps(mult); break
        case Qt.Key_Down: Engine.tuneBySteps(-10 * mult); break
        case Qt.Key_Up: Engine.tuneBySteps(10 * mult); break
        case Qt.Key_Plus:
        case Qt.Key_Equal: Engine.zoom(0.5); break
        case Qt.Key_Minus: Engine.zoom(2.0); break
        case Qt.Key_F: freqDialog.open(); break
        // CW memories M1-M12 and abort, as the desktop's CWX F-keys and Esc.
        case Qt.Key_F1: case Qt.Key_F2: case Qt.Key_F3: case Qt.Key_F4:
        case Qt.Key_F5: case Qt.Key_F6: case Qt.Key_F7: case Qt.Key_F8:
        case Qt.Key_F9: case Qt.Key_F10: case Qt.Key_F11: case Qt.Key_F12:
            Engine.sendCwMemory(event.key - Qt.Key_F1)
            break
        case Qt.Key_Escape: Engine.stopCwx(); break
        case Qt.Key_Space:
            page.spacePtt = true
            Engine.setPtt(true)
            break
        default: return
        }
        event.accepted = true
    }
    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            page.spacePtt = false
            Engine.setPtt(false, "space released")
            event.accepted = true
        }
    }
    onActiveFocusChanged: {
        if (!activeFocus && spacePtt) {
            spacePtt = false
            Engine.setPtt(false, "page lost keyboard focus")
        }
    }
    // Keyboard focus returns to the page whenever a control (a slider, a
    // switch, a popup that closed) took it, so Space and the arrows keep
    // working; text entry and the digit dialogs keep it.
    function keepsKeyboard(item) {
        return item !== null && (item.cursorPosition !== undefined || item.keepsKeyboard === true)
    }
    Connections {
        target: page.Window.window
        function onActiveFocusItemChanged() {
            const item = page.Window.window ? page.Window.window.activeFocusItem : null
            if (!page.visible || page.activeFocus || page.keepsKeyboard(item)) {
                return
            }
            Qt.callLater(() => {
                const now = page.Window.window ? page.Window.window.activeFocusItem : null
                if (page.visible && !page.activeFocus && !page.keepsKeyboard(now)) {
                    page.forceActiveFocus()
                }
            })
        }
    }
    background: Rectangle { color: Theme.background }

    // Layout adapts from the 12.9" iPad Pro (1366x1024 pt) down to 1024x768
    // and portrait: smaller type below 1200 pt, controls stacked under the
    // spectrum in portrait, and a scrolling control column so nothing is cut.
    readonly property bool compact: width < 1200
    // The info panel goes under the frequency only where the frequency row
    // has no room for it (narrow Split View).
    readonly property bool infoUnder: width < 700
    // PTT height, shared with the drawer's copy of the button.
    // PTT runs the full width along the bottom in every orientation.
    readonly property real pttHeight: Math.round(Theme.touchTarget * 1.5)
    readonly property bool portrait: height > width
    // Landscape with the panel pinned: two columns, the panel the full
    // height at the right, PTT under the display only. Otherwise one column
    // and PTT the full width.
    readonly property bool twoColumns: !portrait && Engine.controlsVisible
    // Landscape side column: the pinned control panel (none when hidden).
    // Fixed, so content can never widen it off screen.
    readonly property int sideWidth: Math.max(280, Math.min(360, width * 0.28))

    function formatMhz(mhz) {
        // 14.074.500 style: MHz . kHz . Hz
        const hz = Math.round(mhz * 1e6)
        const mhzPart = Math.floor(hz / 1e6)
        const khzPart = Math.floor((hz % 1e6) / 1e3)
        const hzPart = hz % 1e3
        return mhzPart + "." + String(khzPart).padStart(3, "0") + "." + String(hzPart).padStart(3, "0")
    }

    header: ToolBar {
        background: Rectangle { color: Theme.surface }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacing
            anchors.rightMargin: Theme.spacing
            Label {
                text: Engine.radioName
                color: Theme.textDim
                font.pixelSize: 18
                Layout.fillWidth: true
            }
            // Network quality; tap for the details.
            NetworkBadge {
                objectName: "networkBadge"
                Layout.rightMargin: Theme.spacing
                TapHandler { onTapped: networkDialog.open() }
            }
            // App version and build, so a screenshot or report says which.
            Label {
                text: "v" + Engine.appVersion
                color: Theme.textDim
                font.pixelSize: 13
            }
            ToolButton {
                // Pinned panel: hide it. Hidden: open it as a drawer over the
                // waterfall.
                text: Engine.controlsVisible ? qsTr("Hide controls") : qsTr("☰ Controls")
                focusPolicy: Qt.NoFocus
                onClicked: {
                    if (Engine.controlsVisible) {
                        Engine.controlsVisible = false
                    } else {
                        controlsDrawer.open()
                    }
                }
            }
            ToolButton {
                text: qsTr("Setup")
                focusPolicy: Qt.NoFocus
                onClicked: radioSetup.open()
            }
            ToolButton {
                text: qsTr("Manual")
                focusPolicy: Qt.NoFocus
                onClicked: Qt.openUrlExternally(Engine.manualUrl)
            }
            ToolButton {
                text: qsTr("Log")
                focusPolicy: Qt.NoFocus
                onClicked: logPage.open()
            }
            ToolButton {
                text: qsTr("Disconnect")
                onClicked: Engine.disconnectRadio()
            }
        }
    }

    GridLayout {
        id: grid
        anchors.fill: parent
        // Stay clear of the home indicator and rounded corners: iOS defers or
        // cancels touches at the screen edges, which would end a held PTT.
        anchors.leftMargin: Theme.spacing + grid.SafeArea.margins.left
        anchors.rightMargin: Theme.spacing + grid.SafeArea.margins.right
        anchors.topMargin: Theme.spacing
        anchors.bottomMargin: Theme.spacing + Math.max(grid.SafeArea.margins.bottom, 12)
        columnSpacing: Theme.spacing
        rowSpacing: Theme.spacing
        columns: page.twoColumns ? 2 : 1

        // Left (top in portrait): frequency, S-meter and the panadapter area.
        // Cells are placed by hand: a hidden panel must not let PTT slide
        // into its cell.
        ColumnLayout {
            Layout.row: 0
            Layout.column: 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 320
            spacing: page.compact ? Theme.spacing / 2 : Theme.spacing

            RowLayout {
                spacing: page.compact ? Theme.spacing : 2 * Theme.spacing
                Label {
                    text: page.formatMhz(Engine.frequencyMhz)
                    color: Theme.text
                    font.pixelSize: page.compact ? 40 : 56
                    font.family: "monospace"
                    // Tap for direct entry.
                    TapHandler { onTapped: freqDialog.open() }
                }
                ModeButton {
                    fontSize: page.compact ? 24 : 32
                }
                // The slice at a glance and in reach: antennas, width,
                // speaker, lock, DSP, AGC (each opens its choices).
                InfoPanel {
                    visible: !page.infoUnder
                    Layout.leftMargin: Theme.spacing
                }
                Item { Layout.fillWidth: true }
            }
            InfoPanel {
                visible: page.infoUnder
                singleLine: true
                Layout.fillWidth: true
            }

            // Band stack (Flex): one row, buttons share the width. Each band
            // comes back to its last frequency and mode.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing / 2
                visible: Engine.bandsAvailable
                Repeater {
                    model: Engine.bands
                    delegate: AppButton {
                        required property string modelData
                        text: modelData
                        leftPadding: 2
                        rightPadding: 2
                        Layout.fillWidth: true
                        Layout.minimumWidth: 40
                        Layout.preferredWidth: 60
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: Engine.selectBand(modelData)
                    }
                }
            }

            SliceBar {
                id: sliceBar
                Layout.fillWidth: true
                compact: page.compact
            }

            PanadapterView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                rfLabelWidth: sliceBar.rightMeterWidth
                // Close under the slice row: the meters at its top corners
                // sit right below the buttons.
                Layout.topMargin: -Math.round(Theme.spacing / 2)
            }

            // Step buttons: land on round frequencies (tuneByHz).
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing / 2
                Repeater {
                    model: [-1000, -100, -10, -1, 1, 10, 100, 1000]
                    delegate: AppButton {
                        required property int modelData
                        text: (modelData > 0 ? "+" : "−") + (Math.abs(modelData) >= 1000
                              ? Math.abs(modelData) / 1000 + "k" : Math.abs(modelData))
                        Layout.fillWidth: true
                        Layout.minimumWidth: 40
                        Layout.preferredWidth: Theme.buttonWidth
                        Layout.preferredHeight: Theme.touchTarget
                        focusPolicy: Qt.NoFocus
                        onClicked: Engine.tuneByHz(modelData)
                    }
                }
            }
        }

        // Right (bottom in portrait): the pinned controls, the full height
        // of the side; they scroll when they do not fit.
        ColumnLayout {
            visible: Engine.controlsVisible
            Layout.row: page.twoColumns ? 0 : 1
            Layout.column: page.twoColumns ? 1 : 0
            Layout.rowSpan: page.twoColumns ? 2 : 1
            Layout.preferredWidth: page.portrait ? -1 : page.sideWidth
            Layout.maximumWidth: page.portrait ? Number.POSITIVE_INFINITY : page.sideWidth
            Layout.fillWidth: page.portrait
            Layout.fillHeight: !page.portrait
            Layout.preferredHeight: page.portrait ? page.height * 0.42 : -1
            spacing: Theme.spacing

            // Pinned panel: section toggles; the chosen sections stack below.
            ControlsPanel {
                multiSelect: true
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }

        // PTT along the bottom: under the display (the pinned panel keeps
        // the side's full height), the full width otherwise.
        PttButton {
            id: ptt
            Layout.row: page.twoColumns ? 1 : (Engine.controlsVisible ? 2 : 1)
            Layout.column: 0
            Layout.fillWidth: true
            Layout.preferredHeight: page.pttHeight
            fontPx: 32
        }
    }

    // With the panel hidden, controls slide in from the right over the
    // waterfall (no dimming; tap outside or swipe back to close). Edge swipes
    // do not open it, so a PTT press near the edge never becomes a drag.
    Drawer {
        id: controlsDrawer
        objectName: "controlsDrawer"
        edge: page.portrait ? Qt.BottomEdge : Qt.RightEdge
        width: page.portrait ? page.width : Math.min(page.width * 0.4, 460)
        // Landscape: ends above the page's PTT row, which stays usable, so
        // the drawer needs no PTT of its own. Portrait (from the bottom)
        // covers it and carries a copy.
        height: page.portrait ? Math.min(page.height * 0.5, 560)
                              : page.height - page.pttHeight - 2 * Theme.spacing
                                - Math.max(grid.SafeArea.margins.bottom, 12)
        dim: false
        dragMargin: 0
        onAboutToShow: {
            Engine.setPtt(false, "controls drawer opened")
            Engine.setCwKey(false, "controls drawer opened")
        }
        background: Rectangle {
            color: Theme.surface
            opacity: 0.96
            radius: Theme.radius
        }

        // The controls scroll above the drawer's own PTT/KEY, which sits where
        // the page's button is: the drawer never covers it.
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacing
            anchors.bottomMargin: Theme.spacing + controlsDrawer.SafeArea.margins.bottom
            anchors.rightMargin: Theme.spacing + controlsDrawer.SafeArea.margins.right
            spacing: Theme.spacing
            ControlsPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                showPinButton: true
                onPinRequested: {
                    controlsDrawer.close()
                    Engine.controlsVisible = true
                }
            }
            PttButton {
                visible: page.portrait
                Layout.fillWidth: true
                Layout.preferredHeight: page.pttHeight
                fontPx: 32
            }
        }
    }

    LogPage { id: logPage }
    NetworkDialog { id: networkDialog }
    RadioSetupPage { id: radioSetup }
    SetupSuggestion { onSetupRequested: radioSetup.open() }

    FrequencyDialog {
        id: freqDialog
        onClosed: page.forceActiveFocus()
    }
    Component.onCompleted: page.forceActiveFocus()
    // A layout switch (rotation, Split View, fold/unfold) destroys this page;
    // a held PTT must not outlive its button.
    Component.onDestruction: Engine.stopTransmit("layout changed")
}
