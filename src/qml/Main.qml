import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
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
    property string incomingFileSender: ""
    property string incomingFileName: ""
    property string incomingFileSizeText: ""
    property url incomingFileSuggestedUrl: ""

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
        onFileRequested: sendFileDialog.open()
    }

    FileDialog {
        id: sendFileDialog
        title: "选择要发送的文件"
        fileMode: FileDialog.OpenFile
        onAccepted: appController.sendFile(selectedFile.toString())
    }

    FileDialog {
        id: incomingFileDialog
        title: root.incomingFileName.length > 0
               ? "保存来自 " + root.incomingFileSender + " 的文件：" + root.incomingFileName
               : "保存收到的文件"
        fileMode: FileDialog.SaveFile
        currentFile: root.incomingFileSuggestedUrl
        onAccepted: appController.acceptIncomingFile(selectedFile.toString())
        onRejected: appController.rejectIncomingFile()
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
        function onIncomingFileSaveRequested(senderName, fileName, fileSizeText, suggestedFileUrl) {
            root.incomingFileSender = senderName
            root.incomingFileName = fileName
            root.incomingFileSizeText = fileSizeText
            root.incomingFileSuggestedUrl = suggestedFileUrl
            Qt.callLater(incomingFileDialog.open)
        }
    }
}
