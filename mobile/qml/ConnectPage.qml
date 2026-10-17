import QtQuick
import QtQuick.Controls
import AetherMobile
import QtQuick.Layouts

// Radio chooser: radios on this network, SmartLink (remote) radios, and a
// connect-by-address field. Scrolls so it fits a phone.
Page {
    id: page
    background: Rectangle { color: Theme.background }

    // One tappable radio row (LAN or SmartLink).
    // A radio to connect to: a raised card with an accent edge and a
    // Connect chip, so it reads as something to tap (a tester took the plain
    // row for a label).
    component RadioRow: ItemDelegate {
        id: row
        property string title
        property string subtitle
        Layout.fillWidth: true
        implicitHeight: 2 * Theme.touchTarget
        enabled: Engine.connectionState === "idle"
        background: Rectangle {
            color: row.down ? Qt.lighter(Theme.surfaceRaised, 1.3) : Theme.surfaceRaised
            radius: Theme.radius
            border.width: 2
            border.color: row.enabled ? Theme.accent : Theme.surface
            opacity: row.enabled ? 1.0 : 0.6
        }
        contentItem: RowLayout {
            spacing: Theme.spacing
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { text: row.title; color: Theme.text; font.pixelSize: 20; elide: Text.ElideRight; Layout.fillWidth: true }
                Label { text: row.subtitle; color: Theme.textDim; font.pixelSize: 15; elide: Text.ElideRight; Layout.fillWidth: true }
            }
            Rectangle {
                visible: row.enabled
                implicitWidth: connectChip.implicitWidth + 2 * Theme.spacing
                implicitHeight: Theme.touchTarget * 0.8
                radius: Theme.radius
                color: row.down ? "#bdbdbd" : "#e0e0e0"
                Label {
                    id: connectChip
                    anchors.centerIn: parent
                    text: qsTr("Connect ›")
                    color: "#202020"
                    font.pixelSize: 16
                }
            }
        }
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        // Clear of the notch / Dynamic Island and the home indicator.
        // The page's safe area, not the ScrollView's own: margins taken from
        // the item they move made a binding loop (logged on iPad).
        anchors.leftMargin: 2 * Theme.spacing + page.SafeArea.margins.left
        anchors.rightMargin: 2 * Theme.spacing + page.SafeArea.margins.right
        anchors.topMargin: 2 * Theme.spacing + page.SafeArea.margins.top
        anchors.bottomMargin: Theme.spacing + page.SafeArea.margins.bottom
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroll.availableWidth
            spacing: Theme.spacing

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("OksaSDR")
                    color: Theme.text
                    font.pixelSize: 34
                    font.bold: true
                    Layout.fillWidth: true
                }
                Label { text: "v" + Engine.appVersion; color: Theme.textDim; font.pixelSize: 13 }
                AppButton { text: qsTr("Manual"); onClicked: Qt.openUrlExternally(Engine.manualUrl) }
                AppButton { text: qsTr("Log"); onClicked: logPage.open() }
            }

            Label {
                Layout.fillWidth: true
                visible: Engine.connectionState !== "idle" || Engine.lastError.length > 0
                text: Engine.connectionState === "connecting" ? qsTr("Connecting…") : Engine.lastError
                color: Engine.connectionState === "connecting" ? Theme.textDim : Theme.warning
                font.pixelSize: 17
                wrapMode: Text.Wrap
            }

            // ---- This network ----
            Label {
                text: Engine.radios.count > 0 ? qsTr("On this network") : qsTr("Searching this network…")
                color: Theme.textDim
                font.pixelSize: 17
            }
            Repeater {
                model: Engine.radios
                delegate: RadioRow {
                    required property int index
                    required property string name
                    required property string nickname
                    required property string model
                    required property string address
                    required property string version
                    required property string transport
                    required property bool inUse
                    title: (nickname.length > 0 ? nickname + " — " : "") + (model.length > 0 ? model : name)
                    subtitle: transport === "sim"
                              ? qsTr("Built-in demo radio (receive only)")
                              : address + "   v" + version + (inUse ? qsTr("   in use") : "")
                    onClicked: Engine.connectToRadio(index)
                }
            }

            // ---- SmartLink (remote) ----
            Label {
                text: qsTr("SmartLink (remote)")
                color: Theme.textDim
                font.pixelSize: 17
                Layout.topMargin: Theme.spacing
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: smartLinkBox.implicitHeight + 2 * Theme.spacing
                radius: Theme.radius
                color: Theme.surface

                ColumnLayout {
                    id: smartLinkBox
                    anchors.fill: parent
                    anchors.margins: Theme.spacing
                    spacing: Theme.spacing / 2

                    // Signed out: e-mail and password (FlexRadio account).
                    ColumnLayout {
                        visible: Engine.smartLinkState === "signedOut"
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Sign in with your FlexRadio SmartLink account to use your radio from anywhere.")
                            color: Theme.textDim
                            font.pixelSize: 14
                            wrapMode: Text.Wrap
                        }
                        TextField {
                            id: emailField
                            Layout.fillWidth: true
                            text: Engine.smartLinkEmail
                            placeholderText: qsTr("E-mail")
                            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                            font.pixelSize: 17
                        }
                        TextField {
                            id: passwordField
                            Layout.fillWidth: true
                            placeholderText: qsTr("Password")
                            echoMode: TextInput.Password
                            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                            font.pixelSize: 17
                            onAccepted: signIn.clicked()
                        }
                        AppButton {
                            id: signIn
                            text: qsTr("Sign in to SmartLink")
                            Layout.fillWidth: true
                            enabled: emailField.text.length > 0 && passwordField.text.length > 0
                            onClicked: {
                                Engine.signInSmartLink(emailField.text, passwordField.text)
                                passwordField.text = ""
                            }
                        }
                    }

                    // Signing in.
                    RowLayout {
                        visible: Engine.smartLinkState === "signingIn"
                        BusyIndicator { running: visible; implicitWidth: 32; implicitHeight: 32 }
                        Label { text: qsTr("Signing in to SmartLink…"); color: Theme.textDim }
                    }

                    // Signed in: account, remote radios, sign out.
                    ColumnLayout {
                        visible: Engine.smartLinkState === "signedIn" || Engine.smartLinkState === "offline"
                        Layout.fillWidth: true
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: Engine.smartLinkAccount.length > 0 ? Engine.smartLinkAccount : qsTr("Signed in")
                                color: Theme.text
                                font.pixelSize: 16
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            AppButton { text: qsTr("Sign out"); onClicked: Engine.signOutSmartLink() }
                        }
                        Label {
                            visible: Engine.smartLinkState === "offline"
                            text: qsTr("SmartLink server not reachable. Retrying…")
                            color: Theme.warning
                            font.pixelSize: 14
                        }
                        Label {
                            visible: Engine.smartLinkState === "signedIn" && Engine.smartLinkRadios.length === 0
                            text: qsTr("No radios on this account are online.")
                            color: Theme.textDim
                            font.pixelSize: 14
                        }
                        Repeater {
                            model: Engine.smartLinkRadios
                            delegate: RadioRow {
                                required property int index
                                required property var modelData
                                title: (modelData.nickname.length > 0 ? modelData.nickname + " — " : "") + modelData.model
                                subtitle: modelData.status + (modelData.reachable ? "" : qsTr("   not reachable"))
                                enabled: Engine.connectionState === "idle" && modelData.reachable
                                onClicked: Engine.connectSmartLinkRadio(index)
                            }
                        }
                    }
                }
            }

            // ---- By address ----
            Label {
                text: qsTr("By IP address")
                color: Theme.textDim
                font.pixelSize: 17
                Layout.topMargin: Theme.spacing
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing
                // Opens the app's keypad: text fields did not take every
                // keyboard (a tester's iPad with a hardware keyboard).
                AppButton {
                    id: addressField
                    objectName: "addressField"
                    readonly property string address: Engine.lastAddress
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.touchTarget
                    text: address.length > 0 ? address : qsTr("Enter the radio's IP address")
                    font.pixelSize: 18
                    focusPolicy: Qt.NoFocus
                    onClicked: addressDialog.open()
                }
                AppButton {
                    text: qsTr("Connect")
                    Layout.preferredHeight: Theme.touchTarget
                    enabled: Engine.connectionState === "idle"
                    onClicked: addressField.address.length > 0 ? Engine.connectToAddress(addressField.address)
                                                               : addressDialog.open()
                }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Enter your own radio's IP address. SmartSDR's radio chooser and your router's device list both show it. This device must reach the radio on the same network or over a VPN. The address is remembered on this device only.")
                color: Theme.textDim
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }
        }
    }

    LogPage { id: logPage }
    AddressDialog {
        id: addressDialog
        onConnectRequested: (address) => Engine.connectToAddress(address)
    }
}
