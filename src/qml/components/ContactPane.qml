import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var controller
    property var theme
    signal addFriendRequested()
    signal deleteFriendRequested()
    signal editFriendRemarkRequested()

    radius: 8
    color: theme.panel
    border.color: theme.border

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        Rectangle {
            id: assistantTile
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            radius: 8
            color: controller.selectedConversationKind === "assistant" ? theme.accentSoft : theme.panelAlt
            border.color: controller.selectedConversationKind === "assistant" ? theme.accent : theme.border

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: controller.selectAssistant()
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Rectangle {
                    width: 34
                    height: 34
                    radius: 17
                    color: theme.accent
                    Label {
                        anchors.centerIn: parent
                        text: "AI"
                        color: "white"
                        font.bold: true
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Label {
                        text: "AI 助手"
                        color: theme.text
                        font.bold: true
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Label {
                        text: controller.aiBusy
                              ? "正在生成回复…"
                              : (controller.aiApiKeyConfigured
                                 ? ((controller.aiProvider.length > 0 ? controller.aiProvider + " · " : "") + controller.aiModelName)
                                 : "未配置 API Key")
                        color: theme.muted
                        font.pixelSize: 12
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                }

                Button {
                    text: "设置"
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: {
                        controller.selectAssistant()
                        aiSettingsDialog.open()
                    }
                }
            }
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: "好友" }
            TabButton { text: "群聊" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ColumnLayout {
                spacing: 8
                ListView {
                    id: friendList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: friendModel
                    spacing: 4
                    delegate: ContactDelegate {
                        width: friendList.width
                        theme: root.theme
                        selected: controller.selectedConversationKind === "friend" && controller.selectedConversationIdentifier === identifier
                        onClicked: controller.selectFriend(identifier)
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: friendList.count === 0
                        text: "暂无好友"
                        color: theme.muted
                    }
                }
                // RowLayout {
                //     Layout.fillWidth: true
                //     Button { text: "添加"; Layout.fillWidth: true; onClicked: root.addFriendRequested() }
                //     Button { text: "删除"; Layout.fillWidth: true; enabled: controller.hasSelectedFriend; onClicked: root.deleteFriendRequested() }
                //     Button { text: "编辑备注"; Layout.fillWidth: true; enabled: controller.hasSelectedFriend; onClicked: root.editFriendRemarkRequested() }
                // }
            }

            ColumnLayout {
                spacing: 8
                ListView {
                    id: groupList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: groupModel
                    spacing: 4
                    delegate: ContactDelegate {
                        width: groupList.width
                        theme: root.theme
                        selected: controller.selectedConversationKind === "group" && controller.selectedConversationIdentifier === identifier
                        onClicked: controller.selectGroup(identifier)
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: groupList.count === 0
                        text: "暂无群聊"
                        color: theme.muted
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button { text: "创建"; Layout.fillWidth: true; onClicked: controller.createGroup("群聊") }
                    Button { text: "邀请"; Layout.fillWidth: true; onClicked: controller.inviteSelectedFriendToGroup() }
                    Button { text: "退出"; Layout.fillWidth: true; onClicked: controller.leaveSelectedGroup() }
                }
            }
        }
    }

    Dialog {
        id: aiSettingsDialog
        parent: Overlay.overlay
        modal: true
        title: "AI 助手设置"
        width: parent ? Math.min(460, parent.width - 40) : 460
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        standardButtons: Dialog.NoButton

        onOpened: {
            baseUrlField.text = controller.aiBaseUrl
            modelField.text = controller.aiModelName
            apiKeyField.text = controller.aiApiKey
            providerField.text = controller.aiProvider
            temperatureField.text = Number(controller.aiTemperature).toFixed(1)
            maxTokensSpin.value = Math.max(maxTokensSpin.from, Math.min(maxTokensSpin.to, controller.aiMaxTokens))
        }

        contentItem: ColumnLayout {
            spacing: 10

            Label {
                text: "OpenAI 兼容接口"
                color: theme.text
                font.bold: true
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Label { text: "Base URL"; color: theme.text }
                TextField {
                    id: baseUrlField
                    Layout.fillWidth: true
                    placeholderText: "http://127.0.0.1:8317/v1"
                    selectByMouse: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Label { text: "模型"; color: theme.text }
                TextField {
                    id: modelField
                    Layout.fillWidth: true
                    placeholderText: "gpt-5.3-codex-spark"
                    selectByMouse: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Label { text: "API Key"; color: theme.text }
                TextField {
                    id: apiKeyField
                    Layout.fillWidth: true
                    placeholderText: "sk-xxxxxxxxxxxxxxxx"
                    echoMode: TextInput.Password
                    selectByMouse: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Label { text: "Provider"; color: theme.text }
                TextField {
                    id: providerField
                    Layout.fillWidth: true
                    placeholderText: "openai"
                    selectByMouse: true
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Label { text: "Temperature"; color: theme.text }
                    TextField {
                        id: temperatureField
                        Layout.fillWidth: true
                        placeholderText: "0.0"
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        validator: DoubleValidator {
                            bottom: 0.0
                            top: 2.0
                            decimals: 2
                            notation: DoubleValidator.StandardNotation
                        }
                        selectByMouse: true
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Label { text: "Max Tokens"; color: theme.text }
                    SpinBox {
                        id: maxTokensSpin
                        Layout.fillWidth: true
                        from: 64
                        to: 65536
                        stepSize: 256
                        editable: true
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                Button {
                    text: "取消"
                    onClicked: aiSettingsDialog.close()
                }
                Button {
                    text: "保存"
                    highlighted: true
                    onClicked: {
                        var temperature = Number(temperatureField.text)
                        if (!isFinite(temperature)) {
                            temperature = 0.0
                        }
                        if (controller.saveAiSettings(baseUrlField.text,
                                                      modelField.text,
                                                      apiKeyField.text,
                                                      providerField.text,
                                                      temperature,
                                                      maxTokensSpin.value)) {
                            aiSettingsDialog.close()
                        }
                    }
                }
            }
        }
    }
}
