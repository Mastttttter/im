import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var controller
    property var theme
    signal fileRequested()
    signal audioCallRequested()
    signal videoCallRequested()

    radius: 8
    color: theme.panel
    border.color: theme.border

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: controller.selectedConversationTitle
                color: theme.text
                font.pixelSize: 18
                font.bold: true
                Layout.fillWidth: true
            }
            Button { text: "发送文件"; onClicked: root.fileRequested() }
            Button { text: "音频通话"; onClicked: root.audioCallRequested() }
            Button { text: "视频通话"; onClicked: root.videoCallRequested() }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 6
            color: theme.chatBackground
            border.color: theme.border

            ListView {
                id: messageList
                anchors.fill: parent
                anchors.margins: 10
                clip: true
                spacing: 8
                model: chatModel
                delegate: ChatBubble {
                    width: messageList.width
                    theme: root.theme
                }
                onCountChanged: Qt.callLater(positionViewAtEnd)
            }

            Label {
                anchors.centerIn: parent
                visible: messageList.count === 0
                text: "选择好友或群聊后开始聊天"
                color: theme.muted
            }
        }

        MessageComposer {
            Layout.fillWidth: true
            theme: root.theme
            onSendRequested: function(text) {
                controller.sendMessage(text)
            }
        }
    }
}
