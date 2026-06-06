import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

Dialog {
    id: root
    property var controller
    property var theme
    property int durationSeconds: 0

    title: controller.callVideoEnabled ? "视频呼叫中" : "呼叫中"
    modal: false
    standardButtons: Dialog.NoButton
    width: controller.callVideoEnabled ? 800 : 320
    height: controller.callVideoEnabled ? 600 : 220
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: 0

    background: Rectangle { color: theme.panel; border.color: theme.border; radius: controller.callVideoEnabled ? 0 : 10 }

    function durationText() {
        const minutes = Math.floor(durationSeconds / 60)
        const seconds = durationSeconds % 60
        return (minutes < 10 ? "0" : "") + minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    function callIsConnected() {
        return controller.callStatus === "通话中" || controller.callStatus === "视频通话中"
    }

    onOpened: {
        durationSeconds = 0
        durationTimer.restart()
        Qt.callLater(function() {
            if (callContent.item && callContent.item.attachVideoSinks)
                callContent.item.attachVideoSinks()
        })
    }
    onClosed: {
        durationTimer.stop()
        if (controller.callActive)
            controller.hangupCall()
        controller.setLocalVideoSink(null)
        controller.setRemoteVideoSink(null)
    }

    Timer {
        id: durationTimer
        interval: 1000
        repeat: true
        running: false
        onTriggered: if (root.callIsConnected()) durationSeconds += 1
    }

    Connections {
        target: controller
        function onCallStateChanged() {
            if (!controller.callActive && root.opened)
                root.close()
        }
    }

    Loader {
        id: callContent
        anchors.fill: parent
        sourceComponent: controller.callVideoEnabled ? videoCall : audioCall
        onLoaded: Qt.callLater(function() {
            if (callContent.item && callContent.item.attachVideoSinks)
                callContent.item.attachVideoSinks()
        })
    }

    Component {
        id: audioCall
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12
            Label {
                text: controller.callTitle
                color: theme.accent
                font.pixelSize: 20
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
            Label {
                text: controller.callStatus
                color: theme.muted
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
            Label {
                text: root.durationText()
                color: theme.accent
                font.pixelSize: 28
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: "接听"
                    visible: controller.callCanAnswer
                    Layout.fillWidth: true
                    onClicked: controller.answerCall()
                }
                Button {
                    text: controller.callCanAnswer ? "拒绝" : "挂断"
                    Layout.fillWidth: true
                    onClicked: controller.hangupCall()
                }
            }
        }
    }

    Component {
        id: videoCall
        Item {
            anchors.fill: parent

            function attachVideoSinks() {
                controller.setRemoteVideoSink(remoteVideo.videoSink)
                controller.setLocalVideoSink(localPreview.videoSink)
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: parent.height - 60
                color: "#0a0a14"
                border.color: theme.border

                VideoOutput {
                    id: remoteVideo
                    anchors.fill: parent
                    fillMode: VideoOutput.PreserveAspectFit
                    Component.onCompleted: controller.setRemoteVideoSink(videoSink)
                }

                Label {
                    anchors.centerIn: parent
                    text: controller.callStatus
                    color: theme.muted
                    visible: !root.callIsConnected()
                }

                Rectangle {
                    width: 200
                    height: 150
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 12
                    color: theme.window
                    border.color: theme.accent
                    border.width: 2
                    clip: true

                    VideoOutput {
                        id: localPreview
                        anchors.fill: parent
                        fillMode: VideoOutput.PreserveAspectCrop
                        Component.onCompleted: controller.setLocalVideoSink(videoSink)
                    }

                    Label {
                        anchors.centerIn: parent
                        text: "我"
                        color: theme.text
                        visible: !root.callIsConnected()
                    }
                }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 60
                color: theme.panel
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    Label { text: controller.callTitle; color: theme.accent; font.bold: true }
                    Label { text: controller.callStatus; color: theme.muted }
                    Label { text: root.durationText(); color: theme.accent; font.pixelSize: 16 }
                    Item { Layout.fillWidth: true }
                    Button { text: "接听"; visible: controller.callCanAnswer; onClicked: controller.answerCall() }
                    Button { text: controller.callCanAnswer ? "拒绝" : "挂断"; onClicked: controller.hangupCall() }
                }
            }
        }
    }
}
