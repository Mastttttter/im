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
    width: 460
    padding: 0

    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0

    function resetResult() {
        resultMessage = ""
        resultOk = false
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

    contentItem: ColumnLayout {
        spacing: 14

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            color: theme.panelAlt
            radius: 10

            Label {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 18
                text: "设置个性签名"
                color: theme.text
                font.pixelSize: 20
                font.bold: true
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 18
            spacing: 10

            Label {
                text: "请输入新的个性签名："
                color: theme.text
                font.bold: true
                Layout.fillWidth: true
            }

            TextArea {
                id: statusMessageField
                Layout.fillWidth: true
                Layout.preferredHeight: 118
                placeholderText: "留空可清除个签"
                wrapMode: TextArea.Wrap
                selectByMouse: true
                background: Rectangle {
                    color: theme.input
                    radius: 6
                    border.color: statusMessageField.activeFocus ? theme.accent : theme.border
                }
                Keys.onPressed: function(event) {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                        event.accepted = true
                        confirmButton.clicked()
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

            Rectangle {
                Layout.fillWidth: true
                radius: 6
                color: theme.panelAlt
                implicitHeight: hint.implicitHeight + 16
                Label {
                    id: hint
                    anchors.fill: parent
                    anchors.margins: 8
                    text: "提示：个签会写入 Tox 身份并随 savedata 保存。"
                    color: theme.muted
                    wrapMode: Text.WordWrap
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
                Item { Layout.fillWidth: true }
                Button {
                    text: "取消"
                    onClicked: root.close()
                }
                Button {
                    id: confirmButton
                    text: "确认"
                    highlighted: true
                    onClicked: {
                        const ok = controller.setStatusMessage(statusMessageField.text)
                        root.resultOk = ok
                        root.resultMessage = controller.profileMessage
                        if (ok)
                            root.close()
                    }
                }
            }
        }
    }
}
