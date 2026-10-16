import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// The tuning step (the desktop's STEP) as "STEP 500 Hz": tap to choose from
// the radio's step list. Tap-tune, the arrow keys and Pencil use it.
Item {
    id: stepButton
    objectName: "stepButton"
    property int fontSize: 16
    implicitWidth: row.implicitWidth + 12
    implicitHeight: Math.max(row.implicitHeight, Theme.touchTarget * 0.8)

    function label(hz) {
        return hz >= 1000 ? (hz / 1000).toFixed(hz % 1000 ? 1 : 0) + " kHz" : hz + " Hz"
    }

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: "#ffffff"
        opacity: tap.pressed ? 0.15 : 0
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        Label {
            text: qsTr("STEP")
            color: Theme.textDim
            font.pixelSize: stepButton.fontSize
            font.bold: true
        }
        Label {
            text: stepButton.label(Engine.stepHz)
            color: Theme.accent
            font.pixelSize: stepButton.fontSize
            font.bold: true
        }
    }
    TapHandler { id: tap; onTapped: stepChooser.open() }

    ChooserPopup {
        id: stepChooser
        objectName: "stepChooser"
        title: qsTr("Tuning step")
        options: Engine.stepList.map(hz => stepButton.label(hz))
        current: stepButton.label(Engine.stepHz)
        y: stepButton.height + 4
        onPicked: (name) => {
            const list = Engine.stepList
            for (let i = 0; i < list.length; ++i) {
                if (stepButton.label(list[i]) === name) {
                    Engine.setStepHz(list[i])
                }
            }
        }
    }
}
