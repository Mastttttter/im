import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    property var controller
    property var theme
    property string account: ""

    title: "更换密码"
    modal: true
    standardButtons: Dialog.NoButton
    width: 420
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: 16

    background: Rectangle { color: theme.panel; radius: 10; border.color: theme.border }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label { text: "账号：" + (root.account.length > 0 ? root.account : "未选择"); color: theme.text; font.bold: true }
        TextField { id: oldPassword; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "请输入当前密码" }
        TextField { id: newPassword; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "请输入新密码（至少8个字符）" }
        TextField { id: confirmPassword; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "请再次输入新密码" }

        Rectangle {
            Layout.fillWidth: true
            radius: 6
            color: theme.panelAlt
            implicitHeight: hint.implicitHeight + 16
            Label {
                id: hint
                anchors.fill: parent
                anchors.margins: 8
                text: "⚠️ 提示：\n• 密码仅支持英文+数字，不得少于8个字符\n• 新密码不能与当前密码相同"
                color: theme.muted
                wrapMode: Text.WordWrap
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: "取消"; onClicked: root.close() }
            Button {
                text: "确定"
                highlighted: true
                onClicked: {
                    controller.changePassword(root.account, oldPassword.text, newPassword.text, confirmPassword.text)
                    oldPassword.text = ""
                    newPassword.text = ""
                    confirmPassword.text = ""
                    root.close()
                }
            }
        }
    }
}
