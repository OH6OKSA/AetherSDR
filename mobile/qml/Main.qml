import QtQuick
import QtQuick.Controls
import AetherMobile

ApplicationWindow {
    id: window
    width: 1366
    height: 1024
    visible: true
    title: qsTr("AetherSDR")
    color: Theme.background

    // The page follows the connection: radio list until connected.
    Loader {
        anchors.fill: parent
        sourceComponent: Engine.connectionState === "connected" ? operatePage : connectPage
    }

    Component { id: connectPage; ConnectPage {} }
    Component { id: operatePage; OperatePage {} }
}
