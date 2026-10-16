import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Radio Setup, after SmartSDR's and the desktop's: the radio's own settings
// a radio needs on first use (identity, remote, transmit limits and timing,
// CW keyer and microphone, line out and headphones). Every change is written
// to the radio; values are re-read once a second while the page is open.
Popup {
    id: setup
    objectName: "radioSetup"
    parent: Overlay.overlay
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width * 0.95 : 600, 640)
    height: parent ? parent.height * 0.9 : 800
    padding: Theme.spacing
    background: Rectangle { color: Theme.surface; radius: Theme.radius }

    property var d: ({})
    property int section: 0
    function refresh() { d = Engine.radioSetup() }
    function set(key, value) { Engine.setRadioSetup(key, value); refreshSoon.restart() }
    onOpened: refresh()
    Timer { interval: 1000; repeat: true; running: setup.opened; onTriggered: setup.refresh() }
    Timer { id: refreshSoon; interval: 300; onTriggered: setup.refresh() }

    // ---- pieces ----
    component Heading: Label {
        color: Theme.textDim
        font.pixelSize: 14
        Layout.topMargin: Theme.spacing / 2
    }
    component Info: RowLayout {
        property string label
        property string value
        Layout.fillWidth: true
        Label { text: parent.label; color: Theme.textDim; font.pixelSize: 15; Layout.fillWidth: true }
        Label { text: parent.value.length > 0 ? parent.value : "–"; color: Theme.text; font.pixelSize: 15; font.bold: true }
    }
    // On/off as two of the app's buttons, the current one dark.
    component OnOff: RowLayout {
        id: onOff
        property string label
        property bool on
        signal toggled(bool on)
        Layout.fillWidth: true
        Label { text: onOff.label; color: Theme.text; font.pixelSize: 15; Layout.fillWidth: true; wrapMode: Text.Wrap }
        AppButton { text: qsTr("Off"); checked: !onOff.on; focusPolicy: Qt.NoFocus; Layout.preferredWidth: 64; onClicked: onOff.toggled(false) }
        AppButton { text: qsTr("On"); checked: onOff.on; focusPolicy: Qt.NoFocus; Layout.preferredWidth: 64; onClicked: onOff.toggled(true) }
    }
    // A slider that writes when released (not a command per pixel).
    component Level: ColumnLayout {
        id: level
        property string label
        property string unit: ""
        property real from: 0
        property real to: 100
        property real stepSize: 1
        property real current: 0
        signal committed(real value)
        Layout.fillWidth: true
        spacing: 0
        Label {
            text: level.label + "  " + Math.round(slider.pressed ? slider.value : level.current) + level.unit
            color: Theme.text
            font.pixelSize: 15
        }
        Slider {
            id: slider
            Layout.fillWidth: true
            from: level.from
            to: level.to
            stepSize: level.stepSize
            onPressedChanged: if (!pressed) level.committed(Math.round(value))
        }
        // Follows the radio except while the finger is on it.
        Binding { target: slider; property: "value"; value: level.current; when: !slider.pressed }
    }
    // A text value with its own Save (written only on Save).
    component TextSetting: ColumnLayout {
        id: textSetting
        property string label
        property string current
        property string hint
        property bool upper: true
        signal saved(string text)
        Layout.fillWidth: true
        spacing: 2
        Label { text: textSetting.label; color: Theme.text; font.pixelSize: 15 }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: field
                Layout.fillWidth: true
                font.pixelSize: 17
                placeholderText: textSetting.current.length > 0 ? textSetting.current : textSetting.hint
                inputMethodHints: textSetting.upper ? (Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText) : Qt.ImhNoPredictiveText
                onAccepted: saveButton.clicked()
            }
            AppButton {
                id: saveButton
                text: qsTr("Save")
                enabled: field.text.trim().length > 0
                focusPolicy: Qt.NoFocus
                onClicked: {
                    textSetting.saved(textSetting.upper ? field.text.trim().toUpperCase() : field.text.trim())
                    field.text = ""
                }
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing / 2
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Radio Setup"); color: Theme.text; font.pixelSize: 22; font.bold: true; Layout.fillWidth: true }
            AppButton { text: qsTr("Close"); focusPolicy: Qt.NoFocus; onClicked: setup.close() }
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("These are the radio's own settings, as in SmartSDR's Radio Setup: changes are written to the radio.")
            color: Theme.textDim
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 5
            columnSpacing: 4
            rowSpacing: 4
            Repeater {
                model: [qsTr("Radio"), qsTr("Transmit"), qsTr("Phone/CW"), qsTr("Audio"), qsTr("Network")]
                delegate: AppButton {
                    required property int index
                    required property string modelData
                    text: modelData
                    checked: setup.section === index
                    focusPolicy: Qt.NoFocus
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    onClicked: setup.section = index
                }
            }
        }
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            StackLayout {
                width: scroll.availableWidth
                currentIndex: setup.section

                // ---- Radio ----
                ColumnLayout {
                    spacing: Theme.spacing / 2
                    TextSetting {
                        label: qsTr("Callsign  (%1)").arg(setup.d.callsign || qsTr("not set"))
                        current: setup.d.callsign || ""
                        hint: qsTr("e.g. OH6OKSA")
                        onSaved: (t) => setup.set("callsign", t)
                    }
                    TextSetting {
                        label: qsTr("Radio name  (%1)").arg(setup.d.nickname || "–")
                        current: setup.d.nickname || ""
                        hint: qsTr("Shown in the radio list")
                        upper: false
                        onSaved: (t) => setup.set("nickname", t)
                    }
                    Heading { text: qsTr("Remote") }
                    OnOff {
                        label: qsTr("Remote on (SmartLink may power the radio on)")
                        on: !!setup.d.remoteOn
                        onToggled: (v) => setup.set("remoteOn", v)
                    }
                    OnOff {
                        label: qsTr("Mute the radio's own audio while remote")
                        on: !!setup.d.muteLocalWhenRemote
                        onToggled: (v) => setup.set("muteLocalWhenRemote", v)
                    }
                    Heading { text: qsTr("Stations") }
                    AppButton {
                        text: qsTr("Connected stations (%1)…").arg(Engine.stations.length)
                        leftPadding: 16
                        rightPadding: 16
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: {
                            setup.close()
                            Engine.requestStations()
                        }
                    }
                    Heading { text: qsTr("About the radio") }
                    Info { label: qsTr("Model"); value: setup.d.model || "" }
                    Info { label: qsTr("Serial"); value: setup.d.serial || "" }
                    Info { label: qsTr("Firmware"); value: setup.d.version || "" }
                    Info { label: qsTr("Region"); value: setup.d.region || "" }
                    Info { label: qsTr("Address"); value: setup.d.ip || "" }
                }

                // ---- Transmit ----
                ColumnLayout {
                    spacing: Theme.spacing / 2
                    Level {
                        label: qsTr("Maximum power")
                        unit: " %"
                        current: setup.d.maxPower || 0
                        onCommitted: (v) => setup.set("maxPower", v)
                    }
                    Level {
                        label: qsTr("TX delay")
                        unit: " ms"
                        to: 500
                        stepSize: 5
                        current: setup.d.txDelay || 0
                        onCommitted: (v) => setup.set("txDelay", v)
                    }
                    Level {
                        label: qsTr("Transmit timeout (0 = off)")
                        unit: " s"
                        to: 600
                        stepSize: 10
                        current: setup.d.interlockTimeoutS || 0
                        onCommitted: (v) => setup.set("interlockTimeoutS", v)
                    }
                    Heading { text: qsTr("TUNE signal") }
                    RowLayout {
                        Layout.fillWidth: true
                        AppButton {
                            text: qsTr("Single tone")
                            checked: setup.d.tuneMode === "single_tone"
                            focusPolicy: Qt.NoFocus
                            Layout.fillWidth: true
                            onClicked: setup.set("tuneMode", "single_tone")
                        }
                        AppButton {
                            text: qsTr("Two tone")
                            checked: setup.d.tuneMode === "two_tone"
                            focusPolicy: Qt.NoFocus
                            Layout.fillWidth: true
                            onClicked: setup.set("tuneMode", "two_tone")
                        }
                    }
                }

                // ---- Phone/CW ----
                ColumnLayout {
                    spacing: Theme.spacing / 2
                    Heading { text: qsTr("Microphone") }
                    OnOff { label: qsTr("Mic bias (electret microphones)"); on: !!setup.d.micBias; onToggled: (v) => setup.set("micBias", v) }
                    OnOff { label: qsTr("Mic boost (+20 dB)"); on: !!setup.d.micBoost; onToggled: (v) => setup.set("micBoost", v) }
                    Heading { text: qsTr("CW keyer") }
                    Level {
                        label: qsTr("Break-in delay")
                        unit: " ms"
                        to: 2000
                        stepSize: 10
                        current: setup.d.cwDelay || 0
                        onCommitted: (v) => setup.set("cwDelay", v)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Iambic"); color: Theme.text; font.pixelSize: 15; Layout.fillWidth: true }
                        AppButton { text: qsTr("Off"); checked: !setup.d.iambic; focusPolicy: Qt.NoFocus; Layout.preferredWidth: 56; onClicked: setup.set("iambic", false) }
                        AppButton {
                            text: "A"
                            checked: !!setup.d.iambic && setup.d.iambicMode === 0
                            focusPolicy: Qt.NoFocus
                            Layout.preferredWidth: 56
                            onClicked: { setup.set("iambic", true); setup.set("iambicMode", 0) }
                        }
                        AppButton {
                            text: "B"
                            checked: !!setup.d.iambic && setup.d.iambicMode === 1
                            focusPolicy: Qt.NoFocus
                            Layout.preferredWidth: 56
                            onClicked: { setup.set("iambic", true); setup.set("iambicMode", 1) }
                        }
                    }
                    OnOff { label: qsTr("Swap paddles"); on: !!setup.d.swapPaddles; onToggled: (v) => setup.set("swapPaddles", v) }
                }

                // ---- Audio ----
                ColumnLayout {
                    spacing: Theme.spacing / 2
                    Heading { text: qsTr("The radio's own audio outputs") }
                    Level { label: qsTr("Line out"); current: setup.d.lineoutGain || 0; onCommitted: (v) => setup.set("lineoutGain", v) }
                    OnOff { label: qsTr("Line out muted"); on: !!setup.d.lineoutMute; onToggled: (v) => setup.set("lineoutMute", v) }
                    Level { label: qsTr("Headphones"); current: setup.d.headphoneGain || 0; onCommitted: (v) => setup.set("headphoneGain", v) }
                    OnOff { label: qsTr("Headphones muted"); on: !!setup.d.headphoneMute; onToggled: (v) => setup.set("headphoneMute", v) }
                }

                // ---- Network ----
                ColumnLayout {
                    id: network
                    spacing: Theme.spacing / 2
                    // Held −/+ count from the value at the press (the radio
                    // read-back lags behind the repeats).
                    property int acc: 1450
                    function step(n, dir) {
                        if (n === 0) acc = setup.d.networkMtu || 1450
                        acc = Math.max(576, Math.min(9000, acc + dir * (n < 10 ? 10 : 50)))
                        setup.set("networkMtu", acc)
                    }
                    Heading { text: qsTr("Network MTU") }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Largest packet the radio sends this app (VITA-49 over UDP). 1450 suits most networks and VPNs; lower it if the waterfall or audio break up over a tunnel. Over Tailscale at most 1200 is used.")
                        color: Theme.textDim
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        RepeatButton {
                            text: "−"
                            Layout.preferredWidth: 56
                            onStepped: (n) => network.step(n, -1)
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: (setup.d.networkMtu || 1450) + " " + qsTr("bytes")
                            color: Theme.text
                            font.pixelSize: 20
                            font.bold: true
                        }
                        RepeatButton {
                            text: "+"
                            Layout.preferredWidth: 56
                            onStepped: (n) => network.step(n, 1)
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: [1500, 1450, 1400, 1300, 1200, 1000]
                            delegate: AppButton {
                                required property int modelData
                                text: modelData + (modelData === 1450 ? " (" + qsTr("default") + ")" : "")
                                checked: setup.d.networkMtu === modelData
                                leftPadding: 12
                                rightPadding: 12
                                focusPolicy: Qt.NoFocus
                                onClicked: setup.set("networkMtu", modelData)
                            }
                        }
                    }
                }
            }
        }
    }
}
