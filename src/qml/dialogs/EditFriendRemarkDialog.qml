import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    property var controller
    property var theme

    title: "编辑备注"
    modal: true
    standardButtons: Dialog.NoButton
    width: 420
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: 16

    onOpened: {
        if (!controller || !controller.hasSelectedFriend) {
            root.close()
        } else {
            remarkField.text = controller.selectedFriendRemark
            remarkField.forceActiveFocus()
            remarkField.selectAll()
        }
    }

    background: Rectangle { color: theme.panel; radius: 10; border.color: theme.border }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            text: controller ? "好友：" + controller.selectedFriendDisplayName : "好友"
            color: theme.text
            font.bold: true
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        TextField {
            id: remarkField
            Layout.fillWidth: true
            placeholderText: "备注名；留空则清除备注"
            selectByMouse: true
            onAccepted: {
                if (controller.setSelectedFriendRemark(remarkField.text)) {
                    root.close()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: "取消"; onClicked: root.close() }
            Button {
                text: "保存"
                highlighted: true
                onClicked: {
                    if (controller.setSelectedFriendRemark(remarkField.text)) {
                        root.close()
                    }
                }
            }
        }
    }
}
