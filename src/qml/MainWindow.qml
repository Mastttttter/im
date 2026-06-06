import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"

Item {
    id: root
    property var controller
    property var theme

    signal addFriendRequested()
    signal changePasswordRequested()
    signal noticeRequested()
    signal deleteFriendRequested()
    signal editFriendRemarkRequested()
    signal fileRequested()
    signal audioCallRequested()
    signal videoCallRequested()
    signal clearAssistantHistoryRequested()

    Rectangle {
        anchors.fill: parent
        color: theme.window
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        TopBar {
            Layout.fillWidth: true
            Layout.preferredHeight: parent.height*0.15
            controller: root.controller
            theme: root.theme
            onAddFriendRequested: root.addFriendRequested()
            onChangePasswordRequested: root.changePasswordRequested()
            onNoticeRequested: root.noticeRequested()
            onDeleteFriendRequested: root.deleteFriendRequested()
            onEditFriendRemarkRequested: root.editFriendRemarkRequested()
        }

        SplitView {
            Layout.fillWidth: true
            Layout.preferredHeight: parent.height*0.85
            orientation: Qt.Horizontal

            ContactPane {
                SplitView.preferredWidth: 260
                SplitView.minimumWidth: 220
                SplitView.maximumWidth: 360
                controller: root.controller
                theme: root.theme
                onAddFriendRequested: root.addFriendRequested()
                onDeleteFriendRequested: root.deleteFriendRequested()
                onEditFriendRemarkRequested: root.editFriendRemarkRequested()
            }

            ChatPane {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 520
                controller: root.controller
                theme: root.theme
                onFileRequested: root.fileRequested()
                onAudioCallRequested: root.audioCallRequested()
                onVideoCallRequested: root.videoCallRequested()
                onClearAssistantHistoryRequested: root.clearAssistantHistoryRequested()
            }
        }
    }
}
