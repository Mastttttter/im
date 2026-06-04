import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    property var controller
    property var theme

    title: "删除好友"
    modal: true
    standardButtons: Dialog.NoButton
    width: 420
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: 16

    onOpened: {
        if (!controller || !controller.hasSelectedFriend) {
            root.close()
        }
    }

    background: Rectangle { color: theme.panel; radius: 10; border.color: theme.border }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            text: controller ? "确认删除好友 “" + controller.selectedFriendDisplayName + "”？" : "确认删除好友？"
            color: theme.text
            font.bold: true
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Label {
            text: "此操作只会从本地好友列表删除对方，不会通知对方；备注和历史记录会保留。"
            color: theme.muted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: "取消"; onClicked: root.close() }
            Button {
                text: "删除"
                highlighted: true
                onClicked: {
                    controller.deleteSelectedFriend()
                    root.close()
                }
            }
        }
    }
}
