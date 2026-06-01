#include "widget/MainWindow.h"
#include <qnamespace.h>
#include <qtmetamacros.h>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>
#include "MainWindow.moc"

class ChatInputEdit : public QTextEdit {
  Q_OBJECT
  public:
  explicit ChatInputEdit(QWidget *parent = nullptr) : QTextEdit(parent) {
    setAcceptRichText(false);
    setTabChangesFocus(true);
    setMaximumHeight(80);
    setMinimumHeight(32);
    setStyleSheet(QStringLiteral("QTextEdit { padding: 4px;"));
  }

  void SetPlaceHolderText(QString const &text) {
    placeholderText_ = text;
    viewport()->update();
  }

  QString ToPlanText() const {
    return QTextEdit::toPlainText();
  }

  signals:
  void enterPressed();

  protected:
  void keyPressEvent(QKeyEvent *event) override {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
      if (event->modifiers() & Qt::ShiftModifier) {
        QTextEdit::keyPressEvent(event);
      } else {
        event->accept();
        emit enterPressed();
      }
    } else {
      QTextEdit::keyPressEvent(event);
    }
  }

  void paintEvent(QPaintEvent *event) override {
    QTextEdit::paintEvent(event);
    if (document()->isEmpty() && !placeholderText_.isEmpty()) {
      QPainter painter(viewport());
      painter.setPen(QColor(160, 160, 160));
      QRect rect = viewport()->rect();
      rect.setLeft(rect.left() + 5);
      rect.setTop(rect.top() + 5);
      painter.drawText(rect, Qt::AlignLeft | Qt::AlignTop, placeholderText_);
    }
  }

  private:
  QString placeholderText_;
};

namespace {
inline void SetMsgBoxButtonText_(QMessageBox &box,
                                 QMessageBox::StandardButton button,
                                 QString const &text) {
  if (auto btn = box.button(button)) {
    btn->setText(text);
  }
}

}  // namespace

MainWindow::MainWindow(QString const &profileName, QString const &password,
                       QWidget *parent)
    : QMainWindow(parent),
      profileName_(profileName.trimmed().isEmpty() ? QStringLiteral("default")
                                                   : profileName.trimmed()),
      password_(password) {
  BuildUi_();
  WireSignals_();
}

void MainWindow::BuildUi_() {
  auto *central = new QWidget(this);
  setCentralWidget(central);
  setWindowTitle(QStringLiteral("IM"));
  resize(1000, 650);
  accountNameLabel_ = new QLabel(this);
  accountNameLabel_->setStyleSheet(
      QStringLiteral("font-weight: bold; font-size: 14pt; color: #4fc3f7;"));
  selfIdEdit_ = new QLineEdit(this);
  selfIdEdit_->setReadOnly(true);
  selfIdEdit_->setFrame(false);
  selfIdEdit_->setStyleSheet(
      QStringLiteral("QLineEdit { "
                     "  background: transparent; "
                     "  color: #a0a0b0; "
                     "  font-size: 9pt; "
                     "  padding: 0px;"
                     "  border: none;"
                     "}"));
  selfIdEdit_->setMinimumWidth(500);
  auto nameLayout = new QHBoxLayout();
  nameLayout->setSpacing(0);
  nameLayout->addWidget(accountNameLabel_);
  nameLayout->addStretch(1);
  auto toxIdLayout = new QHBoxLayout();
  toxIdLayout->setSpacing(0);
  auto toxIdLabel = new QLabel(QString("ToxId: "), this);
  toxIdLabel->setStyleSheet(QStringLiteral("color: #808090;"));
  toxIdLayout->addWidget(toxIdLabel);
  toxIdLayout->addWidget(selfIdEdit_);
  toxIdLayout->addStretch(1);
  statusBtn_ = new QPushButton(this);
  statusBtn_->setFlat(true);
  statusBtn_->setStyleSheet(
      QStringLiteral("QPushButton { "
                     "  text-align: left; "
                     "  padding: 0px; "
                     "  color: #808090; "
                     "  font-size: 10pt; "
                     "  border: none; "
                     "  background: transparent; "
                     "} "
                     "QPushButton:hover { "
                     "  color: #4fc3f7; "
                     "  text-decoration: underline; "
                     "}"));
  statusBtn_->setCursor(Qt::PointingHandCursor);
  addFriendBtn_ = new QPushButton(QStringLiteral("add friend"), this);
  deleteFriendBtn_ = new QPushButton(QStringLiteral("delete friend"), this);
  editNicknameBtn_ = new QPushButton(QStringLiteral("edit nick name"), this);
  themeToggleBtn_ = new QPushButton(this);
  themeToggleBtn_->setIcon(QIcon(QStringLiteral(":/moon.svg:")));
  themeToggleBtn_->setIconSize(QSize(24, 24));
  themeToggleBtn_->setToolTip(QStringLiteral("switch to light theme"));
  themeToggleBtn_->setFixedSize(26, 26);
  themeToggleBtn_->setStyleSheet(QStringLiteral(
      "QPushButton { border-radius: 18px; border: none; background: "
      "transparent; }"
      "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }"));
  noticeBtn_ = new QPushButton(QStringLiteral("notice"), this);
  noticeBtn_->setToolTip(
      QStringLiteral("open the notice center (log/sign/config)"));
  noticeBtn_->setFixedHeight(addFriendBtn_->sizeHint().height());
  friendList_ = new QListWidget(this);
  friendList_->setMinimumWidth(240);
  groupList_ = new QListWidget(this);
  groupList_->setMinimumWidth(240);
  createGroupBtn_ = new QPushButton(QStringLiteral("create group"), this);
  inviteToGroupBtn_ = new QPushButton(QStringLiteral("invite to group"), this);
  leaveGroupBtn_ = new QPushButton(QStringLiteral("leave group"), this);
  contactTabWidget_ = new QTabWidget(this);
  contactTabWidget_->addTab(friendList_, QStringLiteral("friend"));
  contactTabWidget_->addTab(groupList_, QStringLiteral("group"));
  contactTabWidget_->setMinimumWidth(240);
  chatView_ = new QTextBrowser(this);
  chatView_->setOpenExternalLinks(false);
  messageEdit_ = new ChatInputEdit(this);
  messageEdit_->setPlaceholderText(
      QStringLiteral("input message, enter to send, shift+enter to new line"));
  sendBtn_ = new QPushButton(QStringLiteral("send"), this);
  sendFileBtn_ = new QPushButton(QStringLiteral("send file"), this);
  callBtn_ = new QPushButton(QStringLiteral("voice call"), this);
  videoCallBtn_ = new QPushButton(QStringLiteral("video call"), this);
  auto inputRow = new QHBoxLayout();
  inputRow->addWidget(messageEdit_, 1);
  inputRow->addWidget(callBtn_);
  inputRow->addWidget(videoCallBtn_);
  inputRow->addWidget(sendFileBtn_);
  inputRow->addWidget(sendBtn_);
  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->addWidget(chatView_, 1);
  rightLayout->addLayout(inputRow);
  right->setLayout(rightLayout);
  auto *splitter = new QSplitter(this);
  splitter->addWidget(contactTabWidget_);
  splitter->addWidget(right);
  splitter->setStretchFactor(1, 1);
  auto *userInfoLayout = new QVBoxLayout();
  userInfoLayout->setSpacing(2);
  userInfoLayout->addLayout(nameLayout);
  userInfoLayout->addLayout(toxIdLayout);
  userInfoLayout->addWidget(statusBtn_);
  auto *topRow = new QHBoxLayout();
  topRow->addLayout(userInfoLayout, 1);
  topRow->addWidget(addFriendBtn_);
  topRow->addWidget(editNicknameBtn_);
  topRow->addWidget(deleteFriendBtn_);
  topRow->addWidget(createGroupBtn_);
  topRow->addWidget(inviteToGroupBtn_);
  topRow->addWidget(leaveGroupBtn_);
  topRow->addWidget(noticeBtn_);
  topRow->addWidget(themeToggleBtn_);
  auto *root = new QVBoxLayout(central);
  root->addLayout(topRow);
  root->addWidget(splitter, 1);
  central->setLayout(root);
  iterateTimer_ = std::make_unique<QTimer>();
  iterateTimer_->setSingleShot(true);
  friendListTimer_ = std::make_unique<QTimer>();
  friendListTimer_->setInterval(1000);
  saveTimer_ = std::make_unique<QTimer>();
  saveTimer_->setSingleShot(true);
  saveTimer_->setInterval(1500);
}

void MainWindow::WireSignals_() {
  // - addFriendBtn_->clicked -> onAddFriendClicked_()
  //   打开添加好友对话框，并调用 tox_.addFriend(...) 发起好友请求
  connect(addFriendBtn_, &QPushButton::clicked, this,
          &MainWindow::OnAddFriendClicked_);
  // - deleteFriendBtn_->clicked -> OnDeleteFriendClicked_()
  //   删除当前选中的好友（本地删除 tox_friend_delete）
  connect(deleteFriendBtn_, &QPushButton::clicked, this,
          &MainWindow::OnDeleteFriendClicked_);
  // - editNicknameBtn_->clicked -> OnEditNicknameClicked_()
  //   编辑当前选中好友的备注名
  connect(editNicknameBtn_, &QPushButton::clicked, this,
          &MainWindow::OnEditNicknameClicked_);
  // - sendBtn_->clicked / messageEdit_->returnPressed -> OnSendClicked_()
  //   两种触发方式共用同一个发送逻辑：按钮点击或输入框回车都发送当前输入内容
  connect(sendBtn_, &QPushButton::clicked, this, &MainWindow::OnSendClicked_);

  // - sendFileBtn_->clicked -> OnSendFileClicked_()
  //   点击"发送文件"按钮，选择文件并发送给当前好友
  connect(sendFileBtn_, &QPushButton::clicked, this,
          &MainWindow::OnSendFileClicked_);

  // - messageEdit_->enterPressed -> OnSendClicked_()
  //   输入框回车时发送当前输入内容（Shift+回车换行）
  connect(messageEdit_, &ChatInputEdit::enterPressed, this,
          &MainWindow::OnSendClicked_);

  // - friendList_->itemSelectionChanged -> onFriendSelectionChanged_()
  //   用户切换聊天对象时触发（选中不同的 friend number）
  connect(friendList_, &QListWidget::itemSelectionChanged, this,
          &MainWindow::OnFriendSelectionChanged_);

  // - iterateTimer_->timeout -> onIterateTick_()
  //   把 toxcore 的 iterate() 接入 Qt 事件循环：每次 tick 执行 iterate
  //   并调度下一次 tick
  connect(iterateTimer_.get(), &QTimer::timeout, this,
          &MainWindow::OnIterateTick_);

  // - friendListTimer_->timeout -> refreshFriendList_()
  //   演示用的周期刷新：定期从 toxcore 读取好友列表/在线状态并更新 UI
  // connect(friendListTimer_.get(), &QTimer::timeout, this,
  //         &MainWindow::RefreshFriendList_);

  // - saveTimer_->timeout -> onSaveTick_()
  connect(saveTimer_.get(), &QTimer::timeout, this, &MainWindow::OnSaveTick_);

  // 音频/视频通话
  connect(callBtn_, &QPushButton::clicked, this, &MainWindow::OnCallClicked_);
  connect(videoCallBtn_, &QPushButton::clicked, this,
          &MainWindow::OnVideoCallClicked_);

  // 个签编辑
  connect(statusBtn_, &QPushButton::clicked, this,
          &MainWindow::OnEditStatusMessageClicked_);

  // 主题切换
  connect(themeToggleBtn_, &QPushButton::clicked, this,
          &MainWindow::OnThemeToggleClicked_);

  // 通知中心（日志/提示/配置）
  connect(noticeBtn_, &QPushButton::clicked, this, [this]() {
    if (!noticeDock_) {
      return;
    }
    noticeDock_->setVisible(!noticeDock_->isVisible());
    if (noticeDock_->isVisible()) {
      noticeDock_->setFloating(true);
      noticeDock_->raise();
      noticeUnread_ = 0;
      // UpdateNoticeButtonBadge_();
    }
  });

  // 群聊功能
  connect(createGroupBtn_, &QPushButton::clicked, this,
          &MainWindow::OnCreateGroupClicked_);
  connect(inviteToGroupBtn_, &QPushButton::clicked, this,
          &MainWindow::OnInviteToGroupClicked_);
  connect(leaveGroupBtn_, &QPushButton::clicked, this,
          &MainWindow::OnLeaveGroupClicked_);
  connect(groupList_, &QListWidget::itemSelectionChanged, this,
          &MainWindow::OnGroupSelectionChanged_);
}

// 点击"添加好友"按钮：弹出对话框并发送好友请求
void MainWindow::OnAddFriendClicked_() {
  AddFriendDialog dlg(profileName_, this);
  if (dlg.exec() != QDialog::Accepted) {
    return;
  }

  std::string const toxId = dlg.ToxIdHex().toStdString();
  std::string const msg = dlg.RequestMessage().toStdString();
  try {
    uint32_t const fn =
        tox_->AddFriend(toxId, msg.empty() ? std::string("hi") : msg);
    AppendSystemMessage_(QStringLiteral("✓ 好友请求已发送，等待对方接受"),
                         "#4caf50");
    // contact & savedata
    (void)FriendPubKeyHex_(fn);
    ScheduleSaveTox_();
    RefreshFriendList_();
  } catch (std::exception const &e) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle(QStringLiteral("添加好友失败"));
    msgBox.setText(QString::fromUtf8(e.what()));
    msgBox.setStandardButtons(QMessageBox::Ok);
    SetMsgBoxButtonText_(msgBox, QMessageBox::Ok, QStringLiteral("确认"));
    msgBox.exec();
  }
}

// “删除好友”按钮处理：读取当前选中 friend number，二次确认后调用
// tox_.deleteFriend 做本地删除
void MainWindow::OnDeleteFriendClicked_() {
  if (isAiChatActive_) {
    QMessageBox msgBox(QMessageBox::Information, QStringLiteral("提示"),
                       QStringLiteral("AI助手会话不支持该操作。"),
                       QMessageBox::NoButton, this);
    msgBox.addButton(QStringLiteral("确认"), QMessageBox::AcceptRole);
    msgBox.exec();
    return;
  }
  uint32_t const fn = CurrentFriendNumber_();
  if (fn == UINT32_MAX) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setWindowTitle(QStringLiteral("提示"));
    msgBox.setText(QStringLiteral("请先选择要删除的好友。"));
    msgBox.setStandardButtons(QMessageBox::Ok);
    SetMsgBoxButtonText_(msgBox, QMessageBox::Ok, QStringLiteral("确认"));
    msgBox.exec();
    return;
  }

  // 获取好友显示名称
  QString friendName = GetFriendDisplayName_(fn);

  QMessageBox msgBox(this);
  msgBox.setWindowTitle(QStringLiteral("删除好友"));
  msgBox.setText(
      QStringLiteral(
          "确定要删除好友 %1 "
          "吗？\n\n注意：这是本地删除，对方不会收到明确通知，但连接会断开。")
          .arg(friendName));
  msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  SetMsgBoxButtonText_(msgBox, QMessageBox::Yes, QStringLiteral("确认"));
  SetMsgBoxButtonText_(msgBox, QMessageBox::No, QStringLiteral("取消"));

  if (msgBox.exec() != QMessageBox::Yes) {
    return;
  }

  try {
    QString friendName = GetFriendDisplayName_(fn);
    tox_->DeleteFriend(fn);
    AppendSystemMessage_(QStringLiteral("✓ 已删除好友：%1").arg(friendName),
                         "#ff9800");
    friendConn_.remove(fn);
    friendPk_.remove(fn);
    lastSelectedFriend_ = UINT32_MAX;
    ScheduleSaveTox_();
    RefreshFriendList_();
  } catch (std::exception const &e) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle(QStringLiteral("删除失败"));
    msgBox.setText(QString::fromUtf8(e.what()));
    msgBox.setStandardButtons(QMessageBox::Ok);
    SetMsgBoxButtonText_(msgBox, QMessageBox::Ok, QStringLiteral("确认"));
    msgBox.exec();
  }
}

// 点击"编辑备注"按钮：修改当前选中好友的备注名
void MainWindow::OnEditNicknameClicked_() {
  if (isAiChatActive_) {
    QMessageBox msgBox(QMessageBox::Information, QStringLiteral("提示"),
                       QStringLiteral("AI助手会话不支持该操作。"),
                       QMessageBox::NoButton, this);
    msgBox.addButton(QStringLiteral("确认"), QMessageBox::AcceptRole);
    msgBox.exec();
    return;
  }
  uint32_t const fn = CurrentFriendNumber_();
  if (fn == UINT32_MAX) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setWindowTitle(QStringLiteral("提示"));
    msgBox.setText(QStringLiteral("请先选择一个好友。"));
    msgBox.setStandardButtons(QMessageBox::Ok);
    SetMsgBoxButtonText_(msgBox, QMessageBox::Ok, QStringLiteral("确认"));
    msgBox.exec();
    return;
  }

  QString const pk = FriendPubKeyHex_(fn);
  if (pk.isEmpty()) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle(QStringLiteral("错误"));
    msgBox.setText(QStringLiteral("无法获取好友公钥。"));
    msgBox.setStandardButtons(QMessageBox::Ok);
    SetMsgBoxButtonText_(msgBox, QMessageBox::Ok, QStringLiteral("确认"));
    msgBox.exec();
    return;
  }

  // 获取当前备注
  QString const currentNickname = store_.GetContactNickname(pk);

  // 获取好友显示名称
  QString friendName = GetFriendDisplayName_(fn);

  // 弹出输入框
  QInputDialog inputDlg(this);
  inputDlg.setWindowTitle(QStringLiteral("编辑备注"));
  inputDlg.setLabelText(
      QStringLiteral("好友：%1\n\n请输入备注名（留空则清除备注）：")
          .arg(friendName));
  inputDlg.setTextValue(currentNickname);
  inputDlg.setOkButtonText(QStringLiteral("确认"));
  inputDlg.setCancelButtonText(QStringLiteral("取消"));

  if (inputDlg.exec() != QDialog::Accepted) {
    return;  // 用户取消
  }
  QString const newNickname = inputDlg.textValue();

  // 设置备注
  try {
    store_.SetContactNickname(pk, newNickname.trimmed());
    AppendSystemMessage_(
        QStringLiteral("✓ 已设置好友备注：%1")
            .arg(newNickname.isEmpty() ? QStringLiteral("（已清除）")
                                       : newNickname),
        "#2196f3");
    RefreshFriendList_();  // 刷新列表显示
  } catch (std::exception const &e) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle(QStringLiteral("设置失败"));
    msgBox.setText(QString::fromUtf8(e.what()));
    msgBox.setStandardButtons(QMessageBox::Ok);
    SetMsgBoxButtonText_(msgBox, QMessageBox::Ok, QStringLiteral("确认"));
    msgBox.exec();
  }
}

// 点击"编辑个签"按钮：修改自己的状态消息
void MainWindow::OnEditStatusMessageClicked_() {
  if (!tox_) {
    return;
  }

  QString const currentStatus =
      QString::fromStdString(tox_->GetSelfStatusMessage());
  QInputDialog inputDlg(this);
  inputDlg.setWindowTitle(QStringLiteral("设置个性签名"));
  inputDlg.setLabelText(QStringLiteral("请输入新的个性签名："));
  inputDlg.setTextValue(currentStatus);
  inputDlg.setOkButtonText(QStringLiteral("确认"));
  inputDlg.setCancelButtonText(QStringLiteral("取消"));

  if (inputDlg.exec() != QDialog::Accepted) {
    return;
  }
  QString const newStatus = inputDlg.textValue();

  try {
    tox_->SetSelfStatusMessage(newStatus.trimmed().toStdString());
    QString const displayStatus = newStatus.trimmed();
    if (displayStatus.isEmpty()) {
      statusBtn_->setText(QStringLiteral("点击设置个签"));
    } else {
      statusBtn_->setText(displayStatus);
    }
    ScheduleSaveTox_();  // 触发保存
  } catch (std::exception const &e) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle(QStringLiteral("设置失败"));
    msgBox.setText(QString::fromUtf8(e.what()));
    msgBox.setStandardButtons(QMessageBox::Ok);
    SetMsgBoxButtonText_(msgBox, QMessageBox::Ok, QStringLiteral("确认"));
    msgBox.exec();
  }
}

// 点击"发送"按钮或回车：发送消息给当前好友或群聊
void MainWindow::OnSendClicked_() {
  QString const text = messageEdit_->ToPlainText().trimmed();
  if (text.isEmpty()) {
    return;
  }

  if (isAiChatActive_) {
    SendAiMessage_(text);
    return;
  }

  // 检查是会议聊天还是好友聊天
  if (isConferenceChatActive_ && currentConferenceNumber_ != UINT32_MAX) {
    // 会议消息发送
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool success = tox_->SendConferenceMessage(currentConferenceNumber_,
                                               text.toStdString());

    if (success) {
      // 显示自己发送的消息（isHistoryMe=true 表示是自己，名字显示"我"）
      AppendConferenceChatLine_(currentConferenceNumber_, UINT32_MAX, text,
                                {.timestampMs = now,
                                 .isHistoryMe = true,
                                 .senderNameOverride = QStringLiteral("我")});

      // 保存到数据库
      try {
        QString confId = ConferenceIdentifier_(currentConferenceNumber_);
        if (!confId.isEmpty()) {
          std::string selfPubKey = tox_->GetSelfAddressHex().substr(0, 64);
          std::string selfName = tox_->GetSelfName();

          store_.InsertConferenceMessage(confId,
                                         QString::fromStdString(selfPubKey),
                                         QString::fromStdString(selfName),
                                         1,  // outgoing
                                         text, now);
        }
      } catch (std::exception const &e) {
        qWarning() << "Failed to save conference message:" << e.what();
      }

      messageEdit_->clear();
    } else {
      QMessageBox::warning(this, QStringLiteral("发送失败"),
                           QStringLiteral("群聊消息发送失败"));
    }
  } else {
    // 好友消息发送
    uint32_t const fn = CurrentFriendNumber_();
    if (fn == UINT32_MAX) {
      QMessageBox msgBox(this);
      msgBox.setIcon(QMessageBox::Information);
      msgBox.setWindowTitle(QStringLiteral("提示"));
      msgBox.setText(QStringLiteral("请先选择一个好友或群聊。"));
      msgBox.setStandardButtons(QMessageBox::Ok);
      SetMsgBoxButtonText_(msgBox, QMessageBox::Ok, QStringLiteral("确认"));
      msgBox.exec();
      return;
    }

    try {
      qint64 now = QDateTime::currentMSecsSinceEpoch();
      (void)tox_->SendFriendMessage(fn, text.toStdString());
      AppendChatLine_(fn, true, text, {.timestampMs = now});

      // 落库：outgoing (direction=1)
      try {
        QString const pk = FriendPubKeyHex_(fn);
        if (!pk.isEmpty()) {
          store_.InsertMessage(pk, 1, static_cast<int>(TOX_MESSAGE_TYPE_NORMAL),
                               text, now);
        }
      } catch (...) {}

      messageEdit_->clear();
    } catch (std::exception const &e) {
      QMessageBox msgBox(this);
      msgBox.setIcon(QMessageBox::Warning);
      msgBox.setWindowTitle(QStringLiteral("发送失败"));
      msgBox.setText(QString::fromUtf8(e.what()));
      msgBox.setStandardButtons(QMessageBox::Ok);
      SetMsgBoxButtonText_(msgBox, QMessageBox::Ok, QStringLiteral("确认"));
      msgBox.exec();
    }
  }
}

// 点击"音频通话"按钮：发起音频通话（打开通话窗口并调用 toxcore API）
void MainWindow::OnCallClicked_() {
  if (isAiChatActive_) {
    QMessageBox msgBox(QMessageBox::Information, QStringLiteral("提示"),
                       QStringLiteral("AI助手会话不支持语音通话。"),
                       QMessageBox::NoButton, this);
    msgBox.addButton(QStringLiteral("确认"), QMessageBox::AcceptRole);
    msgBox.exec();
    return;
  }
  uint32_t const fn = CurrentFriendNumber_();
  if (fn == UINT32_MAX) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setWindowTitle(QStringLiteral("提示"));
    msgBox.setText(QStringLiteral("请先选择一个好友。"));
    msgBox.setStandardButtons(QMessageBox::Ok);
    SetMsgBoxButtonText_(msgBox, QMessageBox::Ok, QStringLiteral("确认"));
    msgBox.exec();
    return;
  }

  // 打开呼叫窗口
  if (callWindow_) {
    callWindow_->close();
    callWindow_ = nullptr;
  }

  currentCallFriend_ = fn;
  videoEnabled_ = false;  // 音频通话
  currentCallOutgoing_ = true;
  localHangupPending_ = false;
  callHangupRecorded_ = false;
  callAnswered_ = false;

  // 获取好友显示名称
  QString friendName = GetFriendDisplayName_(fn);
  callWindow_ = new CallWindow(fn, friendName, false, false,
                               this);  // false=去电, false=无视频

                                       // 设置挂断回调
  callWindow_->SetOnHangup([this, fn]() {
    localHangupPending_ = true;
    if (!callHangupRecorded_) {
      QString statusKey;
      int duration = callWindow_ ? callWindow_->GetCallDuration() : 0;
      if (callAnswered_) {
        statusKey = QStringLiteral("HANGUP_SELF");
      } else {
        statusKey = currentCallOutgoing_ ? QStringLiteral("CANCEL_SELF")
                                         : QStringLiteral("REJECT_SELF");
      }
      AppendCallRecord_(fn, true, false, duration, false,
                        {.statusKey = statusKey});
      callHangupRecorded_ = true;
    }
    tox_->Hangup(fn);
    inCall_ = false;
    videoEnabled_ = false;
    currentCallFriend_ = UINT32_MAX;
  });

  // 设置通话结束回调（显示通话记录）
  callWindow_->SetOnCallFinished([this, fn](int durationSeconds) {
    // 如果已经在挂断回调中记录过，且是本地主动挂断/取消，跳过
    if (localHangupPending_ && callHangupRecorded_) {
      localHangupPending_ = false;
      callHangupRecorded_ = false;
      callAnswered_ = false;
      return;
    }

    // 去电场景，对方触发的结束
    QString statusKey;
    if (callAnswered_) {
      // 已接通，对方挂断
      statusKey = QStringLiteral("HANGUP_REMOTE");
    } else {
      // 未接通，对方拒绝
      statusKey = QStringLiteral("REJECT_REMOTE");
    }

    AppendCallRecord_(fn, true, false, durationSeconds, false,
                      {.statusKey = statusKey});
    localHangupPending_ = false;
    callHangupRecorded_ = false;
    callAnswered_ = false;
  });

  // 发起音频呼叫（使用默认参数：audioEnabled=true, videoEnabled=false）
  if (tox_->Call(fn, {.audioEnabled = true,
                      .videoEnabled = false,
                      .audioBitrateKbps = static_cast<uint32_t>(
                          qMax(1, avConfig_.audio.bitrateKbps)),
                      .videoBitrateKbps = static_cast<uint32_t>(
                          qMax(1, avConfig_.video.bitrateKbps))}) == 0) {
    inCall_ = true;
    callWindow_->show();
  } else {
    callWindow_->close();
    callWindow_ = nullptr;
    currentCallFriend_ = UINT32_MAX;
  }
}

// 点击"视频通话"按钮：发起视频通话（打开通话窗口、启动摄像头）
void MainWindow::OnVideoCallClicked_() {
  if (isAiChatActive_) {
    QMessageBox msgBox(QMessageBox::Information, QStringLiteral("提示"),
                       QStringLiteral("AI助手会话不支持视频通话。"),
                       QMessageBox::NoButton, this);
    msgBox.addButton(QStringLiteral("确认"), QMessageBox::AcceptRole);
    msgBox.exec();
    return;
  }
  uint32_t const fn = CurrentFriendNumber_();
  if (fn == UINT32_MAX) {
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setWindowTitle(QStringLiteral("提示"));
    msgBox.setText(QStringLiteral("请先选择一个好友。"));
    msgBox.setStandardButtons(QMessageBox::Ok);
    SetMsgBoxButtonText_(msgBox, QMessageBox::Ok, QStringLiteral("确认"));
    msgBox.exec();
    return;
  }

  // 打开视频呼叫窗口
  if (callWindow_) {
    callWindow_->close();
    callWindow_ = nullptr;
  }

  currentCallFriend_ = fn;
  videoEnabled_ = true;  // 视频通话
  currentCallOutgoing_ = true;
  localHangupPending_ = false;
  callHangupRecorded_ = false;
  callAnswered_ = false;

  // 获取好友显示名称
  QString friendName = GetFriendDisplayName_(fn);
  callWindow_ = new CallWindow(fn, friendName, false, true,
                               this);  // false=去电, true=视频

                                       // 设置挂断回调
  callWindow_->SetOnHangup([this, fn]() {
    localHangupPending_ = true;
    if (!callHangupRecorded_) {
      QString statusKey;
      int duration = callWindow_ ? callWindow_->GetCallDuration() : 0;
      if (callAnswered_) {
        statusKey = QStringLiteral("HANGUP_SELF");
      } else {
        statusKey = currentCallOutgoing_ ? QStringLiteral("CANCEL_SELF")
                                         : QStringLiteral("REJECT_SELF");
      }
      AppendCallRecord_(fn, true, true, duration, false,
                        {.statusKey = statusKey});
      callHangupRecorded_ = true;
    }
    tox_->Hangup(fn);
    inCall_ = false;
    videoEnabled_ = false;
    currentCallFriend_ = UINT32_MAX;

    // 停止摄像头
    if (video_) {
      video_->StopCamera();
    }
  });

  // 设置通话结束回调（显示通话记录）
  callWindow_->SetOnCallFinished([this, fn](int durationSeconds) {
    // 如果已经在挂断回调中记录过，且是本地主动挂断/取消，跳过
    if (localHangupPending_ && callHangupRecorded_) {
      localHangupPending_ = false;
      callHangupRecorded_ = false;
      callAnswered_ = false;
      return;
    }

    // 去电场景，对方触发的结束
    QString statusKey;
    if (callAnswered_) {
      // 已接通，对方挂断
      statusKey = QStringLiteral("HANGUP_REMOTE");
    } else {
      // 未接通，对方拒绝
      statusKey = QStringLiteral("REJECT_REMOTE");
    }

    AppendCallRecord_(fn, true, true, durationSeconds, false,
                      {.statusKey = statusKey});
    localHangupPending_ = false;
    callHangupRecorded_ = false;
    callAnswered_ = false;
  });

  // 发起视频呼叫（audioEnabled=true, videoEnabled=true）
  if (tox_->Call(fn, {.audioEnabled = true,
                      .videoEnabled = true,
                      .audioBitrateKbps = static_cast<uint32_t>(
                          qMax(1, avConfig_.audio.bitrateKbps)),
                      .videoBitrateKbps = static_cast<uint32_t>(
                          qMax(1, avConfig_.video.bitrateKbps))}) == 0) {
    inCall_ = true;

    // 启动摄像头并设置本地预览
    if (video_) {
      video_->SetLocalPreviewSink(callWindow_->GetLocalVideoSink());
      video_->StartCamera();

      // 连接远程视频信号
      connect(video_.get(), &VideoManager::RemoteFrameReady, callWindow_,
              &CallWindow::RenderRemoteFrame);
    }

    callWindow_->show();
  } else {
    callWindow_->close();
    callWindow_ = nullptr;
    videoEnabled_ = false;
    currentCallFriend_ = UINT32_MAX;
  }
}

// 好友选择变化：切换到选中的好友/AI助手并加载历史消息
void MainWindow::OnFriendSelectionChanged_() {
  // 先判断是否选中了 AI 助手
  if (IsAiChatActive_()) {
    groupList_->clearSelection();
    currentConferenceNumber_ = UINT32_MAX;
    isConferenceChatActive_ = false;
    lastSelectedFriend_ = UINT32_MAX;

    if (isAiChatActive_) {
      return;
    }
    isAiChatActive_ = true;

    chatView_->clear();
    lastDisplayedDate_ = QDate();
    LoadRecentAiMessages_();
    return;
  }

  // 以下为选中真实好友的原有逻辑
  isAiChatActive_ = false;

  uint32_t const fn = CurrentFriendNumber_();
  if (fn == UINT32_MAX) {
    return;
  }

  // 清除会议选择
  groupList_->clearSelection();
  currentConferenceNumber_ = UINT32_MAX;
  isConferenceChatActive_ = false;

  // 清楚改好友的未读计数
  if (friendUnreadCount_.value(fn, 0) > 0) {
    friendUnreadCount_.remove(fn);
    RefreshFriendList_();
    UpdateTabBadge_();
  }

  // 只在“真正选中不同好友”时提示一次，避免重复触发造成刷屏
  if (fn == lastSelectedFriend_) {
    return;
  }
  lastSelectedFriend_ = fn;

  chatView_->clear();
  lastDisplayedDate_ = QDate();  // 重置日期记录
  LoadRecentMessages_(fn);
}
