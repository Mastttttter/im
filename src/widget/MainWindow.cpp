#include "widget/MainWindow.h"
#include "widget/AddFriendDialog.h"
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

class ChatInputEdit final : public QTextEdit {
  public:
  explicit ChatInputEdit(QWidget *parent = nullptr) : QTextEdit(parent) {
    setAcceptRichText(false);
    setTabChangesFocus(true);
    setMinimumHeight(42);
    setMaximumHeight(96);
  }

  void SetSubmitHandler(std::function<void()> handler) {
    submitHandler_ = std::move(handler);
  }

  protected:
  void keyPressEvent(QKeyEvent *event) override {
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        !(event->modifiers() & Qt::ShiftModifier)) {
      event->accept();
      if (submitHandler_) {
        submitHandler_();
      }
      return;
    }
    QTextEdit::keyPressEvent(event);
  }

  private:
  std::function<void()> submitHandler_;
};

namespace {
constexpr uint32_t kInvalidNumber = std::numeric_limits<uint32_t>::max();

struct BootstrapNode {
  QString address;
  uint16_t port{};
  QString publicKeyHex;
};

QString FromUtf8(std::string const &text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString EscapedMultiline(QString text) {
  return text.toHtmlEscaped().replace(QStringLiteral("\n"),
                                      QStringLiteral("<br/>"));
}

QString TimeText() {
  return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
}

QString ChatLine(QString const &sender, QString const &message, bool outgoing) {
  QString const nameColor = outgoing ? QStringLiteral("#4fc3f7")
                                     : QStringLiteral("#81c784");
  return QStringLiteral(
             "<div style='margin:8px 0;'>"
             "<span style='font-weight:600;color:%1;'>%2</span> "
             "<span style='color:#8b92a6;font-size:9pt;'>%3</span>"
             "<div style='white-space:pre-wrap;'>%4</div>"
             "</div>")
      .arg(nameColor, sender.toHtmlEscaped(), TimeText(),
           EscapedMultiline(message));
}

void SetMsgBoxButtonText(QMessageBox &box, QMessageBox::StandardButton button,
                         QString const &text) {
  if (auto *btn = box.button(button)) {
    btn->setText(text);
  }
}

QJsonArray NodeArrayFromDocument(QJsonDocument const &doc) {
  if (doc.isArray()) {
    return doc.array();
  }
  if (!doc.isObject()) {
    return {};
  }
  QJsonObject const object = doc.object();
  if (object.value(QStringLiteral("nodes")).isArray()) {
    return object.value(QStringLiteral("nodes")).toArray();
  }
  if (object.value(QStringLiteral("bootstrap_nodes")).isArray()) {
    return object.value(QStringLiteral("bootstrap_nodes")).toArray();
  }
  return {};
}

QString FirstString(QJsonObject const &object,
                    std::initializer_list<QStringView> keys) {
  for (QStringView key : keys) {
    QJsonValue const value = object.value(key.toString());
    if (value.isString() && !value.toString().trimmed().isEmpty()) {
      return value.toString().trimmed();
    }
  }
  return {};
}

uint16_t PortFromObject(QJsonObject const &object) {
  QJsonValue const value = object.value(QStringLiteral("port"));
  if (value.isDouble()) {
    int const port = value.toInt();
    if (port > 0 && port <= 65535) {
      return static_cast<uint16_t>(port);
    }
  }
  if (value.isString()) {
    bool ok = false;
    int const port = value.toString().toInt(&ok);
    if (ok && port > 0 && port <= 65535) {
      return static_cast<uint16_t>(port);
    }
  }
  return 0;
}

std::vector<BootstrapNode> DefaultBootstrapNodes() {
  return {{QStringLiteral("144.217.167.73"), 33445,
           QStringLiteral(
               "7E5668E0EE09E19F320AD47902419331FFEE147BB3606769CFBE921A2A2FD34C")},
          {QStringLiteral("tox.abilinski.com"), 33445,
           QStringLiteral(
               "10C00EB250C3233E343E2AEBA07115A5C28920E9C8D29492F6D00B29049EDC7E")},
          {QStringLiteral("198.199.98.108"), 33445,
           QStringLiteral(
               "BEF0CFB37AF874BD17B9A8F9FE64C75521DB95A37D33C5BDB00E9CF58659C04F")}};
}

std::vector<BootstrapNode> LoadBootstrapNodes(QStringList const &paths) {
  for (QString const &path : paths) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      continue;
    }
    QJsonParseError error{};
    QJsonDocument const doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
      continue;
    }
    std::vector<BootstrapNode> nodes;
    for (QJsonValue const value : NodeArrayFromDocument(doc)) {
      if (!value.isObject()) {
        continue;
      }
      QJsonObject const object = value.toObject();
      QString address = FirstString(
          object, {QStringLiteral("address"), QStringLiteral("host"),
                   QStringLiteral("ipv4"), QStringLiteral("ip")});
      QString publicKey = FirstString(
          object, {QStringLiteral("public_key"), QStringLiteral("publicKey"),
                   QStringLiteral("publicKeyHex"), QStringLiteral("key")});
      publicKey.remove(QChar(' '));
      publicKey.remove(QChar(':'));
      uint16_t const port = PortFromObject(object);
      if (address.compare(QStringLiteral("NONE"), Qt::CaseInsensitive) != 0 &&
          !address.isEmpty() && port != 0 && publicKey.size() == 64) {
        nodes.push_back({address, port, publicKey});
      }
    }
    if (!nodes.empty()) {
      return nodes;
    }
  }
  return DefaultBootstrapNodes();
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
  ApplyTheme_();
  StartTox_();
}

void MainWindow::BuildUi_() {
  auto *central = new QWidget(this);
  setCentralWidget(central);
  setWindowTitle(QStringLiteral("Tox IM"));
  resize(1080, 700);

  accountNameLabel_ = new QLabel(profileName_, this);
  selfIdEdit_ = new QLineEdit(this);
  selfIdEdit_->setReadOnly(true);
  selfIdEdit_->setPlaceholderText(QStringLiteral("Tox ID will appear after startup"));
  networkStatusLabel_ = new QLabel(QStringLiteral("network: starting"), this);

  addFriendBtn_ = new QPushButton(QStringLiteral("add friend"), this);
  deleteFriendBtn_ = new QPushButton(QStringLiteral("delete friend"), this);
  createGroupBtn_ = new QPushButton(QStringLiteral("create group"), this);
  inviteToGroupBtn_ = new QPushButton(QStringLiteral("invite friend"), this);
  leaveGroupBtn_ = new QPushButton(QStringLiteral("leave group"), this);
  noticeBtn_ = new QPushButton(QStringLiteral("notice"), this);
  themeToggleBtn_ = new QPushButton(this);

  friendList_ = new QListWidget(this);
  groupList_ = new QListWidget(this);
  contactTabs_ = new QTabWidget(this);
  contactTabs_->addTab(friendList_, QStringLiteral("friends"));
  contactTabs_->addTab(groupList_, QStringLiteral("groups"));
  contactTabs_->setMinimumWidth(270);

  chatView_ = new QTextBrowser(this);
  chatView_->setOpenExternalLinks(false);
  messageEdit_ = new ChatInputEdit(this);
  messageEdit_->setPlaceholderText(
      QStringLiteral("type a message, Enter sends, Shift+Enter adds a line"));
  sendBtn_ = new QPushButton(QStringLiteral("send"), this);

  auto *headerText = new QVBoxLayout();
  headerText->addWidget(accountNameLabel_);
  headerText->addWidget(selfIdEdit_);
  headerText->addWidget(networkStatusLabel_);

  auto *headerButtons = new QHBoxLayout();
  headerButtons->addWidget(addFriendBtn_);
  headerButtons->addWidget(deleteFriendBtn_);
  headerButtons->addSpacing(12);
  headerButtons->addWidget(createGroupBtn_);
  headerButtons->addWidget(inviteToGroupBtn_);
  headerButtons->addWidget(leaveGroupBtn_);
  headerButtons->addSpacing(12);
  headerButtons->addWidget(noticeBtn_);
  headerButtons->addWidget(themeToggleBtn_);

  auto *header = new QHBoxLayout();
  header->addLayout(headerText, 1);
  header->addLayout(headerButtons);

  auto *inputRow = new QHBoxLayout();
  inputRow->addWidget(messageEdit_, 1);
  inputRow->addWidget(sendBtn_);

  auto *chatPanel = new QWidget(this);
  auto *chatLayout = new QVBoxLayout(chatPanel);
  chatLayout->addWidget(chatView_, 1);
  chatLayout->addLayout(inputRow);

  auto *splitter = new QSplitter(this);
  splitter->addWidget(contactTabs_);
  splitter->addWidget(chatPanel);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);

  auto *root = new QVBoxLayout(central);
  root->addLayout(header);
  root->addWidget(splitter, 1);

  iterateTimer_ = new QTimer(this);
  iterateTimer_->setSingleShot(true);
  refreshTimer_ = new QTimer(this);
  refreshTimer_->setInterval(1500);

  SetupNotificationCenter_();
  RenderCurrentConversation_();
}

void MainWindow::WireSignals_() {
  connect(addFriendBtn_, &QPushButton::clicked, this,
          &MainWindow::OnAddFriendClicked_);
  connect(deleteFriendBtn_, &QPushButton::clicked, this,
          &MainWindow::OnDeleteFriendClicked_);
  connect(sendBtn_, &QPushButton::clicked, this, &MainWindow::OnSendClicked_);
  connect(themeToggleBtn_, &QPushButton::clicked, this,
          &MainWindow::OnThemeToggleClicked_);
  connect(createGroupBtn_, &QPushButton::clicked, this,
          &MainWindow::OnCreateGroupClicked_);
  connect(inviteToGroupBtn_, &QPushButton::clicked, this,
          &MainWindow::OnInviteToGroupClicked_);
  connect(leaveGroupBtn_, &QPushButton::clicked, this,
          &MainWindow::OnLeaveGroupClicked_);
  connect(friendList_, &QListWidget::itemSelectionChanged, this,
          &MainWindow::OnFriendSelectionChanged_);
  connect(groupList_, &QListWidget::itemSelectionChanged, this,
          &MainWindow::OnGroupSelectionChanged_);
  connect(iterateTimer_, &QTimer::timeout, this, &MainWindow::OnIterateTick_);
  connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::OnRefreshTick_);
  connect(noticeBtn_, &QPushButton::clicked, this, [this]() {
    if (!noticeDock_) {
      return;
    }
    noticeDock_->setVisible(!noticeDock_->isVisible());
    if (noticeDock_->isVisible()) {
      noticeDock_->raise();
      noticeUnread_ = 0;
      UpdateNoticeBadge_();
    }
  });
  messageEdit_->SetSubmitHandler([this]() { OnSendClicked_(); });
}

void MainWindow::ApplyTheme_() {
  QString const base = isDarkTheme_ ? QStringLiteral("#12151c")
                                    : QStringLiteral("#f5f7fb");
  QString const panel = isDarkTheme_ ? QStringLiteral("#1b202b")
                                     : QStringLiteral("#ffffff");
  QString const text = isDarkTheme_ ? QStringLiteral("#e8ecf3")
                                    : QStringLiteral("#1f2937");
  QString const muted = isDarkTheme_ ? QStringLiteral("#9aa4b5")
                                     : QStringLiteral("#5b6472");
  QString const border = isDarkTheme_ ? QStringLiteral("#303747")
                                      : QStringLiteral("#d7dde8");
  QString const button = isDarkTheme_ ? QStringLiteral("#263244")
                                      : QStringLiteral("#e8eef8");

  setStyleSheet(QStringLiteral(R"(
    QWidget { background:%1; color:%3; font-size:10pt; }
    QLabel { color:%3; }
    QLineEdit, QTextEdit, QTextBrowser, QListWidget {
      background:%2; color:%3; border:1px solid %5; border-radius:8px; padding:6px;
      selection-background-color:#3b82f6;
    }
    QPushButton {
      background:%6; color:%3; border:1px solid %5; border-radius:8px; padding:7px 11px;
    }
    QPushButton:hover { border-color:#4fc3f7; }
    QPushButton:disabled { color:%4; }
    QTabWidget::pane { border:1px solid %5; border-radius:8px; }
    QTabBar::tab { background:%6; color:%3; padding:8px 12px; border-top-left-radius:8px; border-top-right-radius:8px; }
    QTabBar::tab:selected { background:%2; color:#4fc3f7; }
    QSplitter::handle { background:%5; }
  )")
                    .arg(base, panel, text, muted, border, button));
  accountNameLabel_->setStyleSheet(
      QStringLiteral("font-size:18pt;font-weight:700;color:#4fc3f7;"));
  networkStatusLabel_->setStyleSheet(QStringLiteral("color:%1;").arg(muted));
  selfIdEdit_->setStyleSheet(
      QStringLiteral("QLineEdit { color:%1; font-family:monospace; }").arg(muted));
  themeToggleBtn_->setText(isDarkTheme_ ? QStringLiteral("light")
                                        : QStringLiteral("dark"));
}

void MainWindow::StartTox_() {
  try {
    tox_ = std::make_unique<ToxCore::ToxCoreWrapper>();
    tox_->SetSelfName(profileName_.toStdString());
    tox_->SetSelfStatusMessage("session-only profile");
    selfIdEdit_->setText(QString::fromStdString(tox_->GetSelfAddressHex()));
    networkStatusLabel_->setText(
        QStringLiteral("network: %1").arg(ConnectionLabel_(selfConnection_)));

    tox_->SetOnFriendRequest([this](std::string const &publicKeyHex,
                                    std::string const &message) {
      QString const publicKey = QString::fromStdString(publicKeyHex);
      QString const requestMessage = FromUtf8(message);
      QMetaObject::invokeMethod(
          this,
          [this, publicKey, requestMessage]() {
            QMessageBox box(this);
            box.setIcon(QMessageBox::Question);
            box.setWindowTitle(QStringLiteral("friend request"));
            box.setText(QStringLiteral("Accept friend request from %1?")
                            .arg(publicKey.left(12)));
            box.setInformativeText(requestMessage);
            box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            SetMsgBoxButtonText(box, QMessageBox::Yes, QStringLiteral("accept"));
            SetMsgBoxButtonText(box, QMessageBox::No, QStringLiteral("reject"));
            if (box.exec() == QMessageBox::Yes) {
              try {
                uint32_t const friendNumber =
                    tox_->AddFriendNoRequest(publicKey.toStdString());
                currentFriendNumber_ = friendNumber;
                groupChatActive_ = false;
                currentGroupNumber_ = kInvalidNumber;
                AppendEventLine_(QStringLiteral("friend"),
                                 QStringLiteral("accepted request from %1")
                                     .arg(publicKey.left(12)));
                RefreshFriendList_();
                RenderCurrentConversation_();
              } catch (std::exception const &e) {
                QMessageBox::warning(this, QStringLiteral("friend request failed"),
                                     QString::fromUtf8(e.what()));
              }
            } else {
              AppendEventLine_(QStringLiteral("friend"),
                               QStringLiteral("rejected request from %1")
                                   .arg(publicKey.left(12)));
            }
          },
          Qt::QueuedConnection);
    });

    tox_->SetOnFriendConnectionStatus(
        [this](uint32_t friendNumber, TOX_CONNECTION status) {
          friendConnectionCache_[friendNumber] = status;
          AppendEventLine_(QStringLiteral("friend"),
                           QStringLiteral("%1 is %2")
                               .arg(GetFriendDisplayName_(friendNumber),
                                    ConnectionLabel_(status)));
          RefreshFriendList_();
        });

    tox_->SetOnFriendMessage([this](uint32_t friendNumber, TOX_MESSAGE_TYPE,
                                    std::string const &message) {
      QString const sender = GetFriendDisplayName_(friendNumber);
      AppendFriendMessage_(friendNumber, sender, FromUtf8(message), false);
      AppendEventLine_(QStringLiteral("friend"),
                       QStringLiteral("message from %1").arg(sender));
    });

    tox_->SetOnConferenceInvite(
        [this](uint32_t friendNumber, TOX_CONFERENCE_TYPE,
               std::vector<uint8_t> const &cookie) {
          QString const friendName = GetFriendDisplayName_(friendNumber);
          QMetaObject::invokeMethod(
              this,
              [this, friendNumber, friendName, cookie]() {
                QMessageBox box(this);
                box.setIcon(QMessageBox::Question);
                box.setWindowTitle(QStringLiteral("group invitation"));
                box.setText(QStringLiteral("Join group invited by %1?")
                                .arg(friendName));
                box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                SetMsgBoxButtonText(box, QMessageBox::Yes,
                                    QStringLiteral("join"));
                SetMsgBoxButtonText(box, QMessageBox::No,
                                    QStringLiteral("reject"));
                if (box.exec() == QMessageBox::Yes) {
                  if (tox_->JoinConference(friendNumber, cookie)) {
                    AppendEventLine_(QStringLiteral("group"),
                                     QStringLiteral("accepted invitation from %1")
                                         .arg(friendName));
                  } else {
                    QMessageBox::warning(this, QStringLiteral("group invitation"),
                                         QStringLiteral("Failed to join group."));
                  }
                } else {
                  AppendEventLine_(QStringLiteral("group"),
                                   QStringLiteral("rejected invitation from %1")
                                       .arg(friendName));
                }
              },
              Qt::QueuedConnection);
        });

    tox_->SetOnConferenceConnected([this](uint32_t conferenceNumber) {
      currentGroupNumber_ = conferenceNumber;
      groupChatActive_ = true;
      AppendEventLine_(QStringLiteral("group"),
                       QStringLiteral("joined %1")
                           .arg(GetGroupDisplayName_(conferenceNumber)));
      RefreshGroupList_();
      RenderCurrentConversation_();
    });

    tox_->SetOnConferenceMessage(
        [this](uint32_t conferenceNumber, uint32_t peerNumber, TOX_MESSAGE_TYPE,
               std::string const &message) {
          QString sender = FromUtf8(tox_->GetConferencePeerName(conferenceNumber,
                                                                peerNumber));
          if (sender.isEmpty()) {
            sender = QStringLiteral("peer %1").arg(peerNumber);
          }
          AppendGroupMessage_(conferenceNumber, sender, FromUtf8(message), false);
          AppendEventLine_(QStringLiteral("group"),
                           QStringLiteral("message in %1 from %2")
                               .arg(GetGroupDisplayName_(conferenceNumber),
                                    sender));
        });

    tox_->SetOnConferenceTitle([this](uint32_t conferenceNumber, uint32_t,
                                      std::string const &title) {
      AppendEventLine_(QStringLiteral("group"),
                       QStringLiteral("%1 title is now %2")
                           .arg(QStringLiteral("group"), FromUtf8(title)));
      RefreshGroupList_();
      if (groupChatActive_ && currentGroupNumber_ == conferenceNumber) {
        RenderCurrentConversation_();
      }
    });

    tox_->SetOnConferencePeerName([this](uint32_t conferenceNumber, uint32_t,
                                         std::string const &) {
      RefreshGroupList_();
      if (groupChatActive_ && currentGroupNumber_ == conferenceNumber) {
        RenderCurrentConversation_();
      }
    });

    tox_->SetOnConferencePeerListChanged([this](uint32_t conferenceNumber) {
      AppendEventLine_(QStringLiteral("group"),
                       QStringLiteral("peer list changed in %1")
                           .arg(GetGroupDisplayName_(conferenceNumber)));
      RefreshGroupList_();
      if (groupChatActive_ && currentGroupNumber_ == conferenceNumber) {
        RenderCurrentConversation_();
      }
    });

    AppendEventLine_(QStringLiteral("status"),
                     QStringLiteral("session-only Tox identity created"));
    BootstrapFromConfig_();
    ScheduleIterate_();
    refreshTimer_->start();
  } catch (std::exception const &e) {
    sendBtn_->setEnabled(false);
    addFriendBtn_->setEnabled(false);
    createGroupBtn_->setEnabled(false);
    AppendEventLine_(QStringLiteral("status"),
                     QStringLiteral("Tox startup failed: %1")
                         .arg(QString::fromUtf8(e.what())));
    QMessageBox::critical(this, QStringLiteral("Tox startup failed"),
                          QString::fromUtf8(e.what()));
  }
}

void MainWindow::BootstrapFromConfig_() {
  if (!tox_) {
    return;
  }
  QStringList const paths{
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral("bootstrap_nodes.json")),
      QDir::current().filePath(QStringLiteral("bootstrap_nodes.json"))};
  std::vector<BootstrapNode> const nodes = LoadBootstrapNodes(paths);
  int connected = 0;
  for (BootstrapNode const &node : nodes) {
    try {
      tox_->Bootstrap(node.address.toStdString(), node.port,
                      node.publicKeyHex.toStdString());
      try {
        tox_->AddTcpRelay(node.address.toStdString(), node.port,
                          node.publicKeyHex.toStdString());
      } catch (...) {}
      ++connected;
      AppendEventLine_(QStringLiteral("network"),
                       QStringLiteral("bootstrapped through %1:%2")
                           .arg(node.address)
                           .arg(node.port));
    } catch (std::exception const &e) {
      AppendEventLine_(QStringLiteral("network"),
                       QStringLiteral("bootstrap failed for %1:%2 (%3)")
                           .arg(node.address)
                           .arg(node.port)
                           .arg(QString::fromUtf8(e.what())));
    }
  }
  if (connected == 0) {
    AppendEventLine_(QStringLiteral("network"),
                     QStringLiteral("no bootstrap node accepted the request"));
  }
}

void MainWindow::ScheduleIterate_() {
  if (!tox_ || !iterateTimer_) {
    return;
  }
  uint32_t const interval = tox_->IterationIntervalMs();
  iterateTimer_->start(static_cast<int>(std::clamp<uint32_t>(interval, 10, 1000)));
}

void MainWindow::OnIterateTick_() {
  if (!tox_) {
    return;
  }
  try {
    tox_->Iterate();
    TOX_CONNECTION const connection = tox_->GetSelfConnectionStatus();
    if (connection != selfConnection_) {
      selfConnection_ = connection;
      networkStatusLabel_->setText(
          QStringLiteral("network: %1").arg(ConnectionLabel_(connection)));
      AppendEventLine_(QStringLiteral("network"),
                       QStringLiteral("network is %1")
                           .arg(ConnectionLabel_(connection)));
    }
  } catch (std::exception const &e) {
    AppendEventLine_(QStringLiteral("status"),
                     QStringLiteral("iterate failed: %1")
                         .arg(QString::fromUtf8(e.what())));
  }
  ScheduleIterate_();
}

void MainWindow::OnRefreshTick_() {
  RefreshFriendList_();
  RefreshGroupList_();
}

void MainWindow::RefreshFriendList_() {
  if (!tox_ || !friendList_) {
    return;
  }
  QSignalBlocker const blocker(friendList_);
  uint32_t const selected = currentFriendNumber_;
  friendList_->clear();
  bool selectedStillExists = false;

  try {
    for (uint32_t const friendNumber : tox_->GetFriendList()) {
      TOX_CONNECTION connection = TOX_CONNECTION_NONE;
      try {
        connection = tox_->GetFriendConnectionStatus(friendNumber);
      } catch (...) {}
      friendConnectionCache_[friendNumber] = connection;

      QString text = QStringLiteral("%1 — %2")
                         .arg(GetFriendDisplayName_(friendNumber),
                              ConnectionLabel_(connection));
      int const unread = friendUnreadCount_.value(friendNumber, 0);
      if (unread > 0) {
        text += QStringLiteral(" (%1)").arg(unread);
      }

      auto *item = new QListWidgetItem(text, friendList_);
      item->setData(Qt::UserRole, friendNumber);
      item->setToolTip(FriendPubKeyHex_(friendNumber));
      if (friendNumber == selected) {
        item->setSelected(true);
        friendList_->setCurrentItem(item);
        selectedStillExists = true;
      }
    }
  } catch (std::exception const &e) {
    AppendEventLine_(QStringLiteral("friend"),
                     QStringLiteral("friend refresh failed: %1")
                         .arg(QString::fromUtf8(e.what())));
  }

  if (!selectedStillExists && !groupChatActive_) {
    currentFriendNumber_ = kInvalidNumber;
  }
}

uint32_t MainWindow::CurrentFriendNumber_() const {
  if (auto const *item = friendList_ ? friendList_->currentItem() : nullptr) {
    bool ok = false;
    uint const value = item->data(Qt::UserRole).toUInt(&ok);
    if (ok) {
      return value;
    }
  }
  return currentFriendNumber_;
}

QString MainWindow::FriendPubKeyHex_(uint32_t friendNumber) {
  if (friendNumber == kInvalidNumber || !tox_) {
    return {};
  }
  if (friendPublicKeyCache_.contains(friendNumber)) {
    return friendPublicKeyCache_.value(friendNumber);
  }
  try {
    QString const publicKey =
        QString::fromStdString(tox_->GetFriendPublicKeyHex(friendNumber));
    friendPublicKeyCache_[friendNumber] = publicKey;
    return publicKey;
  } catch (...) {
    return {};
  }
}

QString MainWindow::GetFriendDisplayName_(uint32_t friendNumber) const {
  if (!tox_ || friendNumber == kInvalidNumber) {
    return QStringLiteral("friend");
  }
  try {
    QString const name = FromUtf8(tox_->GetFriendName(friendNumber)).trimmed();
    if (!name.isEmpty()) {
      return name;
    }
    QString const publicKey =
        QString::fromStdString(tox_->GetFriendPublicKeyHex(friendNumber));
    if (!publicKey.isEmpty()) {
      return QStringLiteral("friend %1").arg(publicKey.left(8));
    }
  } catch (...) {}
  return QStringLiteral("friend %1").arg(friendNumber);
}

void MainWindow::RefreshGroupList_() {
  if (!tox_ || !groupList_) {
    return;
  }
  QSignalBlocker const blocker(groupList_);
  uint32_t const selected = currentGroupNumber_;
  groupList_->clear();
  bool selectedStillExists = false;

  for (uint32_t const conferenceNumber : tox_->GetConferenceList()) {
    QString text = QStringLiteral("%1 — %2 peers")
                       .arg(GetGroupDisplayName_(conferenceNumber))
                       .arg(tox_->GetConferencePeerCount(conferenceNumber));
    int const unread = groupUnreadCount_.value(conferenceNumber, 0);
    if (unread > 0) {
      text += QStringLiteral(" (%1)").arg(unread);
    }

    auto *item = new QListWidgetItem(text, groupList_);
    item->setData(Qt::UserRole, conferenceNumber);
    item->setToolTip(GetGroupPeerNames_(conferenceNumber).join(QStringLiteral("\n")));
    if (conferenceNumber == selected) {
      item->setSelected(true);
      groupList_->setCurrentItem(item);
      selectedStillExists = true;
    }
  }

  if (!selectedStillExists && groupChatActive_) {
    currentGroupNumber_ = kInvalidNumber;
    groupChatActive_ = false;
  }
}

uint32_t MainWindow::CurrentGroupNumber_() const {
  if (auto const *item = groupList_ ? groupList_->currentItem() : nullptr) {
    bool ok = false;
    uint const value = item->data(Qt::UserRole).toUInt(&ok);
    if (ok) {
      return value;
    }
  }
  return currentGroupNumber_;
}

QString MainWindow::GetGroupDisplayName_(uint32_t conferenceNumber) const {
  if (!tox_ || conferenceNumber == kInvalidNumber) {
    return QStringLiteral("group");
  }
  QString title = FromUtf8(tox_->GetConferenceTitle(conferenceNumber)).trimmed();
  if (!title.isEmpty()) {
    return title;
  }
  return QStringLiteral("group %1").arg(conferenceNumber);
}

QStringList MainWindow::GetGroupPeerNames_(uint32_t conferenceNumber) const {
  QStringList names;
  if (!tox_ || conferenceNumber == kInvalidNumber) {
    return names;
  }
  uint32_t const count = tox_->GetConferencePeerCount(conferenceNumber);
  for (uint32_t peer = 0; peer < count; ++peer) {
    QString name = FromUtf8(tox_->GetConferencePeerName(conferenceNumber, peer))
                       .trimmed();
    if (name.isEmpty()) {
      name = QStringLiteral("peer %1").arg(peer);
    }
    names.push_back(name);
  }
  return names;
}

void MainWindow::OnAddFriendClicked_() {
  if (!tox_) {
    return;
  }
  AddFriendDialog dialog(profileName_, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  QString toxId = dialog.ToxIdHex();
  toxId.remove(QChar(' '));
  QString message = dialog.RequestMessage().trimmed();
  if (message.isEmpty()) {
    message = QStringLiteral("hello");
  }
  try {
    uint32_t const friendNumber =
        tox_->AddFriend(toxId.toStdString(), message.toStdString());
    currentFriendNumber_ = friendNumber;
    groupChatActive_ = false;
    currentGroupNumber_ = kInvalidNumber;
    AppendSystemMessage_(QStringLiteral("friend request sent"),
                         QStringLiteral("#81c784"));
    AppendEventLine_(QStringLiteral("friend"),
                     QStringLiteral("request sent to %1").arg(toxId.left(12)));
    RefreshFriendList_();
    RenderCurrentConversation_();
  } catch (std::exception const &e) {
    QMessageBox::warning(this, QStringLiteral("add friend failed"),
                         QString::fromUtf8(e.what()));
  }
}

void MainWindow::OnDeleteFriendClicked_() {
  uint32_t const friendNumber = CurrentFriendNumber_();
  if (!tox_ || friendNumber == kInvalidNumber) {
    QMessageBox::information(this, QStringLiteral("delete friend"),
                             QStringLiteral("Select a friend first."));
    return;
  }
  QString const name = GetFriendDisplayName_(friendNumber);
  QMessageBox box(this);
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(QStringLiteral("delete friend"));
  box.setText(QStringLiteral("Delete %1 locally?").arg(name));
  box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  SetMsgBoxButtonText(box, QMessageBox::Yes, QStringLiteral("delete"));
  SetMsgBoxButtonText(box, QMessageBox::No, QStringLiteral("cancel"));
  if (box.exec() != QMessageBox::Yes) {
    return;
  }

  try {
    tox_->DeleteFriend(friendNumber);
    friendUnreadCount_.remove(friendNumber);
    friendConnectionCache_.remove(friendNumber);
    friendPublicKeyCache_.remove(friendNumber);
    friendChatLines_.remove(friendNumber);
    currentFriendNumber_ = kInvalidNumber;
    AppendEventLine_(QStringLiteral("friend"),
                     QStringLiteral("deleted %1").arg(name));
    RefreshFriendList_();
    RenderCurrentConversation_();
  } catch (std::exception const &e) {
    QMessageBox::warning(this, QStringLiteral("delete friend failed"),
                         QString::fromUtf8(e.what()));
  }
}

void MainWindow::OnSendClicked_() {
  if (!tox_) {
    return;
  }
  QString const text = messageEdit_->toPlainText().trimmed();
  if (text.isEmpty()) {
    return;
  }

  if (groupChatActive_) {
    uint32_t const conferenceNumber = CurrentGroupNumber_();
    if (conferenceNumber == kInvalidNumber) {
      QMessageBox::information(this, QStringLiteral("send message"),
                               QStringLiteral("Select a group first."));
      return;
    }
    if (!tox_->SendConferenceMessage(conferenceNumber, text.toStdString())) {
      QMessageBox::warning(this, QStringLiteral("send message failed"),
                           QStringLiteral("Group message was not accepted."));
      return;
    }
    AppendGroupMessage_(conferenceNumber, QStringLiteral("me"), text, true);
    messageEdit_->clear();
    return;
  }

  uint32_t const friendNumber = CurrentFriendNumber_();
  if (friendNumber == kInvalidNumber) {
    QMessageBox::information(this, QStringLiteral("send message"),
                             QStringLiteral("Select a friend or group first."));
    return;
  }
  try {
    tox_->SendFriendMessage(friendNumber, text.toStdString());
    AppendFriendMessage_(friendNumber, QStringLiteral("me"), text, true);
    messageEdit_->clear();
  } catch (std::exception const &e) {
    QMessageBox::warning(this, QStringLiteral("send message failed"),
                         QString::fromUtf8(e.what()));
  }
}

void MainWindow::OnFriendSelectionChanged_() {
  uint32_t const friendNumber = CurrentFriendNumber_();
  if (friendNumber == kInvalidNumber) {
    return;
  }
  {
    QSignalBlocker const blocker(groupList_);
    groupList_->clearSelection();
    groupList_->setCurrentItem(nullptr);
  }
  groupChatActive_ = false;
  currentGroupNumber_ = kInvalidNumber;
  currentFriendNumber_ = friendNumber;
  friendUnreadCount_.remove(friendNumber);
  RefreshFriendList_();
  RenderCurrentConversation_();
}

void MainWindow::OnThemeToggleClicked_() {
  isDarkTheme_ = !isDarkTheme_;
  ApplyTheme_();
}

void MainWindow::OnCreateGroupClicked_() {
  if (!tox_) {
    return;
  }
  bool ok = false;
  QString const title = QInputDialog::getText(
      this, QStringLiteral("create group"), QStringLiteral("group title"),
      QLineEdit::Normal, QStringLiteral("group"), &ok);
  if (!ok) {
    return;
  }

  uint32_t const conferenceNumber = tox_->CreateConference();
  if (conferenceNumber == kInvalidNumber) {
    QMessageBox::warning(this, QStringLiteral("create group failed"),
                         QStringLiteral("Tox did not create the group."));
    return;
  }
  if (!title.trimmed().isEmpty()) {
    tox_->SetConferenceTitle(conferenceNumber, title.trimmed().toStdString());
  }
  currentGroupNumber_ = conferenceNumber;
  currentFriendNumber_ = kInvalidNumber;
  groupChatActive_ = true;
  AppendEventLine_(QStringLiteral("group"),
                   QStringLiteral("created %1")
                       .arg(GetGroupDisplayName_(conferenceNumber)));
  RefreshGroupList_();
  RenderCurrentConversation_();
}

void MainWindow::OnInviteToGroupClicked_() {
  if (!tox_) {
    return;
  }
  uint32_t const conferenceNumber = CurrentGroupNumber_();
  if (conferenceNumber == kInvalidNumber) {
    QMessageBox::information(this, QStringLiteral("invite friend"),
                             QStringLiteral("Select a group first."));
    return;
  }

  std::vector<uint32_t> const friends = tox_->GetFriendList();
  if (friends.empty()) {
    QMessageBox::information(this, QStringLiteral("invite friend"),
                             QStringLiteral("No friends are available."));
    return;
  }

  QStringList choices;
  choices.reserve(static_cast<qsizetype>(friends.size()));
  for (uint32_t const friendNumber : friends) {
    QString publicKey = FriendPubKeyHex_(friendNumber);
    if (publicKey.isEmpty()) {
      publicKey = QString::number(friendNumber);
    }
    choices.push_back(QStringLiteral("%1 — %2 — %3")
                          .arg(GetFriendDisplayName_(friendNumber))
                          .arg(ConnectionLabel_(friendConnectionCache_.value(
                              friendNumber, TOX_CONNECTION_NONE)))
                          .arg(publicKey.left(12)));
  }

  bool ok = false;
  QString const selected = QInputDialog::getItem(
      this, QStringLiteral("invite friend"), QStringLiteral("friend"), choices, 0,
      false, &ok);
  if (!ok) {
    return;
  }
  int const index = choices.indexOf(selected);
  if (index < 0 || index >= static_cast<int>(friends.size())) {
    return;
  }
  uint32_t const friendNumber = friends[static_cast<size_t>(index)];
  if (!tox_->InviteFriendToConference(friendNumber, conferenceNumber)) {
    QMessageBox::warning(this, QStringLiteral("invite friend failed"),
                         QStringLiteral("Tox did not send the group invitation."));
    return;
  }
  AppendEventLine_(QStringLiteral("group"),
                   QStringLiteral("invited %1 to %2")
                       .arg(GetFriendDisplayName_(friendNumber),
                            GetGroupDisplayName_(conferenceNumber)));
}

void MainWindow::OnGroupSelectionChanged_() {
  uint32_t const conferenceNumber = CurrentGroupNumber_();
  if (conferenceNumber == kInvalidNumber) {
    return;
  }
  {
    QSignalBlocker const blocker(friendList_);
    friendList_->clearSelection();
    friendList_->setCurrentItem(nullptr);
  }
  groupChatActive_ = true;
  currentGroupNumber_ = conferenceNumber;
  currentFriendNumber_ = kInvalidNumber;
  groupUnreadCount_.remove(conferenceNumber);
  RefreshGroupList_();
  RenderCurrentConversation_();
}

void MainWindow::OnLeaveGroupClicked_() {
  uint32_t const conferenceNumber = CurrentGroupNumber_();
  if (!tox_ || conferenceNumber == kInvalidNumber) {
    QMessageBox::information(this, QStringLiteral("leave group"),
                             QStringLiteral("Select a group first."));
    return;
  }
  QString const name = GetGroupDisplayName_(conferenceNumber);
  QMessageBox box(this);
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(QStringLiteral("leave group"));
  box.setText(QStringLiteral("Leave %1?").arg(name));
  box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  SetMsgBoxButtonText(box, QMessageBox::Yes, QStringLiteral("leave"));
  SetMsgBoxButtonText(box, QMessageBox::No, QStringLiteral("cancel"));
  if (box.exec() != QMessageBox::Yes) {
    return;
  }

  if (!tox_->DeleteConference(conferenceNumber)) {
    QMessageBox::warning(this, QStringLiteral("leave group failed"),
                         QStringLiteral("Tox did not leave the group."));
    return;
  }
  groupUnreadCount_.remove(conferenceNumber);
  groupChatLines_.remove(conferenceNumber);
  currentGroupNumber_ = kInvalidNumber;
  groupChatActive_ = false;
  AppendEventLine_(QStringLiteral("group"), QStringLiteral("left %1").arg(name));
  RefreshGroupList_();
  RenderCurrentConversation_();
}

void MainWindow::AppendSystemMessage_(QString const &text, QString const &color) {
  chatView_->append(QStringLiteral(
                        "<div style='text-align:center;color:%1;margin:10px;'>%2</div>")
                        .arg(color, text.toHtmlEscaped()));
}

void MainWindow::AppendFriendMessage_(uint32_t friendNumber, QString const &sender,
                                      QString const &message, bool outgoing) {
  QString const line = ChatLine(sender, message, outgoing);
  friendChatLines_[friendNumber].push_back(line);
  bool const active = !groupChatActive_ && currentFriendNumber_ == friendNumber;
  if (!outgoing && !active) {
    friendUnreadCount_[friendNumber] = friendUnreadCount_.value(friendNumber, 0) + 1;
    RefreshFriendList_();
  }
  if (active) {
    chatView_->append(line);
  }
}

void MainWindow::AppendGroupMessage_(uint32_t conferenceNumber,
                                     QString const &sender,
                                     QString const &message, bool outgoing) {
  QString const line = ChatLine(sender, message, outgoing);
  groupChatLines_[conferenceNumber].push_back(line);
  bool const active = groupChatActive_ && currentGroupNumber_ == conferenceNumber;
  if (!outgoing && !active) {
    groupUnreadCount_[conferenceNumber] =
        groupUnreadCount_.value(conferenceNumber, 0) + 1;
    RefreshGroupList_();
  }
  if (active) {
    chatView_->append(line);
  }
}

void MainWindow::RenderCurrentConversation_() {
  if (!chatView_) {
    return;
  }
  chatView_->clear();
  if (groupChatActive_ && currentGroupNumber_ != kInvalidNumber) {
    QStringList const peers = GetGroupPeerNames_(currentGroupNumber_);
    AppendSystemMessage_(QStringLiteral("group: %1").arg(
                             GetGroupDisplayName_(currentGroupNumber_)),
                         QStringLiteral("#8b92a6"));
    AppendSystemMessage_(
        peers.isEmpty() ? QStringLiteral("no visible peers yet")
                        : QStringLiteral("peers: %1").arg(peers.join(", ")),
        QStringLiteral("#8b92a6"));
    for (QString const &line : groupChatLines_.value(currentGroupNumber_)) {
      chatView_->append(line);
    }
    return;
  }

  if (currentFriendNumber_ != kInvalidNumber) {
    AppendSystemMessage_(QStringLiteral("friend: %1")
                             .arg(GetFriendDisplayName_(currentFriendNumber_)),
                         QStringLiteral("#8b92a6"));
    for (QString const &line : friendChatLines_.value(currentFriendNumber_)) {
      chatView_->append(line);
    }
    return;
  }

  AppendSystemMessage_(
      QStringLiteral("Select a friend or group, or copy your Tox ID to share."),
      QStringLiteral("#8b92a6"));
}

void MainWindow::SetupNotificationCenter_() {
  noticeDock_ = new QDockWidget(QStringLiteral("notification center"), this);
  noticeDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea |
                               Qt::BottomDockWidgetArea);
  noticeTabs_ = new QTabWidget(noticeDock_);
  noticeStatusView_ = new QTextBrowser(noticeTabs_);
  noticeLogView_ = new QTextBrowser(noticeTabs_);
  noticeStatusView_->setOpenExternalLinks(false);
  noticeLogView_->setOpenExternalLinks(false);
  noticeTabs_->addTab(noticeStatusView_, QStringLiteral("recent"));
  noticeTabs_->addTab(noticeLogView_, QStringLiteral("events"));
  noticeDock_->setWidget(noticeTabs_);
  addDockWidget(Qt::RightDockWidgetArea, noticeDock_);
  noticeDock_->hide();
  noticeStatusView_->setPlainText(QStringLiteral("No events yet."));
  connect(noticeDock_, &QDockWidget::visibilityChanged, this, [this](bool visible) {
    if (visible) {
      noticeUnread_ = 0;
      UpdateNoticeBadge_();
    }
  });
}

void MainWindow::AppendEventLine_(QString const &category, QString const &text) {
  QString const plain = QStringLiteral("[%1] %2: %3")
                            .arg(TimeText(), category, text);
  noticeRecentLines_.push_back(plain);
  while (noticeRecentLines_.size() > 80) {
    noticeRecentLines_.removeFirst();
  }

  QStringList recent;
  qsizetype const start = std::max<qsizetype>(0, noticeRecentLines_.size() - 8);
  for (qsizetype i = start; i < noticeRecentLines_.size(); ++i) {
    recent.push_back(noticeRecentLines_.at(i));
  }
  if (noticeStatusView_) {
    noticeStatusView_->setPlainText(recent.join(QStringLiteral("\n")));
  }
  if (noticeLogView_) {
    noticeLogView_->append(QStringLiteral("<span style='color:#8b92a6;'>[%1]</span> "
                                          "<b>%2</b> %3")
                               .arg(TimeText(), category.toHtmlEscaped(),
                                    text.toHtmlEscaped()));
  }
  if (noticeDock_ && !noticeDock_->isVisible()) {
    ++noticeUnread_;
    UpdateNoticeBadge_();
  }
  statusBar()->showMessage(QStringLiteral("%1: %2").arg(category, text), 5000);
}

void MainWindow::UpdateNoticeBadge_() {
  if (!noticeBtn_) {
    return;
  }
  noticeBtn_->setText(noticeUnread_ > 0
                          ? QStringLiteral("notice (%1)").arg(noticeUnread_)
                          : QStringLiteral("notice"));
}

QString MainWindow::ConnectionLabel_(TOX_CONNECTION connection) const {
  switch (connection) {
    case TOX_CONNECTION_TCP:
      return QStringLiteral("online via TCP");
    case TOX_CONNECTION_UDP:
      return QStringLiteral("online via UDP");
    case TOX_CONNECTION_NONE:
    default:
      return QStringLiteral("offline");
  }
}
