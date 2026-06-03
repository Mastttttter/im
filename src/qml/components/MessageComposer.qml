import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var theme
    signal sendRequested(string text)

    radius: 6
    color: theme.panel
    border.color: theme.border
    implicitHeight: 76

    function sendAndClear() {
        const body = editor.text.trim()
        if (body.length === 0)
            return
        root.sendRequested(editor.text)
        editor.text = ""
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 8

        TextArea {
            id: editor
            Layout.fillWidth: true
            Layout.fillHeight: true
            placeholderText: "输入消息，回车发送，Shift+回车换行"
            wrapMode: TextArea.Wrap
            Keys.onPressed: function(event) {
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) {
                    event.accepted = true
                    root.sendAndClear()
                }
            }
        }

        Button {
            text: "发送"
            highlighted: true
            Layout.preferredWidth: 82
            Layout.fillHeight: true
            onClicked: root.sendAndClear()
        }
    }
}
