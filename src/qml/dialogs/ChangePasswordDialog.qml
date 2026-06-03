import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    property var controller
    property var theme
    property string account: ""
    property string resultMessage: ""
    property bool resultOk: false

    title: "更换密码"
    modal: true
    standardButtons: Dialog.NoButton
    width: 420
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: 16

    function resetFields() {
        oldPassword.text = ""
        newPassword.text = ""
        confirmPassword.text = ""
        resultMessage = ""
        resultOk = false
    }

    onOpened: {
        resultMessage = ""
        resultOk = false
        oldPassword.forceActiveFocus()
    }

    background: Rectangle { color: theme.panel; radius: 10; border.color: theme.border }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label { text: "账号：" + (root.account.length > 0 ? root.account : "未选择"); color: theme.text; font.bold: true }
        TextField { id: oldPassword; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "请输入当前密码"; Keys.onReturnPressed: confirmButton.clicked() }
        TextField { id: newPassword; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "请输入新密码（至少8个字符）"; Keys.onReturnPressed: confirmButton.clicked() }
        TextField { id: confirmPassword; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "请再次输入新密码"; Keys.onReturnPressed: confirmButton.clicked() }

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

        Label {
            text: root.resultMessage
            visible: text.length > 0
            color: root.resultOk ? theme.success : theme.danger
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: "取消"; onClicked: { root.resetFields(); root.close() } }
            Button {
                id: confirmButton
                text: "确定"
                highlighted: true
                onClicked: {
                    const ok = controller.changePassword(root.account, oldPassword.text, newPassword.text, confirmPassword.text)
                    root.resultOk = ok
                    root.resultMessage = controller.profileMessage
                    if (ok) {
                        root.resetFields()
                        root.close()
                    }
                }
            }
        }
    }
}
