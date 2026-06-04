import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    property var controller
    property var theme

    title: "好友请求"
    modal: true
    standardButtons: Dialog.NoButton
    width: 460
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: 16

    onOpened: {
        if (!controller || !controller.hasPendingFriendRequest) {
            root.close()
        }
    }

    background: Rectangle { color: theme.panel; radius: 10; border.color: theme.border }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            text: "收到新的好友请求"
            color: theme.text
            font.bold: true
        }

        Label {
            text: "公钥"
            color: theme.muted
        }
        TextArea {
            Layout.fillWidth: true
            Layout.preferredHeight: 78
            text: controller ? controller.pendingFriendRequestPublicKey : ""
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.WrapAnywhere
        }

        Label {
            text: "附言"
            color: theme.muted
        }
        TextArea {
            Layout.fillWidth: true
            Layout.preferredHeight: 90
            text: controller && controller.pendingFriendRequestMessage.length > 0
                  ? controller.pendingFriendRequestMessage
                  : "对方没有填写附言。"
            readOnly: true
            selectByMouse: true
            wrapMode: TextArea.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                text: "拒绝"
                onClicked: {
                    if (controller.rejectPendingFriendRequest()) {
                        root.close()
                        if (controller.hasPendingFriendRequest) {
                            Qt.callLater(function() { root.open() })
                        }
                    }
                }
            }
            Button {
                text: "同意"
                highlighted: true
                onClicked: {
                    if (controller.acceptPendingFriendRequest()) {
                        root.close()
                        if (controller.hasPendingFriendRequest) {
                            Qt.callLater(function() { root.open() })
                        }
                    }
                }
            }
        }
    }
}
