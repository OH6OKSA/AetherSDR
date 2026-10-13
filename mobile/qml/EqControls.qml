import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The radio's 8-band equalizer, RX or TX (as SmartSDR's EQ panel):
// on/off, −10…+10 dB per band, Flat resets.
ColumnLayout {
    id: eq
    spacing: Theme.spacing / 2
    implicitWidth: 200
    property bool tx: false
    readonly property var bands: tx ? Engine.eqTxBands : Engine.eqRxBands
    readonly property bool on: tx ? Engine.eqTxEnabled : Engine.eqRxEnabled

    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Equalizer"); color: Theme.textDim; font.pixelSize: 16; Layout.fillWidth: true }
        AppButton { text: qsTr("RX"); checked: !eq.tx; focusPolicy: Qt.NoFocus; onClicked: eq.tx = false }
        AppButton { text: qsTr("TX"); checked: eq.tx; focusPolicy: Qt.NoFocus; onClicked: eq.tx = true }
    }
    RowLayout {
        Layout.fillWidth: true
        AppButton {
            text: eq.on ? qsTr("EQ on") : qsTr("EQ off")
            checked: eq.on
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.setEqEnabled(eq.tx, !eq.on)
        }
        AppButton {
            text: qsTr("Flat")
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            onClicked: Engine.resetEq(eq.tx)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 0
        opacity: eq.on ? 1 : 0.5
        Repeater {
            model: Engine.eqBandLabels
            // An Item that fills its share, with the band centred in it: a
            // ColumnLayout alone cannot grow past its children's widths.
            delegate: Item {
                id: band
                required property int index
                required property string modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: bandColumn.implicitHeight
                ColumnLayout {
                    id: bandColumn
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 2
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: (eq.bands[band.index] > 0 ? "+" : "") + eq.bands[band.index]
                        color: Theme.text
                        font.pixelSize: 12
                    }
                    Slider {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredHeight: 140
                        orientation: Qt.Vertical
                        from: -10; to: 10; stepSize: 1
                        value: eq.bands[band.index]
                        onMoved: Engine.setEqBand(eq.tx, band.index, Math.round(value))
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: band.modelData
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
}
