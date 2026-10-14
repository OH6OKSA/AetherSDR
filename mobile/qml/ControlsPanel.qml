import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The operating controls in sections (Mode, DSP, Audio, TX, CW, View,
// Profiles, Memory). In the slide-in drawer they are tabs, one at a time.
// Pinned to the iPad's side (multiSelect) they are toggle buttons, as
// AetherSDR's applet buttons: every chosen section shows, one under another.
ColumnLayout {
    id: panel
    spacing: Theme.spacing / 2
    implicitWidth: 200

    // Tablet only: offer to put the panel back into the page.
    property bool showPinButton: false
    // Pinned side panel: several sections at once, chosen by toggles.
    property bool multiSelect: false
    signal pinRequested()

    readonly property var sections: [
        { name: "Mode", title: qsTr("Mode"), component: modeSection },
        { name: "DSP", title: qsTr("DSP"), component: dspSection },
        { name: "Audio", title: qsTr("Audio"), component: audioSection },
        { name: "TX", title: qsTr("TX"), component: txSection },
        { name: "CW", title: qsTr("CW"), component: cwSection },
        { name: "View", title: qsTr("View"), component: viewSection },
        { name: "Profiles", title: qsTr("Profiles"), component: profileSection },
        { name: "Memory", title: qsTr("Memory"), component: memorySection },
    ]

    function toggleSection(name) {
        const list = Engine.pinnedSections.slice()
        const i = list.indexOf(name)
        if (i >= 0) list.splice(i, 1)
        else list.push(name)
        Engine.pinnedSections = list
    }

    AppButton {
        visible: panel.showPinButton
        text: qsTr("Pin controls to the side")
        Layout.fillWidth: true
        onClicked: panel.pinRequested()
    }

    // ---- Drawer: one section at a time, chosen from a button grid ----
    // A grid rather than a tab bar: on an iPhone all eight sections show at
    // once (4 x 2) instead of hiding behind a scrolling strip.
    Item {
        id: tabs
        objectName: "controlsTabs"
        property int currentIndex: 0
        visible: !panel.multiSelect
        Layout.fillWidth: true
        implicitHeight: tabGrid.implicitHeight
        GridLayout {
            id: tabGrid
            width: parent.width
            columns: panel.width >= 640 ? 8 : 4
            columnSpacing: 4
            rowSpacing: 4
            Repeater {
                model: panel.sections
                delegate: AppButton {
                    required property int index
                    required property var modelData
                    text: modelData.title
                    checked: tabs.currentIndex === index
                    leftPadding: 2
                    rightPadding: 2
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 40
                    focusPolicy: Qt.NoFocus
                    onClicked: tabs.currentIndex = index
                }
            }
        }
    }
    StackLayout {
        visible: !panel.multiSelect
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: tabs.currentIndex
        Repeater {
            model: panel.multiSelect ? [] : panel.sections
            delegate: ScrollView {
                id: tabScroll
                required property var modelData
                clip: true
                contentWidth: availableWidth
                Loader {
                    width: tabScroll.availableWidth
                    sourceComponent: tabScroll.modelData.component
                }
            }
        }
    }

    // ---- Pinned: toggles, then every chosen section ----
    Flow {
        visible: panel.multiSelect
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: panel.sections
            delegate: AppButton {
                required property var modelData
                text: modelData.title
                checked: Engine.pinnedSections.indexOf(modelData.name) >= 0
                leftPadding: 8
                rightPadding: 8
                focusPolicy: Qt.NoFocus
                onClicked: panel.toggleSection(modelData.name)
            }
        }
    }
    ScrollView {
        id: pinnedScroll
        visible: panel.multiSelect
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: pinnedScroll.availableWidth
            spacing: Theme.spacing
            Repeater {
                model: panel.multiSelect ? panel.sections : []
                delegate: ColumnLayout {
                    required property var modelData
                    readonly property bool shown: Engine.pinnedSections.indexOf(modelData.name) >= 0
                    visible: shown
                    Layout.fillWidth: true
                    spacing: Theme.spacing / 2
                    Label { text: modelData.title; color: Theme.accent; font.pixelSize: 15; font.bold: true }
                    Loader {
                        Layout.fillWidth: true
                        active: parent.shown
                        sourceComponent: modelData.component
                    }
                }
            }
            Label {
                visible: Engine.pinnedSections.length === 0
                text: qsTr("Choose the controls to show with the buttons above.")
                color: Theme.textDim
                wrapMode: Text.Wrap
                Layout.fillWidth: true
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

    // ---- Sections ----
    Component {
        id: modeSection
        GridLayout {
            columns: Math.max(1, Math.min(6, Engine.modes.length))
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
    Component {
        id: dspSection
        ColumnLayout {
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
    Component {
        id: audioSection
        ColumnLayout {
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
    Component { id: txSection; TxControls {} }
    Component { id: cwSection; CwControls {} }
    Component { id: viewSection; DisplayControls {} }
    Component { id: profileSection; ProfileControls {} }
    Component { id: memorySection; MemoryControls {} }
}
