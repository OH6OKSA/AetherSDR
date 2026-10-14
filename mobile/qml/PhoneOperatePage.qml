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
            ToolButton {
                text: qsTr("Controls")
                focusPolicy: Qt.NoFocus
                onClicked: controlsDrawer.open()
            }
            ToolButton { text: qsTr("Log"); focusPolicy: Qt.NoFocus; onClicked: logPage.open() }
            ToolButton { text: qsTr("Disconnect"); onClicked: Engine.disconnectRadio() }
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
                Label { text: Engine.mode; color: Theme.accent; font.pixelSize: 18 }
                LockButton { implicitWidth: 44 }
                Item { Layout.fillWidth: true }
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
                Layout.fillWidth: true
                compact: true
            }

            PanadapterView {
                Layout.fillWidth: true
                Layout.fillHeight: true
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
                visible: (Engine.lastError.length > 0 || !Engine.transmitAvailable)
                         && !page.landscape
                text: Engine.lastError.length > 0 ? Engine.lastError : Engine.transmitUnavailableReason
                color: Engine.lastError.length > 0 ? Theme.warning : Theme.textDim
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

            AppButton {
                id: ptt
                Layout.fillWidth: true
                Layout.preferredHeight: page.landscape ? 120 : 64
                // In CW modes the button is the straight key (KEY), in the same place.
                readonly property bool keyMode: Engine.cwMode && !Engine.tuning
                enabled: (keyMode ? Engine.cwKeyAvailable : Engine.transmitAvailable) || Engine.tuning || pressed
                text: Engine.tuning ? qsTr("TUNE") : keyMode ? (Engine.cwKeyDown ? qsTr("KEY ●") : qsTr("KEY"))
                      : Engine.transmitting ? qsTr("TX") : qsTr("PTT")
                font.pixelSize: 26
                font.bold: true
                // Ready, unavailable and transmitting must look different at a
                // glance: ready has a bright border and label, unavailable is dim.
                contentItem: Text {
                    text: ptt.text
                    font: ptt.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: ptt.enabled ? "white" : Theme.textDim
                    opacity: ptt.enabled ? 1.0 : 0.5
                }
                background: Rectangle {
                    radius: Theme.radius
                    color: Engine.transmitting || Engine.tuning || Engine.cwKeyDown ? Theme.transmit
                         : ptt.enabled ? Theme.pttReady : Theme.surface
                    border.width: Engine.transmitting ? 3 : ptt.enabled ? 2 : 1
                    border.color: Engine.transmitting ? "white"
                                : ptt.enabled ? Theme.transmit : Theme.surfaceRaised
                }
                // While the tune carrier is on, PTT is its off switch.
                onPressed: Engine.tuning ? Engine.stopTransmit("PTT pressed during tune")
                         : keyMode ? Engine.setCwKey(true) : Engine.setPtt(true)
                onReleased: keyMode ? Engine.setCwKey(false, "key released") : Engine.setPtt(false, "button released")
                onCanceled: keyMode ? Engine.setCwKey(false, "touch cancelled by the system")
                                    : Engine.setPtt(false, "touch cancelled by the system")
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

        ControlsPanel {
            anchors.fill: parent
            anchors.margins: Theme.spacing
            anchors.bottomMargin: Theme.spacing + (page.landscape ? 0 : controlsDrawer.SafeArea.margins.bottom)
            anchors.rightMargin: Theme.spacing + (page.landscape ? controlsDrawer.SafeArea.margins.right : 0)
        }
    }

    LogPage { id: logPage }
    FrequencyDialog { id: freqDialog }
}
