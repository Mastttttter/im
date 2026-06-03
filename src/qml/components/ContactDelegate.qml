import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    required property string identifier
    required property string displayName
    required property string subtitle
    required property int onlineState
    required property int unreadCount
    required property string avatarText
    required property string publicKey
    required property bool aiAssistant
    property var theme
    property bool selected: false
    signal clicked()

    height: 58
    radius: 8
    color: selected ? theme.accentSoft : theme.panelAlt
    border.color: selected ? theme.accent : theme.border

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        Rectangle {
            width: 36
            height: 36
            radius: 18
            color: onlineState > 0 ? theme.accent : theme.border
            Label {
                anchors.centerIn: parent
                text: avatarText
                color: onlineState > 0 ? "white" : theme.muted
                font.bold: true
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label {
                text: displayName
                color: theme.text
                elide: Text.ElideRight
                font.bold: selected
                Layout.fillWidth: true
            }
            Label {
                text: subtitle
                color: theme.muted
                elide: Text.ElideRight
                font.pixelSize: 12
                Layout.fillWidth: true
            }
        }

        Rectangle {
            visible: unreadCount > 0
            width: Math.max(22, unreadLabel.implicitWidth + 10)
            height: 22
            radius: 11
            color: theme.danger
            Label {
                id: unreadLabel
                anchors.centerIn: parent
                text: unreadCount > 99 ? "99+" : unreadCount
                color: "white"
                font.pixelSize: 11
                font.bold: true
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
