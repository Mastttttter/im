import QtQuick
import QtQuick.Controls

Item {
    id: root
    required property string identifier
    required property string sender
    required property string text
    required property string timestamp
    required property bool outgoing
    required property bool system
    required property string messageType
    required property int progress
    required property string deliveryState
    property var theme

    height: bubble.height

    Rectangle {
        id: bubble
        width: root.system ? Math.min(root.width * 0.76, body.implicitWidth + 28)
                           : Math.min(root.width * 0.72, Math.max(180, body.implicitWidth + 28))
        height: column.implicitHeight + 18
        radius: 10
        color: root.system ? theme.systemBubble : (root.outgoing ? theme.outgoingBubble : theme.incomingBubble)
        border.color: theme.border
        anchors.right: root.outgoing && !root.system ? parent.right : undefined
        anchors.left: !root.outgoing && !root.system ? parent.left : undefined
        anchors.horizontalCenter: root.system ? parent.horizontalCenter : undefined

        Column {
            id: column
            anchors.fill: parent
            anchors.margins: 9
            spacing: 4

            Row {
                width: parent.width
                spacing: 8
                visible: !root.system
                Label {
                    text: root.sender
                    color: root.outgoing ? theme.accent : theme.muted
                    font.bold: true
                    font.pixelSize: 12
                }
                Label {
                    text: root.timestamp
                    color: theme.muted
                    font.pixelSize: 11
                }
                Label {
                    text: root.messageType === "file" || root.deliveryState === "sent" ? "" : root.deliveryState
                    color: root.deliveryState === "failed" ? theme.danger : theme.warning
                    font.pixelSize: 11
                }
            }

            Label {
                id: body
                width: Math.min(root.width * 0.72 - 18, implicitWidth)
                text: root.text
                color: theme.text
                wrapMode: Text.Wrap
                horizontalAlignment: root.system ? Text.AlignHCenter : Text.AlignLeft
            }

            ProgressBar {
                width: parent.width
                from: 0
                to: 100
                value: root.progress
                visible: root.messageType === "file" && root.progress >= 0 && root.progress < 100
                         && root.deliveryState !== "failed" && root.deliveryState !== "cancelled"
            }
        }
    }
}
