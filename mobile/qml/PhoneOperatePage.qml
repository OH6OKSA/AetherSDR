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
    readonly property int receiveWidth: Math.round(Engine.controlsVisible
        ? grid.width * 0.56 : grid.width - pttStripWidth - grid.columnSpacing)

    function formatMhz(mhz) {
        const hz = Math.round(mhz * 1e6)
        const mhzPart = Math.floor(hz / 1e6)
        const khzPart = Math.floor((hz % 1e6) / 1e3)
        const hzPart = hz % 1e3
        return mhzPart + "." + String(khzPart).padStart(3, "0") + "." + String(hzPart).padStart(3, "0")
    }

    // A layout switch (rotation, fold/unfold) destroys this page; a held
    // PTT must not outlive its button.
    Component.onDestruction: Engine.setPtt(false, "layout changed")

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
                text: Engine.controlsVisible ? qsTr("Hide controls") : qsTr("Controls")
                focusPolicy: Qt.NoFocus
                onClicked: Engine.controlsVisible = !Engine.controlsVisible
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
                AppButton { text: "−"; implicitWidth: 40; implicitHeight: 40; onClicked: Engine.zoom(2.0) }
                AppButton { text: "+"; implicitWidth: 40; implicitHeight: 40; onClicked: Engine.zoom(0.5) }
                Item { Layout.fillWidth: true }
                // Fixed width: a changing reading must not resize the row (and
                // with it the spectrum, which would restart the radio's FFT).
                Label {
                    text: Engine.sMeterText
                    color: Theme.text
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignRight
                    Layout.preferredWidth: 104
                }
            }
            ProgressBar {
                Layout.fillWidth: true
                from: -130
                to: -20
                value: Engine.sMeterDbm
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
                        font.pixelSize: 14
                        width: 56
                        height: Theme.touchTarget
                        onClicked: Engine.selectBand(modelData)
                    }
                }
            }

            PanadapterView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 160
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: [-1000, -100, 100, 1000]
                    delegate: AppButton {
                        required property int modelData
                        text: (modelData > 0 ? "+" : "") + (Math.abs(modelData) >= 1000 ? modelData / 1000 + "k" : modelData)
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        onClicked: Engine.tuneTo(Engine.frequencyMhz + modelData / 1e6)
                    }
                }
            }
        }

        // ---- controls: tabs plus PTT ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: page.landscape
            Layout.preferredHeight: page.landscape || !Engine.controlsVisible ? -1 : page.height * 0.40
            spacing: Theme.spacing / 2

            TabBar {
                id: tabs
                visible: Engine.controlsVisible
                Layout.fillWidth: true
                TabButton { text: qsTr("Mode") }
                TabButton { text: qsTr("DSP") }
                TabButton { text: qsTr("Audio") }
                TabButton { text: qsTr("TX") }
            }

            StackLayout {
                visible: Engine.controlsVisible
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: tabs.currentIndex

                // Mode
                ScrollView {
                    id: modeScroll
                    clip: true
                    contentWidth: availableWidth
                    GridLayout {
                        width: modeScroll.availableWidth
                        columns: Math.max(1, Engine.modes.length)
                        columnSpacing: 4
                        Repeater {
                            model: Engine.modes
                            delegate: AppButton {
                                required property string modelData
                                text: modelData
                                leftPadding: 2
                                rightPadding: 2
                                checkable: true
                                checked: Engine.mode === modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: Theme.touchTarget
                                onClicked: Engine.mode = modelData
                            }
                        }
                    }
                }

                // Filter and receive DSP
                ScrollView {
                    id: dspScroll
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: dspScroll.availableWidth
                        Label {
                            text: qsTr("Filter  %1 Hz").arg(Engine.filterHigh - Engine.filterLow)
                            color: Theme.textDim
                            visible: Engine.filterPresets.length > 0
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: Engine.filterPresets.length > 0
                            Repeater {
                                model: Engine.filterPresets
                                delegate: AppButton {
                                    required property int modelData
                                    text: modelData >= 1000 ? (modelData / 1000).toFixed(1) + "k" : modelData
                                    checkable: true
                                    checked: Math.abs((Engine.filterHigh - Engine.filterLow) - modelData) < 60
                                    Layout.fillWidth: true
                                    onClicked: Engine.setFilterWidth(modelData)
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Repeater {
                                model: [{ key: "nr", label: "NR" }, { key: "nb", label: "NB" }, { key: "anf", label: "ANF" }]
                                delegate: AppButton {
                                    required property var modelData
                                    text: modelData.label
                                    checkable: true
                                    checked: Engine[modelData.key]
                                    Layout.fillWidth: true
                                    onClicked: Engine[modelData.key] = !Engine[modelData.key]
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("AGC"); color: Theme.textDim }
                            Repeater {
                                model: Engine.agcModes
                                delegate: AppButton {
                                    required property string modelData
                                    text: modelData
                                    checkable: true
                                    checked: Engine.agcMode === modelData
                                    Layout.fillWidth: true
                                    onClicked: Engine.agcMode = modelData
                                }
                            }
                        }
                    }
                }

                // Audio
                ScrollView {
                    id: audioScroll
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: audioScroll.availableWidth
                        Label { text: qsTr("AF gain"); color: Theme.textDim }
                        RowLayout {
                            Layout.fillWidth: true
                            Slider {
                                Layout.fillWidth: true
                                from: 0
                                to: 100
                                value: Engine.afGain
                                onMoved: Engine.afGain = value
                            }
                            AppButton {
                                text: Engine.muted ? qsTr("Unmute") : qsTr("Mute")
                                onClicked: Engine.muted = !Engine.muted
                            }
                        }
                    }
                }

                // Transmit settings
                ScrollView {
                    id: txScroll
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: txScroll.availableWidth
                        enabled: Engine.transmitAvailable || Engine.canUseDeviceMicrophone
                        Label { text: qsTr("RF power  %1").arg(Engine.rfPower); color: Theme.textDim }
                        Slider {
                            Layout.fillWidth: true
                            from: 0
                            to: 100
                            stepSize: 1
                            value: Engine.rfPower
                            onMoved: Engine.rfPower = Math.round(value)
                        }
                        Label { text: qsTr("Mic gain  %1").arg(Engine.micGain); color: Theme.textDim }
                        Slider {
                            Layout.fillWidth: true
                            from: 0
                            to: 100
                            stepSize: 1
                            value: Engine.micGain
                            onMoved: Engine.micGain = Math.round(value)
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            height: 10
                            radius: 3
                            color: Theme.surface
                            Rectangle {
                                height: parent.height
                                radius: 3
                                width: parent.width * Math.max(0, Math.min(1, (Engine.micLevelDbfs + 40) / 40))
                                color: Engine.micPeakDbfs > -3 ? Theme.transmit
                                     : Engine.micPeakDbfs > -10 ? Theme.warning : Theme.accent
                            }
                        }
                        AppButton {
                            Layout.fillWidth: true
                            visible: Engine.canUseDeviceMicrophone
                            text: qsTr("Use this device's microphone")
                            onClicked: Engine.useDeviceMicrophone()
                        }
                    }
                }
            }

            // With the panel hidden in landscape, push PTT to the bottom of
            // its strip.
            Item {
                visible: page.landscape && !Engine.controlsVisible
                Layout.fillHeight: true
            }

            Label {
                Layout.fillWidth: true
                visible: (Engine.lastError.length > 0 || !Engine.transmitAvailable)
                         && (Engine.controlsVisible || !page.landscape)
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
                Layout.preferredHeight: page.landscape && !Engine.controlsVisible ? 120 : 64
                enabled: Engine.transmitAvailable || pressed
                text: Engine.transmitting ? qsTr("TX") : qsTr("PTT")
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
                    color: Engine.transmitting ? Theme.transmit
                         : ptt.enabled ? Theme.pttReady : Theme.surface
                    border.width: Engine.transmitting ? 3 : ptt.enabled ? 2 : 1
                    border.color: Engine.transmitting ? "white"
                                : ptt.enabled ? Theme.transmit : Theme.surfaceRaised
                }
                onPressed: Engine.setPtt(true)
                onReleased: Engine.setPtt(false, "button released")
                onCanceled: Engine.setPtt(false, "touch cancelled by the system")
            }
        }
    }

    LogPage { id: logPage }
    FrequencyDialog { id: freqDialog }
}
