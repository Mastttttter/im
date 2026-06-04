import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "components"
import "dialogs"
import "style"

ApplicationWindow {
    id: root
    width: 1000
    height: 650
    minimumWidth: 900
    minimumHeight: 580
    visible: true
    title: "IM"

    Theme {
        id: theme
        dark: appController.darkTheme
    }

    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    color: theme.window

    LoginPage {
        id: loginPage
        anchors.fill: parent
        visible: !appController.loggedIn
        controller: appController
        onChangePasswordRequested: function(account) {
            changePasswordDialog.account = account
            changePasswordDialog.open()
        }
    }

    MainWindow {
        anchors.fill: parent
        visible: appController.loggedIn
        controller: appController
        theme: theme
        onAddFriendRequested: addFriendDialog.open()
        onDeleteFriendRequested: deleteFriendDialog.open()
        onEditFriendRemarkRequested: editFriendRemarkDialog.open()
        onChangePasswordRequested: {
            changePasswordDialog.account = appController.accountName
            changePasswordDialog.open()
        }
        onNoticeRequested: {
            noticeCenter.open()
            appController.markNoticesRead()
        }
        onAudioCallRequested: appController.startAudioCall()
        onVideoCallRequested: appController.startVideoCall()
        onFileRequested: appController.sendFileStub()
    }

    AddFriendDialog {
        id: addFriendDialog
        controller: appController
        theme: theme
    }

    IncomingFriendRequestDialog {
        id: incomingFriendRequestDialog
        controller: appController
        theme: theme
    }

    DeleteFriendDialog {
        id: deleteFriendDialog
        controller: appController
        theme: theme
    }

    EditFriendRemarkDialog {
        id: editFriendRemarkDialog
        controller: appController
        theme: theme
    }

    ChangePasswordDialog {
        id: changePasswordDialog
        controller: appController
        theme: theme
    }

    NoticeCenter {
        id: noticeCenter
        controller: appController
        theme: theme
        model: noticeModel
        onClosed: appController.markNoticesRead()
    }

    CallWindow {
        id: callWindow
        controller: appController
        theme: theme
    }

    Connections {
        target: appController
        function onCallShellRequested() {
            callWindow.open()
        }
        function onFriendRequestPromptRequested() {
            if (appController.hasPendingFriendRequest) {
                incomingFriendRequestDialog.open()
            }
        }
        function onPendingFriendRequestChanged() {
            if (appController.hasPendingFriendRequest && !incomingFriendRequestDialog.visible) {
                incomingFriendRequestDialog.open()
            }
        }
    }
}
