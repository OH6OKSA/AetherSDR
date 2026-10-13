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
    implicitHeight: compact ? 34 : 44

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

    ColumnLayout {
        anchors.fill: parent
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
        }
    }

    // Open the chooser right under this meter (popup() alone opens at the
    // last pointer position, which on touch can be anywhere).
    TapHandler { onTapped: chooser.popup(meter, 0, meter.height) }

    // Two columns of choices: what the face shows in receive and in
    // transmit. A tick marks the current choice of each.
    Menu {
        id: chooser
        MenuSeparator { contentItem: Label { text: qsTr("Receive"); color: Theme.textDim; font.pixelSize: 13; padding: 6 } }
        Repeater {
            model: Engine.meterKinds
            delegate: MenuItem {
                required property string modelData
                text: (meter.rxKind === modelData ? "✓  " : "     ") + modelData
                font.bold: meter.rxKind === modelData
                onTriggered: Engine.setMeter(meter.side, false, modelData)
            }
        }
        MenuSeparator { contentItem: Label { text: qsTr("Transmit"); color: Theme.textDim; font.pixelSize: 13; padding: 6 } }
        Repeater {
            model: Engine.meterKinds
            delegate: MenuItem {
                required property string modelData
                text: (meter.txKind === modelData ? "✓  " : "     ") + modelData
                font.bold: meter.txKind === modelData
                onTriggered: Engine.setMeter(meter.side, true, modelData)
            }
        }
    }
}
