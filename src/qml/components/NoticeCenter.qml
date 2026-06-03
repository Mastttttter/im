import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    property var controller
    property var theme
    property var model

    modal: false
    focus: true
    width: 560
    height: 420
    x: parent ? parent.width - width - 24 : 0
    y: 88
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function severityColor(severity) {
        if (severity === "error") return theme.danger
        if (severity === "warning") return theme.warning
        return theme.text
    }

    background: Rectangle {
        color: theme.panel
        radius: 10
        border.color: theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "通知中心"
                color: theme.text
                font.pixelSize: 18
                font.bold: true
                Layout.fillWidth: true
            }
            Button { text: "关闭"; onClicked: root.close() }
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: "日志" }
            TabButton { text: "配置" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            Rectangle {
                color: theme.chatBackground
                radius: 6
                border.color: theme.border
                ListView {
                    id: noticeList
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    spacing: 4
                    model: root.model
                    delegate: Rectangle {
                        id: noticeRow
                        required property string timestamp
                        required property string category
                        required property string text
                        required property string severity
                        width: noticeList.width
                        implicitHeight: noticeText.implicitHeight + 12
                        color: "transparent"
                        Label {
                            id: noticeText
                            anchors.fill: parent
                            anchors.margins: 6
                            text: "[" + noticeRow.timestamp + "] " + noticeRow.category + ": " + noticeRow.text
                            color: root.severityColor(noticeRow.severity)
                            wrapMode: Text.Wrap
                            font.family: "Consolas, Courier New, monospace"
                            font.pixelSize: 12
                        }
                    }
                    onCountChanged: Qt.callLater(positionViewAtEnd)
                }
            }

            Rectangle {
                color: theme.chatBackground
                radius: 6
                border.color: theme.border
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 8
                    Label { text: "状态"; color: theme.accent; font.bold: true }
                    Label { text: controller.networkStatus; color: theme.text }
                    Label { text: "当前账号：" + controller.accountName; color: theme.text }
                    Label { text: "当前会话：" + controller.selectedConversationTitle; color: theme.text }
                    Label { text: "音视频配置：占位 shell，真实采集与编码稍后接入。"; color: theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Label { text: "文件传输：占位 send/receive flow，真实断点续传稍后接入。"; color: theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Label { text: "AI 助手：占位回复服务，真实 provider 配置稍后接入。"; color: theme.text; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
