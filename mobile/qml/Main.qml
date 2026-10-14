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
    // Adaptive metrics follow the window (Theme.qml).
    Binding { target: Theme; property: "windowWidth"; value: window.width }
    Binding { target: Theme; property: "windowHeight"; value: window.height }
    readonly property bool phoneSized: Theme.phoneLayout

    // The page follows the connection: radio list until connected.
    Loader {
        anchors.fill: parent
        sourceComponent: Engine.connectionState !== "connected" ? connectPage
                       : window.phoneSized ? phoneOperatePage : operatePage
    }

    // A SmartLink radio presented a different TLS certificate than the one
    // pinned for it on first use. The connect waits for the operator.
    Dialog {
        id: certDialog
        property string host
        property string expected
        property string presented
        modal: true
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 24)
        // Fixed size: a wrapped label inside an auto-sized dialog loops.
        height: Math.min(340, parent.height - 24)
        title: qsTr("Radio certificate changed")
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label {
            // From the dialog's fixed width, not availableWidth, which would
            // feed the wrapped height back into the dialog's own size.
            width: certDialog.width - certDialog.leftPadding - certDialog.rightPadding
            wrapMode: Text.Wrap
            text: qsTr("The radio at %1 now presents a different security certificate than before. This is normal after a firmware update or reset, but it can also mean someone is intercepting the connection.\n\nTrust the new certificate and connect?").arg(certDialog.host)
        }
        onAccepted: Engine.answerCertificate(true)
        onRejected: Engine.answerCertificate(false)
    }
    Connections {
        target: Engine
        function onCertificateChanged(host, expected, presented) {
            certDialog.host = host
            certDialog.expected = expected
            certDialog.presented = presented
            certDialog.open()
        }
    }

    Component { id: connectPage; ConnectPage {} }
    Component { id: operatePage; OperatePage {} }
    Component { id: phoneOperatePage; PhoneOperatePage {} }
}
