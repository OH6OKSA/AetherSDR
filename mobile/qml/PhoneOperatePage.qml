import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Operate page for phone-sized windows (iPhone, and a folded iPhone Duo).
// Portrait: frequency, spectrum, a tabbed control area and a full-width PTT.
// Landscape: spectrum on the left, tabs and PTT on the right.
Page {
    id: page
    background: Rectangle { color: Theme.background }

    readonly property bool landscape: width > height
    // PTT height, shared with the drawer's copy of the button. PTT runs
    // along the bottom in both orientations; lower in landscape.
    readonly property real pttHeight: landscape ? Math.round(Theme.touchTarget * 1.2) : 64

    function formatMhz(mhz) {
        const hz = Math.round(mhz * 1e6)
        const mhzPart = Math.floor(hz / 1e6)
        const khzPart = Math.floor((hz % 1e6) / 1e3)
        const hzPart = hz % 1e3
        return mhzPart + "." + String(khzPart).padStart(3, "0") + "." + String(hzPart).padStart(3, "0")
    }

    // A layout switch (rotation, fold/unfold) destroys this page; a held
    // PTT must not outlive its button.
    Component.onDestruction: Engine.stopTransmit("layout changed")

    header: ToolBar {
        background: Rectangle { color: Theme.surface }
        topPadding: header.SafeArea.margins.top
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacing
            anchors.rightMargin: Theme.spacing
            Label {
                text: Engine.radioName
                color: Theme.textDim
                font.pixelSize: 15
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            // Network quality (dot and ms); tap for the details.
            NetworkBadge {
                compact: true
                TapHandler { onTapped: networkDialog.open() }
            }
            // App version and build, so a screenshot or report says which.
            Label {
                text: Engine.appVersion
                color: Theme.textDim
                font.pixelSize: 11
            }
            ToolButton {
                text: qsTr("Controls")
                focusPolicy: Qt.NoFocus
                onClicked: controlsDrawer.open()
            }
            // The rest behind one button: a phone's top row has no room.
            ToolButton {
                id: moreButton
                text: "⋯"
                font.pixelSize: 22
                focusPolicy: Qt.NoFocus
                onClicked: moreMenu.popup(moreButton, 0, moreButton.height)
                Menu {
                    id: moreMenu
                    MenuItem { text: qsTr("Radio Setup"); onTriggered: radioSetup.open() }
                    MenuItem { text: qsTr("Manual"); onTriggered: Qt.openUrlExternally(Engine.manualUrl) }
                    MenuItem { text: qsTr("Log"); onTriggered: logPage.open() }
                    MenuItem { text: qsTr("Disconnect"); onTriggered: Engine.disconnectRadio() }
                }
            }
        }
    }

    GridLayout {
        id: grid
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing / 2 + grid.SafeArea.margins.left
        anchors.rightMargin: Theme.spacing / 2 + grid.SafeArea.margins.right
        anchors.topMargin: Theme.spacing / 2
        anchors.bottomMargin: Theme.spacing / 2 + Math.max(grid.SafeArea.margins.bottom, 8)
        columns: 1
        columnSpacing: Theme.spacing / 2
        rowSpacing: Theme.spacing / 2

        // ---- receive view: frequency, bands, spectrum, steps; the full
        // width in both orientations ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing / 2

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: page.formatMhz(Engine.frequencyMhz)
                    color: Theme.text
                    font.pixelSize: 30
                    font.family: "monospace"
                    TapHandler { onTapped: freqDialog.open() }
                }
                ModeButton { fontSize: 18 }
                // Landscape: the step first, then the slice at a glance and
                // in reach (one row).
                StepButton {
                    visible: page.landscape
                    fontSize: 13
                    Layout.leftMargin: Theme.spacing / 2
                }
                InfoPanel {
                    visible: page.landscape
                    singleLine: true
                    showStep: false
                }
                // Portrait: the room right of the mode takes the tuning step
                // and the DSP/AGC line, left-aligned, so the row under the
                // frequency only carries the slice.
                ColumnLayout {
                    visible: !page.landscape
                    spacing: 0
                    // No margin on the narrowest phones, or AGC meets the edge.
                    Layout.leftMargin: page.width < 400 ? 0 : Theme.spacing
                    StepButton {
                        Layout.alignment: Qt.AlignLeft
                        fontSize: 13
                        heightScale: 0.55
                    }
                    InfoPanel {
                        Layout.alignment: Qt.AlignLeft
                        singleLine: true
                        showSliceRow: false
                        showStep: false
                        rowScale: 0.55
                    }
                }
                Item { Layout.fillWidth: true }
            }
            // Tight under the frequency row: no gap, a lower row (still a
            // finger's height for its targets).
            InfoPanel {
                visible: !page.landscape
                showStep: false
                showStatusRow: false
                singleLine: true
                rowScale: 0.75
                Layout.fillWidth: true
                Layout.topMargin: -Theme.spacing / 2
            }

            // Horizontally scrolling band strip.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing / 2
                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.touchTarget
                    orientation: ListView.Horizontal
                    spacing: 4
                    clip: true
                    visible: Engine.bandsAvailable
                    model: Engine.bands
                    delegate: AppButton {
                        required property string modelData
                        text: modelData
                        leftPadding: 2
                        rightPadding: 2
                        width: Theme.buttonWidth
                        height: Theme.touchTarget
                        onClicked: Engine.selectBand(modelData)
                    }
                }
            }

            SliceBar {
                id: sliceBar
                Layout.fillWidth: true
                compact: true
            }

            PanadapterView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                rfLabelWidth: sliceBar.rightMeterWidth
                Layout.minimumHeight: page.landscape ? 90 : 160
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                // Step buttons: land on round frequencies (tuneByHz).
                Repeater {
                    model: [-1000, -100, -10, -1, 1, 10, 100, 1000]
                    delegate: AppButton {
                        required property int modelData
                        text: (modelData > 0 ? "+" : "−") + (Math.abs(modelData) >= 1000
                              ? Math.abs(modelData) / 1000 + "k" : Math.abs(modelData))
                        leftPadding: 1
                        rightPadding: 1
                        Layout.fillWidth: true
                        Layout.minimumWidth: 30
                        Layout.preferredWidth: Theme.buttonWidth
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: Engine.tuneByHz(modelData)
                    }
                }
                // Landscape: the controls button and PTT end this row, so
                // the low screen keeps its height for the spectrum.
                AppButton {
                    visible: page.landscape
                    text: qsTr("☰")
                    font.pixelSize: 20
                    Layout.preferredWidth: Theme.touchTarget * 1.2
                    Layout.preferredHeight: Theme.touchTarget
                    onClicked: controlsDrawer.open()
                }
                PttButton {
                    visible: page.landscape
                    Layout.preferredWidth: Math.max(150, page.width * 0.2)
                    Layout.preferredHeight: Theme.touchTarget
                    fontPx: 22
                }
            }
        }

        // ---- Portrait: PTT along the bottom, the controls button beside it
        // (landscape puts both at the end of the step row); the controls
        // live in a drawer. ----
        ColumnLayout {
            visible: !page.landscape
            Layout.fillWidth: true
            spacing: Theme.spacing / 2

            Label {
                Layout.fillWidth: true
                visible: !Engine.transmitAvailable
                text: Engine.transmitUnavailableReason
                color: Theme.textDim
                font.pixelSize: 12
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Label {
                visible: Engine.transmitting
                text: qsTr("%1 W   SWR %2").arg(Engine.forwardPowerW.toFixed(0))
                      .arg(Engine.swrValid ? Engine.swr.toFixed(1) : "–")
                color: Engine.swrValid && Engine.swr > 2.5 ? Theme.transmit : Theme.text
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing / 2
                AppButton {
                    text: qsTr("☰ Controls")
                    Layout.preferredWidth: Math.max(110, page.width * 0.22)
                    Layout.preferredHeight: page.pttHeight
                    onClicked: controlsDrawer.open()
                }
                PttButton {
                    id: ptt
                    Layout.fillWidth: true
                    Layout.preferredHeight: page.pttHeight
                    fontPx: 26
                }
            }
        }
    }

    // Controls slide in over the waterfall: from the right in landscape, from
    // the bottom in portrait. No dimming, so the waterfall stays visible; a
    // tap outside or a swipe back closes it. Edge swipes do not open it
    // (dragMargin 0): in landscape the PTT strip shares that edge, and a PTT
    // press must never turn into a drawer drag.
    Drawer {
        id: controlsDrawer
        objectName: "controlsDrawer"
        edge: page.landscape ? Qt.RightEdge : Qt.BottomEdge
        width: page.landscape ? Math.min(page.width * 0.5, 460) : page.width
        height: page.landscape ? page.height : Math.min(page.height * 0.55, 520)
        dim: false
        dragMargin: 0
        // A drawer opening while PTT is held would hide the button.
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
            anchors.bottomMargin: Theme.spacing + (page.landscape ? 0 : controlsDrawer.SafeArea.margins.bottom)
            anchors.rightMargin: Theme.spacing + (page.landscape ? controlsDrawer.SafeArea.margins.right : 0)
            spacing: Theme.spacing
            ControlsPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
            PttButton {
                Layout.fillWidth: true
                Layout.preferredHeight: page.pttHeight
                fontPx: 26
            }
        }
    }

    LogPage { id: logPage }
    NetworkDialog { id: networkDialog }
    RadioSetupPage { id: radioSetup }
    SetupSuggestion { onSetupRequested: radioSetup.open() }
    FrequencyDialog { id: freqDialog }
}
