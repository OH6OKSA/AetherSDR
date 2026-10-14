import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AetherMobile

// Receive filter, right of the tuning lock, as on SmartSDR/AetherSDR: the
// button shows the width in use and the DSP that is on; tap it for the
// mode's eight widths (the active one checked) and the radio's DSP toggles.
Item {
    id: filters
    Layout.fillWidth: true
    implicitWidth: button.width
    implicitHeight: Theme.touchTarget

    readonly property real cellWidth: Math.max(Theme.buttonWidth * 0.8, 60)
    function label(hz) {
        if (hz >= 1000) {
            return (hz / 1000).toFixed(1).replace(/\.0$/, "") + "k"
        }
        return String(hz)
    }

    // "Filter ▼" over a small status line: the width in use and the DSP
    // that is on (e.g. "2.7 kHz · NB NR"). Opens the chooser below.
    readonly property var activeDsp: Engine.dspKeys.filter(k => Engine.dspOn(k))
    readonly property string status: {
        const parts = []
        if (Engine.filterPresets.length > 0 && Engine.filterWidth > 0) {
            const w = Engine.filterWidth
            parts.push(w >= 1000 ? (w / 1000).toFixed(1).replace(/\.0$/, "") + " kHz" : w + " Hz")
        }
        if (activeDsp.length > 0) {
            parts.push(activeDsp.join(" "))
        }
        return parts.join(" · ")
    }
    AppButton {
        id: button
        leftPadding: 8
        rightPadding: 8
        width: Math.max(Theme.touchTarget,
                        Math.min(Theme.buttonWidth * 2.2, content.implicitWidth + leftPadding + rightPadding))
        height: Theme.touchTarget
        focusPolicy: Qt.NoFocus
        onClicked: chooser.open()
        contentItem: Item {
            Column {
                id: content
                anchors.centerIn: parent
                width: Math.min(implicitWidth, button.availableWidth)
                spacing: 0
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 4
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Filter")
                        font.pixelSize: Math.round(Theme.buttonFont * 0.95)
                        color: "#1a1a1a"
                    }
                    Caret {
                        anchors.verticalCenter: parent.verticalCenter
                        size: Math.round(Theme.buttonFont * 0.7)
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(implicitWidth, button.availableWidth)
                    visible: text.length > 0
                    text: filters.status
                    font.pixelSize: Math.max(10, Math.round(Theme.buttonFont * 0.72))
                    color: "#3a3a3a"
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }

    Popup {
        id: chooser
        objectName: "filterChooser"
        parent: button
        y: button.height + 4
        margins: Theme.spacing
        padding: Theme.spacing
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.surfaceRaised
            radius: Theme.radius
        }
        contentItem: ColumnLayout {
            spacing: Theme.spacing / 2
            Label {
                visible: Engine.filterPresets.length > 0
                text: qsTr("%1 filter").arg(Engine.mode)
                color: Theme.textDim
                font.pixelSize: Math.round(Theme.buttonFont * 0.85)
            }
            GridLayout {
                columns: Math.min(4, Math.max(1, Engine.filterPresets.length))
                columnSpacing: 4
                rowSpacing: 4
                Repeater {
                    model: Engine.filterPresets
                    delegate: AppButton {
                        required property int modelData
                        text: filters.label(modelData)
                        checked: Engine.activeFilterPreset === modelData
                        focusPolicy: Qt.NoFocus
                        Layout.preferredWidth: filters.cellWidth
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: {
                            Engine.setFilterWidth(modelData)
                            chooser.close()
                        }
                    }
                }
            }
            Label {
                text: qsTr("DSP")
                color: Theme.textDim
                font.pixelSize: Math.round(Theme.buttonFont * 0.85)
            }
            // Toggles stay open: several are often set together.
            GridLayout {
                columns: 4
                columnSpacing: 4
                rowSpacing: 4
                Repeater {
                    model: Engine.dspKeys
                    delegate: AppButton {
                        required property string modelData
                        text: modelData
                        // Re-read on every DSP change (dspOn is a method).
                        checked: Engine.dspKeys.length >= 0 && Engine.dspOn(modelData)
                        focusPolicy: Qt.NoFocus
                        Layout.preferredWidth: filters.cellWidth
                        Layout.preferredHeight: Theme.touchTarget
                        onClicked: Engine.setDsp(modelData, !checked)
                    }
                }
            }
        }
    }
}
