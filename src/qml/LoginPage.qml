import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root
    property var controller
    signal changePasswordRequested(string account)

    readonly property string accountText: accountBox.editable ? accountBox.editText.trim() : accountBox.currentText.trim()
    readonly property bool knownAccount: controller ? controller.isKnownAccount(accountText) : false

    Image {
        anchors.fill: parent
        source: "qrc:/login_background.svg"
        fillMode: Image.PreserveAspectCrop
        smooth: true
    }

    Rectangle {
        anchors.fill: parent
        color: "#66000000"
    }

    Rectangle {
        id: card
        width: Math.min(520, parent.width - 80)
        radius: 18
        color: "#66000000"
        border.color: "#55ffffff"
        anchors.centerIn: parent
        implicitHeight: content.implicitHeight + 52

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 26
            spacing: 14

            Label {
                text: "欢迎使用 IM"
                color: "#ffffff"
                font.pixelSize: 28
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }

            Item { Layout.preferredHeight: 10 }

            Label { text: "账号："; color: "#ffffff"; font.bold: true }
            ComboBox {
                id: accountBox
                editable: true
                model: controller ? controller.knownAccounts : []
                Layout.fillWidth: true
                Layout.preferredHeight: 42
                Component.onCompleted: if (count === 0) editText = ""
            }

            Label { text: "密码："; color: "#ffffff"; font.bold: true }
            TextField {
                id: passwordField
                echoMode: TextInput.Password
                Layout.fillWidth: true
                Layout.preferredHeight: 42
                Keys.onReturnPressed: submitButton.clicked()
            }

            Label {
                text: "确认密码："
                color: "#ffffff"
                font.bold: true
                visible: !root.knownAccount
                Layout.fillWidth: true
            }
            TextField {
                id: confirmPasswordField
                echoMode: TextInput.Password
                placeholderText: "再次输入密码"
                visible: !root.knownAccount
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 42 : 0
                Keys.onReturnPressed: submitButton.clicked()
            }

            Rectangle {
                Layout.fillWidth: true
                radius: 8
                color: "#88000000"
                implicitHeight: hint.implicitHeight + 18
                Label {
                    id: hint
                    anchors.fill: parent
                    anchors.margins: 9
                    color: "#ffffff"
                    wrapMode: Text.WordWrap
                    text: "⚠️ 重要提示：\n• 账号仅允许英文+数字+下划线，不少于5个字符，不多于10个字符\n• 密码仅支持英文+数字，不得少于8个字符\n• 本地不存储密码，忘记密码将导致数据永久丢失"
                }
            }

            Label {
                text: controller ? controller.profileMessage : ""
                visible: text.length > 0
                color: "#ffffff"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Button {
                    text: "更换密码"
                    visible: root.knownAccount
                    highlighted: true
                    Material.background: "#ff9800"
                    onClicked: root.changePasswordRequested(root.accountText)
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: submitButton
                    text: "登录/注册"
                    highlighted: true
                    onClicked: controller.loginOrRegister(root.accountText, passwordField.text, confirmPasswordField.text, !root.knownAccount)
                }
                Button {
                    text: "退出"
                    onClicked: controller.exitApplication()
                }
            }
        }
    }
}
