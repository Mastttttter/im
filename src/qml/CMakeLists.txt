qt_add_qml_module(im
    URI im
    VERSION 1.0
    NO_GENERATE_QMLTYPES
    QML_FILES
        Main.qml
        LoginPage.qml
        MainWindow.qml
        components/TopBar.qml
        components/ContactPane.qml
        components/ContactDelegate.qml
        components/ChatPane.qml
        components/ChatBubble.qml
        components/MessageComposer.qml
        components/NoticeCenter.qml
        dialogs/AddFriendDialog.qml
        dialogs/ChangePasswordDialog.qml
        dialogs/CallWindow.qml
        style/Theme.qml
)
