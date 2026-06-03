import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root

    property var controller
    property var theme
    property string resultMessage: ""
    property bool resultOk: false

    title: "设置个性签名"
    modal: true
    standardButtons: Dialog.NoButton
    width: 440
    padding: 16

    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0

    function resetResult() {
        resultMessage = ""
        resultOk = false
    }

    function submit() {
        const ok = controller.setStatusMessage(statusMessageField.text)
        root.resultOk = ok
        root.resultMessage = controller.profileMessage
        if (ok)
            root.close()
    }

    onOpened: {
        resetResult()
        statusMessageField.text = controller ? controller.statusMessage : ""
        statusMessageField.forceActiveFocus()
        statusMessageField.cursorPosition = statusMessageField.length
    }

    background: Rectangle {
        color: theme.panel
        radius: 10
        border.color: theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            text: "请输入新的个性签名："
            color: theme.text
            font.bold: true
            Layout.fillWidth: true
        }

        TextArea {
            id: statusMessageField
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            color: theme.text
            selectedTextColor: "white"
            selectionColor: theme.accent
            placeholderText: ""
            wrapMode: TextArea.Wrap
            selectByMouse: true
            background: Rectangle {
                color: theme.input
                radius: 6
                border.color: statusMessageField.activeFocus ? theme.accent : theme.border
            }

            Label {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 12
                anchors.topMargin: 10
                text: "留空可清除个签"
                color: theme.muted
                visible: statusMessageField.text.length === 0
            }

            Keys.onPressed: function(event) {
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                    event.accepted = true
                    root.submit()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: statusMessageField.text.length + " 字符"
                color: theme.muted
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Label {
                text: "Ctrl+Enter 确认"
                color: theme.muted
                font.pixelSize: 12
            }
        }

        Label {
            text: root.resultMessage
            visible: text.length > 0
            color: root.resultOk ? theme.success : theme.danger
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true

            Item {
                Layout.fillWidth: true
            }

            Button {
                text: "取消"
                onClicked: root.close()
            }

            Button {
                text: "确认"
                highlighted: true
                onClicked: root.submit()
            }
        }
    }
}
