import QtQuick
import QtQuick.Controls
import AetherMobile
import QtQuick.Layouts

Page {
    id: page
    focus: true

    // Hardware keyboard (Magic Keyboard): arrows tune by step (Shift x10),
    // +/- zoom, F opens direct entry. Space is hold-to-talk: press keys,
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
    background: Rectangle { color: Theme.background }

    // Layout adapts from the 12.9" iPad Pro (1366x1024 pt) down to 1024x768
    // and portrait: smaller type below 1200 pt, controls stacked under the
    // spectrum in portrait, and a scrolling control column so nothing is cut.
    readonly property bool compact: width < 1200
    readonly property bool portrait: height > width

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
        columns: page.portrait ? 1 : 2

        // Left (top in portrait): frequency, S-meter and the panadapter area.
        ColumnLayout {
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
                Label {
                    text: Engine.mode
                    color: Theme.accent
                    font.pixelSize: page.compact ? 24 : 32
                }
                // Zoom the pan out / in around the slice.
                AppButton {
                    text: "−"
                    font.pixelSize: 22
                    Layout.preferredWidth: page.compact ? 48 : 56
                    Layout.preferredHeight: Theme.touchTarget
                    focusPolicy: Qt.NoFocus
                    onClicked: Engine.zoom(2.0)
                }
                AppButton {
                    text: "+"
                    font.pixelSize: 22
                    Layout.preferredWidth: page.compact ? 48 : 56
                    Layout.preferredHeight: Theme.touchTarget
                    focusPolicy: Qt.NoFocus
                    onClicked: Engine.zoom(0.5)
                }
                Item { Layout.fillWidth: true }
                ColumnLayout {
                    // Fixed width so a changing reading never resizes the row.
                    Label {
                        text: Engine.sMeterText
                        color: Theme.text
                        font.pixelSize: page.compact ? 16 : 22
                        Layout.preferredWidth: page.compact ? 150 : 260
                    }
                    ProgressBar {
                        Layout.preferredWidth: page.compact ? 150 : 260
                        from: -130
                        to: -20
                        value: Engine.sMeterDbm
                    }
                }
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
                        font.pixelSize: page.compact ? 13 : 15
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

            PanadapterView {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            RowLayout {
                spacing: Theme.spacing
                Repeater {
                    model: [-1000, -100, -10, 10, 100, 1000]
                    delegate: AppButton {
                        required property int modelData
                        text: (modelData > 0 ? "+" : "") + (Math.abs(modelData) >= 1000
                              ? modelData / 1000 + (page.compact ? "k" : " kHz")
                              : modelData + (page.compact ? "" : " Hz"))
                        Layout.preferredHeight: Theme.touchTarget
                        Layout.fillWidth: true
                        onClicked: Engine.tuneTo(Engine.frequencyMhz + modelData / 1e6)
                focusPolicy: Qt.NoFocus
                    }
                }
            }
        }

        // Right (bottom in portrait): controls scroll when they do not fit;
        // PTT stays pinned below them.
        ColumnLayout {
            Layout.preferredWidth: page.portrait ? -1
                : Engine.controlsVisible ? Math.max(280, Math.min(360, page.width * 0.28)) : 160
            Layout.fillWidth: page.portrait
            Layout.fillHeight: !page.portrait
            Layout.preferredHeight: page.portrait && Engine.controlsVisible ? page.height * 0.42 : -1
            spacing: Theme.spacing

          ScrollView {
            id: controlsScroll
            visible: Engine.controlsVisible
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

          ColumnLayout {
            width: controlsScroll.availableWidth
            spacing: page.compact ? Theme.spacing / 2 : Theme.spacing

            Label { text: qsTr("Mode"); color: Theme.textDim; font.pixelSize: 16 }
            GridLayout {
                columns: Math.max(1, Engine.modes.length)
                columnSpacing: 4
                Layout.fillWidth: true
                Repeater {
                    model: Engine.modes
                    delegate: AppButton {
                        required property string modelData
                        text: modelData
                        // Six modes share one row; narrow columns keep labels whole.
                        leftPadding: 2
                        rightPadding: 2
                        font.pixelSize: page.compact ? 13 : 15
                        checkable: true
                        checked: Engine.mode === modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: Engine.mode = modelData
                    }
                }
            }

            Label {
                text: qsTr("Filter  %1 Hz").arg(Engine.filterHigh - Engine.filterLow)
                color: Theme.textDim
                font.pixelSize: 16
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
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: Engine.setFilterWidth(modelData)
                    }
                }
            }

            // Receive DSP of the active slice (radio-side).
            RowLayout {
                Layout.fillWidth: true
                Repeater {
                    model: [{ key: "nr", label: "NR" }, { key: "nb", label: "NB" }, { key: "anf", label: "ANF" }]
                    delegate: AppButton {
                        required property var modelData
                        text: modelData.label
                        checkable: true
                        checked: Engine[modelData.key]
                        focusPolicy: Qt.NoFocus
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: Engine[modelData.key] = !Engine[modelData.key]
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("AGC"); color: Theme.textDim; font.pixelSize: 16 }
                Repeater {
                    model: Engine.agcModes
                    delegate: AppButton {
                        required property string modelData
                        text: modelData
                        checkable: true
                        checked: Engine.agcMode === modelData
                        focusPolicy: Qt.NoFocus
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: Engine.agcMode = modelData
                    }
                }
            }

            Label { text: qsTr("AF gain"); color: Theme.textDim; font.pixelSize: 16 }
            RowLayout {
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

            // Transmit settings and meters; dimmed until the radio can transmit.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing / 2
                enabled: Engine.transmitAvailable || Engine.canUseDeviceMicrophone
                opacity: enabled ? 1.0 : 0.4

                Label {
                    text: qsTr("RF power  %1").arg(Engine.rfPower)
                    color: Theme.textDim
                    font.pixelSize: 16
                }
                Slider {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    stepSize: 1
                    value: Engine.rfPower
                    onMoved: Engine.rfPower = Math.round(value)
                }
                Label {
                    text: qsTr("Mic gain  %1").arg(Engine.micGain)
                    color: Theme.textDim
                    font.pixelSize: 16
                }
                Slider {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    stepSize: 1
                    value: Engine.micGain
                    onMoved: Engine.micGain = Math.round(value)
                }
                // Mic level as the radio meters it: aim for peaks around -10 dBFS.
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Mic"); color: Theme.textDim; font.pixelSize: 16 }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 12
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
                }
                Label {
                    visible: Engine.transmitting
                    text: qsTr("%1 W   SWR %2")
                          .arg(Engine.forwardPowerW.toFixed(0))
                          .arg(Engine.swrValid ? Engine.swr.toFixed(1) : "–")
                    color: Engine.swrValid && Engine.swr > 2.5 ? Theme.transmit : Theme.text
                    font.pixelSize: 22
                }
            }

            Label {
                Layout.fillWidth: true
                visible: !Engine.transmitAvailable
                text: Engine.transmitUnavailableReason
                color: Theme.textDim
                wrapMode: Text.Wrap
            }
            AppButton {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.touchTarget
                visible: Engine.canUseDeviceMicrophone
                text: qsTr("Use this device's microphone")
                onClicked: Engine.useDeviceMicrophone()
            }
            Label {
                Layout.fillWidth: true
                visible: Engine.lastError.length > 0
                text: Engine.lastError
                color: Theme.warning
                wrapMode: Text.Wrap
            }
          }
          }

            // With the panel hidden, keep PTT at the bottom of its strip.
            Item {
                visible: !Engine.controlsVisible && !page.portrait
                Layout.fillHeight: true
            }

            AppButton {
                id: ptt
                Layout.fillWidth: true
                Layout.preferredHeight: (page.compact ? 2 : 3) * Theme.touchTarget
                // Stays enabled while held: disabling a pressed button cancels
                // the press, which would unkey mid-transmission.
                enabled: Engine.transmitAvailable || pressed
                text: Engine.transmitting ? qsTr("TX") : qsTr("PTT")
                focusPolicy: Qt.NoFocus
                font.pixelSize: 32
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
                // Hold to transmit; release (or losing the touch) always unkeys.
                onPressed: Engine.setPtt(true)
                onReleased: Engine.setPtt(false, "button released")
                onCanceled: Engine.setPtt(false, "touch cancelled by the system")
            }
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
        height: page.portrait ? Math.min(page.height * 0.5, 560) : page.height
        dim: false
        dragMargin: 0
        onAboutToShow: Engine.setPtt(false, "controls drawer opened")
        background: Rectangle {
            color: Theme.surface
            opacity: 0.96
            radius: Theme.radius
        }

        ControlsPanel {
            anchors.fill: parent
            anchors.margins: Theme.spacing
            anchors.bottomMargin: Theme.spacing + controlsDrawer.SafeArea.margins.bottom
            anchors.rightMargin: Theme.spacing + controlsDrawer.SafeArea.margins.right
            showPinButton: true
            onPinRequested: {
                controlsDrawer.close()
                Engine.controlsVisible = true
            }
        }
    }

    LogPage { id: logPage }

    FrequencyDialog {
        id: freqDialog
        onClosed: page.forceActiveFocus()
    }
    Component.onCompleted: page.forceActiveFocus()
    // A layout switch (rotation, Split View, fold/unfold) destroys this page;
    // a held PTT must not outlive its button.
    Component.onDestruction: Engine.setPtt(false, "layout changed")
}
