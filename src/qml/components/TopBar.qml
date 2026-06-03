import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var controller
    property var theme
    signal addFriendRequested()
    signal changePasswordRequested()
    signal noticeRequested()

    radius: 8
    color: theme.window
    border.color: "transparent"
    implicitHeight: 126

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Label {
                text: controller.accountName
                color: theme.accent
                font.pixelSize: 20
                font.bold: true
                Layout.fillWidth: true
            }

            Button {
                text: controller.noticeUnread > 0 ? "通知(" + Math.min(controller.noticeUnread, 99) + ")" : "通知"
                onClicked: root.noticeRequested()
            }
            Button {
                width: 36
                height: 36
                icon.source: controller.darkTheme ? "qrc:/moon.svg" : "qrc:/sun.svg"
                icon.width: 24
                icon.height: 24
                text: ""
                onClicked: controller.toggleTheme()
                ToolTip.visible: hovered
                ToolTip.text: controller.darkTheme ? "切换到浅色主题" : "切换到深色主题"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label { text: "ToxID："; color: theme.muted }
            TextField {
                text: controller.selfToxId
                readOnly: true
                selectByMouse: true
                color: theme.muted
                background: Rectangle { color: "transparent" }
                font.pixelSize: 12
                Layout.fillWidth: true
                Layout.minimumWidth: 420
            }
            Button { text: "复制"; onClicked: controller.copySelfId() }
            Label { text: controller.networkStatus; color: theme.muted; Layout.preferredWidth: 140 }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                text: controller.statusMessage
                placeholderText: "点击设置个签"
                color: theme.muted
                background: Rectangle { color: "transparent" }
                Layout.preferredWidth: 190
                onEditingFinished: controller.setStatusMessage(text)
            }
            Button { text: "添加好友"; onClicked: root.addFriendRequested() }
            Button { text: "更换密码"; onClicked: root.changePasswordRequested() }
            Button { text: "删除好友"; onClicked: controller.deleteSelectedFriend() }
            Button { text: "创建群聊"; onClicked: controller.createGroup("群聊") }
            Button { text: "邀请入群"; onClicked: controller.inviteSelectedFriendToGroup() }
            Button { text: "退出群聊"; onClicked: controller.leaveSelectedGroup() }
            Item { Layout.fillWidth: true }
        }
    }
}
