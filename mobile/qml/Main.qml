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

    // Phone-sized windows (iPhone, folded iPhone Duo, narrow Split View)
    // get the phone layout; it switches live on rotation or fold/unfold.
    readonly property bool phoneSized: width < 700 || height < 500

    // The page follows the connection: radio list until connected.
    Loader {
        anchors.fill: parent
        sourceComponent: Engine.connectionState !== "connected" ? connectPage
                       : window.phoneSized ? phoneOperatePage : operatePage
    }

    Component { id: connectPage; ConnectPage {} }
    Component { id: operatePage; OperatePage {} }
    Component { id: phoneOperatePage; PhoneOperatePage {} }
}
