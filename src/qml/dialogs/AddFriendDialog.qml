import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    property var controller
    property var theme

    title: "添加好友"
    modal: true
    standardButtons: Dialog.NoButton
    width: 440
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: 16

    background: Rectangle { color: theme.panel; radius: 10; border.color: theme.border }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label { text: "ToxID："; color: theme.text; font.bold: true }
        TextField {
            id: toxIdField
            Layout.fillWidth: true
            placeholderText: "76 位 ToxID；无效输入会创建界面占位好友"
        }

        Label { text: "附言："; color: theme.text; font.bold: true }
        TextArea {
            id: messageField
            Layout.fillWidth: true
            Layout.preferredHeight: 90
            placeholderText: "好友请求附言（可选）"
            text: controller && controller.accountName.length > 0 ? "您好，我是" + controller.accountName : "您好"
            wrapMode: TextArea.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: "取消"; onClicked: root.close() }
            Button {
                text: "确认"
                highlighted: true
                onClicked: {
                    controller.addFriend(toxIdField.text, messageField.text)
                    toxIdField.text = ""
                    root.close()
                }
            }
        }
    }
}
