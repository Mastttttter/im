import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var controller
    property var theme
    signal addFriendRequested()
    signal deleteFriendRequested()
    signal editFriendRemarkRequested()

    radius: 8
    color: theme.panel
    border.color: theme.border

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            radius: 8
            color: controller.selectedConversationKind === "assistant" ? theme.accentSoft : theme.panelAlt
            border.color: theme.border
            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8
                Rectangle {
                    width: 34; height: 34; radius: 17
                    color: theme.accent
                    Label { anchors.centerIn: parent; text: "AI"; color: "white"; font.bold: true }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Label { text: "AI 助手"; color: theme.text; font.bold: true }
                    Label { text: "占位智能回复"; color: theme.muted; font.pixelSize: 12 }
                }
            }
            MouseArea { anchors.fill: parent; onClicked: controller.selectAssistant() }
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: "好友" }
            TabButton { text: "群聊" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ColumnLayout {
                spacing: 8
                ListView {
                    id: friendList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: friendModel
                    spacing: 4
                    delegate: ContactDelegate {
                        width: friendList.width
                        theme: root.theme
                        selected: controller.selectedConversationKind === "friend" && controller.selectedConversationIdentifier === identifier
                        onClicked: controller.selectFriend(identifier)
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: friendList.count === 0
                        text: "暂无好友"
                        color: theme.muted
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button { text: "添加"; Layout.fillWidth: true; onClicked: root.addFriendRequested() }
                    Button { text: "删除"; Layout.fillWidth: true; enabled: controller.hasSelectedFriend; onClicked: root.deleteFriendRequested() }
                    Button { text: "编辑备注"; Layout.fillWidth: true; enabled: controller.hasSelectedFriend; onClicked: root.editFriendRemarkRequested() }
                }
            }

            ColumnLayout {
                spacing: 8
                ListView {
                    id: groupList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: groupModel
                    spacing: 4
                    delegate: ContactDelegate {
                        width: groupList.width
                        theme: root.theme
                        selected: controller.selectedConversationKind === "group" && controller.selectedConversationIdentifier === identifier
                        onClicked: controller.selectGroup(identifier)
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: groupList.count === 0
                        text: "暂无群聊"
                        color: theme.muted
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button { text: "创建"; Layout.fillWidth: true; onClicked: controller.createGroup("群聊") }
                    Button { text: "邀请"; Layout.fillWidth: true; onClicked: controller.inviteSelectedFriendToGroup() }
                    Button { text: "退出"; Layout.fillWidth: true; onClicked: controller.leaveSelectedGroup() }
                }
            }
        }
    }
}
