import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The tabbed operating controls (Mode, DSP, Audio, TX, CW, View, Profiles) shown in the
// slide-in controls drawer.
ColumnLayout {
    id: panel
    spacing: Theme.spacing / 2
    implicitWidth: 200

    // Tablet only: offer to put the panel back into the page.
    property bool showPinButton: false
    signal pinRequested()

    AppButton {
        visible: panel.showPinButton
        text: qsTr("Pin controls to the side")
        Layout.fillWidth: true
        onClicked: panel.pinRequested()
    }

    TabBar {
        id: tabs
        objectName: "controlsTabs"
        Layout.fillWidth: true
        TabButton { text: qsTr("Mode") }
        TabButton { text: qsTr("DSP") }
        TabButton { text: qsTr("Audio") }
        TabButton { text: qsTr("TX") }
        TabButton { text: qsTr("CW") }
        TabButton { text: qsTr("View") }
        TabButton { text: qsTr("Profiles") }
    }

    StackLayout {
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
                            checked: Engine.activeFilterPreset === modelData
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
                            checked: Engine.agcMode === modelData
                            Layout.fillWidth: true
                            onClicked: Engine.agcMode = modelData
                        }
                    }
                }
                RxControls {
                    Layout.fillWidth: true
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
                EqControls {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacing
                }
            }
        }

        // Transmit: tune, ATU, PROC, monitor, power and microphone.
        ScrollView {
            id: txScroll
            clip: true
            contentWidth: availableWidth
            TxControls {
                width: txScroll.availableWidth
            }
        }

        // CW: key, CWX, speed, pitch, break-in, decoder.
        ScrollView {
            id: cwScroll
            clip: true
            contentWidth: availableWidth
            CwControls {
                width: cwScroll.availableWidth
            }
        }

        // View: waterfall and spectrum levels.
        ScrollView {
            id: viewScroll
            clip: true
            contentWidth: availableWidth
            DisplayControls {
                width: viewScroll.availableWidth
            }
        }

        // Profiles: global, TX and mic.
        ScrollView {
            id: profileScroll
            clip: true
            contentWidth: availableWidth
            ProfileControls {
                width: profileScroll.availableWidth
            }
        }
    }

    Label {
        Layout.fillWidth: true
        visible: Engine.lastError.length > 0 || !Engine.transmitAvailable
        text: Engine.lastError.length > 0 ? Engine.lastError : Engine.transmitUnavailableReason
        color: Engine.lastError.length > 0 ? Theme.warning : Theme.textDim
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }
}
