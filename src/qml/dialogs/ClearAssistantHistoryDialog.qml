import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    property var controller
    property var theme

    title: "清空 AI 助手历史"
    modal: true
    standardButtons: Dialog.NoButton
    width: 420
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: 16

    onOpened: {
        if (!controller || controller.selectedConversationKind !== "assistant" || controller.aiBusy) {
            root.close()
        }
    }

    background: Rectangle { color: theme.panel; radius: 10; border.color: theme.border }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            text: "确认清空 AI 助手历史？"
            color: theme.text
            font.bold: true
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Label {
            text: "此操作会删除本地可见历史和加密数据库中的 AI 助手消息，不会影响 AI 设置。"
            color: theme.muted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: "取消"; onClicked: root.close() }
            Button {
                text: "清空"
                highlighted: true
                enabled: controller && !controller.aiBusy
                onClicked: {
                    controller.clearAssistantHistory()
                    root.close()
                }
            }
        }
    }
}
