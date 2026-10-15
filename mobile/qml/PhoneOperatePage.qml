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
    // PTT height, shared with the drawer's copy of the button.
    readonly property real pttHeight: landscape ? 120 : 64
    // Landscape spectrum width: 56 % with the control panel, everything but
    // a PTT strip without it.
    readonly property int pttStripWidth: 120
    readonly property int receiveWidth: Math.round(grid.width - pttStripWidth - grid.columnSpacing)

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
        columns: page.landscape ? 2 : 1
        columnSpacing: Theme.spacing / 2
        rowSpacing: Theme.spacing / 2

        // ---- receive view: frequency, bands, spectrum, steps ----
        // In landscape this column has a fixed share of the width, so text in
        // the control column can never change the spectrum's size.
        ColumnLayout {
            Layout.fillWidth: !page.landscape
            Layout.fillHeight: true
            Layout.preferredWidth: page.landscape ? page.receiveWidth : -1
            Layout.maximumWidth: page.landscape ? page.receiveWidth : Number.POSITIVE_INFINITY
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
                // The slice at a glance and in reach (one row); under the
                // frequency in portrait.
                InfoPanel {
                    visible: page.landscape
                    singleLine: true
                    Layout.leftMargin: Theme.spacing / 2
                }
                Item { Layout.fillWidth: true }
            }
            InfoPanel {
                visible: !page.landscape
                singleLine: true
                Layout.fillWidth: true
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
                Layout.minimumHeight: 160
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
            }
        }

        // ---- PTT strip: right edge in landscape, bottom in portrait. The
        // controls live in a drawer so the waterfall keeps the screen. ----
        ColumnLayout {
            Layout.fillWidth: !page.landscape
            Layout.fillHeight: page.landscape
            Layout.preferredWidth: page.landscape ? page.pttStripWidth : -1
            spacing: Theme.spacing / 2

            AppButton {
                text: qsTr("☰ Controls")
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.touchTarget
                onClicked: controlsDrawer.open()
            }

            // In landscape, PTT sits at the bottom of its strip.
            Item {
                visible: page.landscape
                Layout.fillHeight: true
            }

            Label {
                Layout.fillWidth: true
                visible: !Engine.transmitAvailable
                         && !page.landscape
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

            PttButton {
                id: ptt
                Layout.fillWidth: true
                Layout.preferredHeight: page.pttHeight
                fontPx: 26
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
