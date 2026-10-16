import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// One selectable meter face (SmartSDR style). Tap to choose what it shows.
// TX meters read only while the radio is on the air; otherwise they show "–".
Item {
    id: meter
    // "left" or "right": which remembered selection this face uses.
    property string side: "left"
    property bool compact: false
    // Receive and transmit each have their own choice (e.g. S / SWR).
    readonly property bool txView: Engine.radioTransmitting || Engine.tuning
    readonly property string kind: txView ? (side === "left" ? Engine.meterLeftTx : Engine.meterRightTx)
                                          : (side === "left" ? Engine.meterLeft : Engine.meterRight)
    readonly property string rxKind: side === "left" ? Engine.meterLeft : Engine.meterRight
    readonly property string txKind: side === "left" ? Engine.meterLeftTx : Engine.meterRightTx

    implicitWidth: compact ? 120 : 180
    // Where space is short (iPhone landscape) the host can ask for bars.
    property bool analogAllowed: true
    readonly property bool analog: (side === "left" ? Engine.analogLeft : Engine.analogRight) && analogAllowed
    // Height of an analog face; the host sizes it to the space available.
    property real faceHeight: compact ? 66 : 92
    implicitHeight: analog ? faceHeight : (compact ? 34 : 44)
    // Width the analog face needs at its height (no empty sides).
    readonly property real analogFitWidth: gauge.fitWidth

    // value, min, max, text, alarm level and whether the reading is live
    readonly property var reading: {
        const tx = Engine.radioTransmitting
        switch (kind) {
        case "S":
            return { v: Engine.sMeterDbm, min: -127, max: -13, live: true, text: Engine.sMeterText, red: 1e9 }
        case "Power":
            return { v: Engine.forwardPowerW, min: 0, max: 120, live: tx,
                     text: Engine.forwardPowerW.toFixed(0) + " W", red: 100 }
        case "SWR":
            return { v: Engine.swrValid ? Engine.swr : 1, min: 1, max: 3, live: tx && Engine.swrValid,
                     text: "SWR " + (Engine.swrValid ? Engine.swr.toFixed(1) : "–"), red: 2.5 }
        case "ALC":
            return { v: Engine.alcDbfs, min: -20, max: 0, live: tx && Engine.alcAvailable,
                     text: "ALC " + Engine.alcDbfs.toFixed(0) + " dB", red: -3 }
        case "Mic":
            return { v: Engine.micLevelDbfs, min: -40, max: 0, live: true,
                     text: "Mic " + Engine.micLevelDbfs.toFixed(0) + " dB", red: -3 }
        case "Comp":
            return { v: Engine.compressionDb, min: 0, max: 25, live: tx && Engine.procEnabled,
                     text: "Comp −" + Engine.compressionDb.toFixed(0) + " dB", red: 1e9 }
        case "PA temp":
            return { v: Engine.paTempC, min: 0, max: 120, live: Engine.hasPaTemp,
                     text: "PA " + Engine.paTempC.toFixed(0) + " °C", red: 70 }
        case "Volts":
            return { v: Engine.supplyVolts, min: 10, max: 16, live: Engine.hasSupplyVolts,
                     text: Engine.supplyVolts.toFixed(1) + " V", red: 15 }
        }
        return { v: 0, min: 0, max: 1, live: false, text: kind, red: 1e9 }
    }

    // Peak hold (the desktop meters' peak): the highest reading stays 1.5 s,
    // then falls back toward the current one.
    property real peak: reading.min
    property double peakHoldUntil: 0
    readonly property real currentValue: reading.live ? reading.v : reading.min
    onCurrentValueChanged: {
        if (currentValue >= peak) {
            peak = currentValue
            peakHoldUntil = Date.now() + 1500
        }
    }
    Timer {
        interval: 100
        repeat: true
        running: meter.reading.live && meter.peak > meter.currentValue
        onTriggered: {
            if (Date.now() < meter.peakHoldUntil) return
            const fall = (meter.reading.max - meter.reading.min) * 0.04
            meter.peak = Math.max(meter.currentValue, meter.peak - fall)
        }
    }

    AnalogGauge {
        id: gauge
        anchors.fill: parent
        visible: meter.analog
        peak: meter.peak
        compact: meter.compact
        kind: meter.kind
        value: meter.reading.v
        min: meter.reading.min
        max: meter.reading.max
        red: meter.reading.red
        live: meter.reading.live
        text: meter.reading.text
    }

    ColumnLayout {
        anchors.fill: parent
        visible: !meter.analog
        spacing: 2
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: meter.reading.live ? meter.reading.text : meter.kind + "  –"
                color: meter.reading.live && meter.reading.v >= meter.reading.red ? Theme.transmit : Theme.text
                font.pixelSize: meter.compact ? 12 : 15
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label { text: "▾"; color: Theme.textDim; font.pixelSize: meter.compact ? 11 : 13 }
        }
        Rectangle {
            Layout.fillWidth: true
            height: meter.compact ? 6 : 8
            radius: 3
            color: Theme.surfaceRaised
            Rectangle {
                height: parent.height
                radius: 3
                width: meter.reading.live
                       ? parent.width * Math.max(0, Math.min(1, (meter.reading.v - meter.reading.min)
                                                              / (meter.reading.max - meter.reading.min)))
                       : 0
                color: meter.reading.v >= meter.reading.red ? Theme.transmit : Theme.accent
            }
            // Peak hold tick.
            Rectangle {
                visible: meter.reading.live && meter.peak > meter.currentValue
                width: 2
                height: parent.height
                color: "#f0b429"
                x: Math.max(0, Math.min(parent.width - width, parent.width
                    * (meter.peak - meter.reading.min) / (meter.reading.max - meter.reading.min) - width / 2))
            }
        }
    }

    // Open the chooser right under this meter; the popup's margins keep it
    // inside the window (the right meter's opens leftwards).
    TapHandler { onTapped: chooser.open() }

    // Style and what the face shows in receive and in transmit, as the
    // app's buttons: the current choice of each is the dark (checked) one.
    Popup {
        id: chooser
        objectName: "meterChooser"
        parent: meter
        y: meter.height + 4
        margins: Theme.spacing
        padding: Theme.spacing
        modal: false
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.surfaceRaised
            radius: Theme.radius
        }
        readonly property real cellWidth: Math.max(Theme.buttonWidth * 0.9, 64)
        contentItem: ColumnLayout {
            spacing: Theme.spacing / 2
            Label { text: qsTr("Style"); color: Theme.textDim; font.pixelSize: Math.round(Theme.buttonFont * 0.85) }
            RowLayout {
                spacing: 4
                Repeater {
                    model: [{ label: qsTr("Bar"), analog: false }, { label: qsTr("Analog"), analog: true }]
                    delegate: AppButton {
                        required property var modelData
                        text: modelData.label
                        checked: (meter.side === "left" ? Engine.analogLeft : Engine.analogRight) === modelData.analog
                        focusPolicy: Qt.NoFocus
                        Layout.preferredWidth: chooser.cellWidth * 2 + 4
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: Engine.setMeterAnalog(meter.side, modelData.analog)
                    }
                }
            }
            Repeater {
                model: [{ title: qsTr("Receive"), tx: false }, { title: qsTr("Transmit"), tx: true }]
                delegate: ColumnLayout {
                    id: section
                    required property var modelData
                    spacing: Theme.spacing / 2
                    Label { text: section.modelData.title; color: Theme.textDim; font.pixelSize: Math.round(Theme.buttonFont * 0.85) }
                    GridLayout {
                        columns: 4
                        columnSpacing: 4
                        rowSpacing: 4
                        Repeater {
                            model: Engine.meterKinds
                            delegate: AppButton {
                                required property string modelData
                                text: modelData
                                checked: (section.modelData.tx ? meter.txKind : meter.rxKind) === modelData
                                focusPolicy: Qt.NoFocus
                                Layout.preferredWidth: chooser.cellWidth
                                Layout.preferredHeight: Theme.touchTarget
                                onClicked: Engine.setMeter(meter.side, section.modelData.tx, modelData)
                            }
                        }
                    }
                }
            }
        }
    }
}
