#include "app/AppController.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringView>
#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace {
constexpr uint32_t kInvalidNumber = std::numeric_limits<uint32_t>::max();

struct BootstrapNode {
  QString address;
  uint16_t port{};
  QString publicKeyHex;
};

QString fromUtf8(std::string const &text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString timestampText() {
  return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
}

QString avatarText(QString const &displayName) {
  QString const trimmed = displayName.trimmed();
  if (trimmed.isEmpty()) {
    return QStringLiteral("?");
  }
  return trimmed.left(1).toUpper();
}

QJsonArray nodeArrayFromDocument(QJsonDocument const &doc) {
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

QString firstString(QJsonObject const &object,
                    std::initializer_list<QStringView> keys) {
  for (QStringView key: keys) {
    QJsonValue const value = object.value(key.toString());
    if (value.isString() && !value.toString().trimmed().isEmpty()) {
      return value.toString().trimmed();
    }
  }
  return {};
}

uint16_t portFromObject(QJsonObject const &object) {
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

std::vector<BootstrapNode> defaultBootstrapNodes() {
  return {{QStringLiteral("144.217.167.73"), 33445,
           QStringLiteral(
               "7E5668E0EE09E19F320AD47902419331FFEE147BB3606769CFBE921A2A2FD34C")},
          {QStringLiteral("tox.abilinski.com"), 33445,
           QStringLiteral(
               "10C00EB250C3233E343E2AEBA07115A5C28920E9C8D29492F6D00B29049EDC7E")},
          {QStringLiteral("198.199.98.108"), 33445,
           QStringLiteral("BEF0CFB37AF874BD17B9A8F9FE64C75521DB95A37D33C5BDB00E9CF5"
                          "8659C04F")}};
}

std::vector<BootstrapNode> loadBootstrapNodes(QStringList const &paths) {
  for (QString const &path: paths) {
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
    for (QJsonValue const value: nodeArrayFromDocument(doc)) {
      if (!value.isObject()) {
        continue;
      }
      QJsonObject const object = value.toObject();
      QString address = firstString(
          object, {QStringLiteral("address"), QStringLiteral("host"),
                   QStringLiteral("ipv4"), QStringLiteral("ip")});
      QString publicKey = firstString(
          object, {QStringLiteral("public_key"), QStringLiteral("publicKey"),
                   QStringLiteral("publicKeyHex"), QStringLiteral("key")});
      publicKey.remove(QChar(' '));
      publicKey.remove(QChar(':'));
      uint16_t const port = portFromObject(object);
      if (address.compare(QStringLiteral("NONE"), Qt::CaseInsensitive) != 0 &&
          !address.isEmpty() && port != 0 && publicKey.size() == 64) {
        nodes.push_back({address, port, publicKey});
      }
    }
    if (!nodes.empty()) {
      return nodes;
    }
  }
  return defaultBootstrapNodes();
}
}  // namespace

AppController::AppController(QObject *parent) : QObject(parent) {
  darkTheme_ = storageService_.themePreference() != QStringLiteral("light");

  iterateTimer_.setSingleShot(true);
  refreshTimer_.setInterval(1500);
  connect(&iterateTimer_, &QTimer::timeout, this, &AppController::onIterateTick);
  connect(&refreshTimer_, &QTimer::timeout, this, &AppController::onRefreshTick);

  addNotice(QStringLiteral("status"), QStringLiteral("QML shell ready"));
}

bool AppController::loggedIn() const { return loggedIn_; }
QString AppController::accountName() const { return accountName_; }
QString AppController::selfToxId() const { return selfToxId_; }
QString AppController::networkStatus() const { return networkStatus_; }
QString AppController::statusMessage() const { return statusMessage_; }
bool AppController::darkTheme() const { return darkTheme_; }
int AppController::noticeUnread() const { return noticeUnread_; }
QStringList AppController::knownAccounts() const {
  return profileService_.knownAccounts();
}
QString AppController::profileMessage() const { return profileMessage_; }
QString AppController::selectedConversationIdentifier() const {
  return selectedIdentifier_;
}
QString AppController::selectedConversationKind() const {
  return conversationKindText(selectedKind_);
}
QString AppController::selectedConversationTitle() const { return selectedTitle_; }
bool AppController::callActive() const { return callActive_; }
bool AppController::callVideoEnabled() const { return callVideoEnabled_; }
QString AppController::callTitle() const { return callTitle_; }
QString AppController::callStatus() const { return callStatus_; }

ContactListModel *AppController::friendModel() { return &friendModel_; }
ContactListModel *AppController::groupModel() { return &groupModel_; }
ChatMessageModel *AppController::chatModel() { return &chatModel_; }
NoticeModel *AppController::noticeModel() { return &noticeModel_; }

bool AppController::isKnownAccount(QString const &account) const {
  return profileService_.isKnownAccount(account);
}

void AppController::loginOrRegister(QString const &account,
                                    QString const &password,
                                    QString const &confirmPassword,
                                    bool registerNew) {
  ProfileResult const result = profileService_.loginOrRegister(
      account, password, confirmPassword, registerNew);
  setProfileMessage(result.message);
  addNotice(QStringLiteral("profile"), result.message,
            result.success ? QStringLiteral("info") : QStringLiteral("warning"));
  if (!result.success) {
    return;
  }

  accountName_ = account.trimmed();
  emit accountNameChanged();
  emit knownAccountsChanged();

  if (!startTox()) {
    return;
  }

  if (!loggedIn_) {
    loggedIn_ = true;
    emit loggedInChanged();
  }
  selectAssistant();
}

bool AppController::changePassword(QString const &account,
                                   QString const &oldPassword,
                                   QString const &newPassword,
                                   QString const &confirmPassword) {
  ProfileResult const result = profileService_.changePassword(
      account, oldPassword, newPassword, confirmPassword);
  setProfileMessage(result.message);
  addNotice(QStringLiteral("profile"), result.message,
            result.success ? QStringLiteral("info") : QStringLiteral("warning"));
  if (result.success) {
    emit knownAccountsChanged();
  }
  return result.success;
}

void AppController::exitApplication() { QCoreApplication::quit(); }

void AppController::copySelfId() {
  if (auto *clipboard = QGuiApplication::clipboard()) {
    clipboard->setText(selfToxId_);
    addNotice(QStringLiteral("profile"), QStringLiteral("ToxID 已复制。"));
  }
}

void AppController::toggleTheme() {
  darkTheme_ = !darkTheme_;
  storageService_.saveThemePreference(darkTheme_ ? QStringLiteral("dark")
                                                 : QStringLiteral("light"));
  emit darkThemeChanged();
  addNotice(QStringLiteral("ui"),
            darkTheme_ ? QStringLiteral("已切换到深色主题。")
                       : QStringLiteral("已切换到浅色主题。"));
}

bool AppController::setStatusMessage(QString const &message) {
  QString const text = message.trimmed();
  if (text.toUtf8().size() > 1007) {
    QString const error = QStringLiteral("个签过长，最多 1007 字节。");
    setProfileMessage(error);
    addNotice(QStringLiteral("profile"), error, QStringLiteral("warning"));
    return false;
  }

  try {
    if (tox_) {
      tox_->SetSelfStatusMessage(text.toStdString());
      persistSavedata();
    }
  } catch (std::exception const &e) {
    QString const error = QStringLiteral("设置个签失败：%1")
                              .arg(QString::fromUtf8(e.what()));
    setProfileMessage(error);
    addNotice(QStringLiteral("profile"), error, QStringLiteral("warning"));
    return false;
  }

  if (statusMessage_ != text) {
    statusMessage_ = text;
    emit statusMessageChanged();
  }
  QString const ok = text.isEmpty() ? QStringLiteral("个签已清空。")
                                    : QStringLiteral("个签已更新。");
  setProfileMessage(ok);
  addNotice(QStringLiteral("profile"), ok);
  return true;
}

void AppController::markNoticesRead() {
  if (noticeUnread_ == 0) {
    return;
  }
  noticeUnread_ = 0;
  emit noticeUnreadChanged();
}

void AppController::selectFriend(QString const &identifier) {
  ContactItem item;
  if (!friendModel_.contact(identifier, item)) {
    return;
  }
  bool ok = false;
  uint32_t const friendNumber = identifier.toUInt(&ok);
  if (ok) {
    friendUnreadCount_.remove(friendNumber);
    friendModel_.setUnreadCount(identifier, 0);
  } else if (stubFriends_.contains(identifier)) {
    stubFriends_[identifier].unreadCount = 0;
    refreshFriendList();
  }
  selectConversation(ConversationKind::Friend, identifier, item.displayName);
}

void AppController::selectGroup(QString const &identifier) {
  ContactItem item;
  if (!groupModel_.contact(identifier, item)) {
    return;
  }
  bool ok = false;
  uint32_t const conferenceNumber = identifier.toUInt(&ok);
  if (ok) {
    groupUnreadCount_.remove(conferenceNumber);
    groupModel_.setUnreadCount(identifier, 0);
  } else if (stubGroups_.contains(identifier)) {
    stubGroups_[identifier].unreadCount = 0;
    refreshGroupList();
  }
  selectConversation(ConversationKind::Group, identifier, item.displayName);
}

void AppController::selectAssistant() {
  selectConversation(ConversationKind::Assistant, QStringLiteral("assistant"),
                     QStringLiteral("AI 助手"));
  QString const key = conversationKey(selectedKind_, selectedIdentifier_);
  if (chatHistory_.value(key).isEmpty()) {
    appendCurrentSystemMessage(
        QStringLiteral("AI 助手是占位服务，真实模型后续接入。"));
  }
}

void AppController::addFriend(QString const &toxId, QString const &message) {
  QString cleaned = toxId.trimmed();
  cleaned.remove(QChar(' '));
  cleaned.remove(QChar(':'));
  QString request = message.trimmed().isEmpty()
                        ? QStringLiteral("您好，我是%1").arg(accountName_)
                        : message.trimmed();

  if (cleaned.size() == 76 && tox_) {
    try {
      uint32_t const friendNumber = tox_->AddFriend(cleaned.toStdString(),
                                                    request.toStdString());
      persistSavedata();
      refreshFriendList();
      selectFriend(QString::number(friendNumber));
      addNotice(QStringLiteral("friend"),
                QStringLiteral("好友请求已发送到 %1。")
                    .arg(cleaned.left(12)));
      return;
    } catch (std::exception const &e) {
      addNotice(QStringLiteral("friend"),
                QStringLiteral("真实好友请求失败，已保留为界面占位：%1")
                    .arg(QString::fromUtf8(e.what())),
                QStringLiteral("warning"));
    }
  }

  QString const id = makeStubFriendIdentifier();
  QString const title = cleaned.isEmpty()
                            ? QStringLiteral("待添加好友")
                            : QStringLiteral("好友 %1").arg(cleaned.left(8));
  stubFriends_.insert(id, {id,
                           title,
                           QStringLiteral("好友请求占位 · %1").arg(request),
                           0,
                           0,
                           avatarText(title),
                           cleaned,
                           false});
  refreshFriendList();
  selectFriend(id);
  addNotice(QStringLiteral("friend"),
            QStringLiteral("添加好友界面占位已创建。"),
            cleaned.size() == 76 ? QStringLiteral("info")
                                 : QStringLiteral("warning"));
}

void AppController::deleteSelectedFriend() {
  if (selectedKind_ != ConversationKind::Friend || selectedIdentifier_.isEmpty()) {
    addNotice(QStringLiteral("friend"), QStringLiteral("请先选择好友。"),
              QStringLiteral("warning"));
    return;
  }

  bool ok = false;
  uint32_t const friendNumber = selectedIdentifier_.toUInt(&ok);
  if (ok && tox_) {
    try {
      tox_->DeleteFriend(friendNumber);
      friendUnreadCount_.remove(friendNumber);
      friendConnectionCache_.remove(friendNumber);
      friendPublicKeyCache_.remove(friendNumber);
      persistSavedata();
    } catch (std::exception const &e) {
      addNotice(QStringLiteral("friend"),
                QStringLiteral("删除好友失败：%1").arg(QString::fromUtf8(e.what())),
                QStringLiteral("error"));
      return;
    }
  } else {
    stubFriends_.remove(selectedIdentifier_);
  }

  chatHistory_.remove(conversationKey(ConversationKind::Friend, selectedIdentifier_));
  refreshFriendList();
  addNotice(QStringLiteral("friend"), QStringLiteral("好友已从本地列表删除。"));
  selectAssistant();
}

void AppController::createGroup(QString const &title) {
  QString const groupTitle = title.trimmed().isEmpty() ? QStringLiteral("群聊")
                                                       : title.trimmed();
  if (tox_) {
    uint32_t const conferenceNumber = tox_->CreateConference();
    if (conferenceNumber != kInvalidNumber) {
      if (!groupTitle.isEmpty()) {
        tox_->SetConferenceTitle(conferenceNumber, groupTitle.toStdString());
      }
      persistSavedata();
      refreshGroupList();
      selectGroup(QString::number(conferenceNumber));
      addNotice(QStringLiteral("group"),
                QStringLiteral("已创建群聊 %1。").arg(groupTitle));
      return;
    }
  }

  QString const id = makeStubGroupIdentifier();
  stubGroups_.insert(id, {id,
                          groupTitle,
                          QStringLiteral("群聊元数据占位"),
                          0,
                          0,
                          avatarText(groupTitle),
                          {},
                          false});
  groupPersistenceService_.rememberGroup(id, groupTitle);
  refreshGroupList();
  selectGroup(id);
  addNotice(QStringLiteral("group"), QStringLiteral("群聊占位已创建。"));
}

void AppController::inviteSelectedFriendToGroup() {
  if (selectedKind_ != ConversationKind::Group || selectedIdentifier_.isEmpty()) {
    addNotice(QStringLiteral("group"), QStringLiteral("请先选择群聊。"),
              QStringLiteral("warning"));
    return;
  }
  if (!tox_) {
    addNotice(QStringLiteral("group"), QStringLiteral("邀请入群占位流程已触发。"));
    return;
  }

  std::vector<uint32_t> const friends = tox_->GetFriendList();
  if (friends.empty()) {
    addNotice(QStringLiteral("group"), QStringLiteral("当前没有可邀请的好友。"),
              QStringLiteral("warning"));
    return;
  }
  uint32_t const conferenceNumber = numericIdentifier(selectedIdentifier_);
  if (conferenceNumber == kInvalidNumber) {
    addNotice(QStringLiteral("group"), QStringLiteral("占位群聊不发送真实邀请。"));
    return;
  }
  if (tox_->InviteFriendToConference(friends.front(), conferenceNumber)) {
    addNotice(QStringLiteral("group"), QStringLiteral("已邀请第一个好友入群。"));
    return;
  }
  addNotice(QStringLiteral("group"), QStringLiteral("Tox 未接受群邀请请求。"),
            QStringLiteral("warning"));
}

void AppController::leaveSelectedGroup() {
  if (selectedKind_ != ConversationKind::Group || selectedIdentifier_.isEmpty()) {
    addNotice(QStringLiteral("group"), QStringLiteral("请先选择群聊。"),
              QStringLiteral("warning"));
    return;
  }

  bool ok = false;
  uint32_t const conferenceNumber = selectedIdentifier_.toUInt(&ok);
  if (ok && tox_) {
    if (!tox_->DeleteConference(conferenceNumber)) {
      addNotice(QStringLiteral("group"), QStringLiteral("Tox 未退出群聊。"),
                QStringLiteral("warning"));
      return;
    }
    groupUnreadCount_.remove(conferenceNumber);
    persistSavedata();
  } else {
    stubGroups_.remove(selectedIdentifier_);
    groupPersistenceService_.forgetGroup(selectedIdentifier_);
  }

  chatHistory_.remove(conversationKey(ConversationKind::Group, selectedIdentifier_));
  refreshGroupList();
  addNotice(QStringLiteral("group"), QStringLiteral("已退出群聊。"));
  selectAssistant();
}

void AppController::sendMessage(QString const &text) {
  QString const body = text.trimmed();
  if (body.isEmpty()) {
    return;
  }
  if (selectedKind_ == ConversationKind::None) {
    selectAssistant();
  }

  ChatMessageItem outgoing{makeMessageIdentifier(),
                           QStringLiteral("我"),
                           body,
                           timestampText(),
                           true,
                           false,
                           QStringLiteral("text"),
                           -1,
                           QStringLiteral("sent")};

  if (selectedKind_ == ConversationKind::Friend) {
    uint32_t const friendNumber = numericIdentifier(selectedIdentifier_);
    if (friendNumber != kInvalidNumber && tox_) {
      try {
        tox_->SendFriendMessage(friendNumber, body.toStdString());
      } catch (std::exception const &e) {
        outgoing.deliveryState = QStringLiteral("failed");
        addNotice(QStringLiteral("message"),
                  QStringLiteral("好友消息发送失败：%1")
                      .arg(QString::fromUtf8(e.what())),
                  QStringLiteral("warning"));
      }
    } else {
      outgoing.deliveryState = QStringLiteral("queued");
      addNotice(QStringLiteral("message"), QStringLiteral("占位好友消息已加入界面队列。"));
    }
    appendMessageToConversation(selectedKind_, selectedIdentifier_, outgoing);
    return;
  }

  if (selectedKind_ == ConversationKind::Group) {
    uint32_t const conferenceNumber = numericIdentifier(selectedIdentifier_);
    if (conferenceNumber != kInvalidNumber && tox_) {
      if (!tox_->SendConferenceMessage(conferenceNumber, body.toStdString())) {
        outgoing.deliveryState = QStringLiteral("failed");
        addNotice(QStringLiteral("message"), QStringLiteral("群聊消息未被 Tox 接受。"),
                  QStringLiteral("warning"));
      }
    } else {
      outgoing.deliveryState = QStringLiteral("queued");
      addNotice(QStringLiteral("message"), QStringLiteral("占位群聊消息已加入界面队列。"));
    }
    appendMessageToConversation(selectedKind_, selectedIdentifier_, outgoing);
    return;
  }

  appendMessageToConversation(ConversationKind::Assistant, QStringLiteral("assistant"),
                              outgoing);
  QTimer::singleShot(250, this, [this, body]() {
    appendMessageToConversation(
        ConversationKind::Assistant, QStringLiteral("assistant"),
        {makeMessageIdentifier(),
         QStringLiteral("AI 助手"),
         aiAssistantService_.reply(accountName_, body),
         timestampText(),
         false,
         false,
         QStringLiteral("assistant"),
         -1,
         QStringLiteral("received")});
  });
}

void AppController::sendFileStub() {
  addNotice(QStringLiteral("file"),
            fileTransferService_.placeholderSend(currentConversationTitle()));
  appendCurrentSystemMessage(QStringLiteral("文件发送占位流程已触发。"));
}

void AppController::startAudioCall() {
  if (selectedKind_ == ConversationKind::None) {
    addNotice(QStringLiteral("call"), QStringLiteral("请先选择会话。"),
              QStringLiteral("warning"));
    return;
  }
  callActive_ = true;
  callVideoEnabled_ = false;
  callTitle_ = currentConversationTitle();
  callStatus_ = QStringLiteral("等待对方接听...");
  emit callStateChanged();
  addNotice(QStringLiteral("call"), callService_.startCall(callTitle_, false));
  appendCurrentSystemMessage(QStringLiteral("音频通话占位窗口已打开。"));
  emit callShellRequested();
}

void AppController::startVideoCall() {
  if (selectedKind_ == ConversationKind::None) {
    addNotice(QStringLiteral("call"), QStringLiteral("请先选择会话。"),
              QStringLiteral("warning"));
    return;
  }
  callActive_ = true;
  callVideoEnabled_ = true;
  callTitle_ = currentConversationTitle();
  callStatus_ = QStringLiteral("等待对方接听...");
  emit callStateChanged();
  addNotice(QStringLiteral("call"), callService_.startCall(callTitle_, true));
  appendCurrentSystemMessage(QStringLiteral("视频通话占位窗口已打开。"));
  emit callShellRequested();
}

void AppController::answerCall() {
  if (!callActive_) {
    return;
  }
  callStatus_ = QStringLiteral("通话中");
  emit callStateChanged();
}

void AppController::hangupCall() {
  if (!callActive_) {
    return;
  }
  addNotice(QStringLiteral("call"), callService_.hangupCall(callTitle_));
  appendCurrentSystemMessage(QStringLiteral("通话占位流程已结束。"));
  callActive_ = false;
  callStatus_ = QStringLiteral("通话已结束");
  emit callStateChanged();
}

void AppController::onIterateTick() {
  if (!tox_) {
    return;
  }
  try {
    tox_->Iterate();
    TOX_CONNECTION const connection = tox_->GetSelfConnectionStatus();
    if (connection != selfConnection_) {
      selfConnection_ = connection;
      networkStatus_ = QStringLiteral("network: %1").arg(connectionLabel(connection));
      emit networkStatusChanged();
      addNotice(QStringLiteral("network"),
                QStringLiteral("network is %1").arg(connectionLabel(connection)));
    }
  } catch (std::exception const &e) {
    addNotice(QStringLiteral("status"),
              QStringLiteral("iterate failed: %1").arg(QString::fromUtf8(e.what())),
              QStringLiteral("warning"));
  }
  scheduleIterate();
}

void AppController::onRefreshTick() {
  refreshFriendList();
  refreshGroupList();
}

bool AppController::startTox() {
  iterateTimer_.stop();
  refreshTimer_.stop();
  tox_.reset();
  selfConnection_ = TOX_CONNECTION_NONE;
  networkStatus_ = QStringLiteral("network: starting");
  emit networkStatusChanged();

  try {
    statusMessage_.clear();
    emit statusMessageChanged();
    QByteArray const savedata = storageService_.loadToxSavedata(accountName_);
    bool restored = false;
    if (!savedata.isEmpty()) {
      try {
        std::vector<uint8_t> data(static_cast<size_t>(savedata.size()));
        std::memcpy(data.data(), savedata.constData(),
                    static_cast<size_t>(savedata.size()));
        tox_ = std::make_unique<ToxCore::ToxCoreWrapper>(data);
        restored = true;
        addNotice(QStringLiteral("tox"), QStringLiteral("已从数据库恢复 Tox 身份。"));
      } catch (std::exception const &e) {
        addNotice(QStringLiteral("tox"),
                  QStringLiteral("恢复 Tox 身份失败，已创建新身份：%1")
                      .arg(QString::fromUtf8(e.what())),
                  QStringLiteral("warning"));
        tox_ = std::make_unique<ToxCore::ToxCoreWrapper>();
      }
    } else {
      tox_ = std::make_unique<ToxCore::ToxCoreWrapper>();
      addNotice(QStringLiteral("tox"), QStringLiteral("已创建新的 Tox 身份。"));
    }

    tox_->SetSelfName(accountName_.toStdString());
    if (restored) {
      statusMessage_ = QString::fromStdString(tox_->GetSelfStatusMessage());
      emit statusMessageChanged();
    } else {
      tox_->SetSelfStatusMessage({});
    }
    selfToxId_ = QString::fromStdString(tox_->GetSelfAddressHex());
    emit selfToxIdChanged();

    registerToxCallbacks();
    bootstrapFromConfig();
    refreshFriendList();
    refreshGroupList();
    persistSavedata();
    scheduleIterate();
    refreshTimer_.start();
    addNotice(QStringLiteral("tox"), QStringLiteral("Tox identity ready."));
    return true;
  } catch (std::exception const &e) {
    tox_.reset();
    selfToxId_ = QStringLiteral("Tox startup failed");
    networkStatus_ = QStringLiteral("network: offline");
    emit selfToxIdChanged();
    emit networkStatusChanged();
    QString const message =
        QStringLiteral("Tox startup failed: %1").arg(QString::fromUtf8(e.what()));
    setProfileMessage(message);
    addNotice(QStringLiteral("tox"), message, QStringLiteral("error"));
    return false;
  }
}

void AppController::registerToxCallbacks() {
  if (!tox_) {
    return;
  }

  tox_->SetOnFriendRequest([this](std::string const &publicKeyHex,
                                  std::string const &message) {
    QString const publicKey = QString::fromStdString(publicKeyHex);
    addNotice(QStringLiteral("friend"),
              QStringLiteral("收到来自 %1 的好友请求：%2")
                  .arg(publicKey.left(12), fromUtf8(message)));
  });

  tox_->SetOnFriendConnectionStatus(
      [this](uint32_t friendNumber, TOX_CONNECTION status) {
        friendConnectionCache_[friendNumber] = status;
        addNotice(QStringLiteral("friend"),
                  QStringLiteral("%1 is %2")
                      .arg(friendDisplayName(friendNumber), connectionLabel(status)));
        refreshFriendList();
      });

  tox_->SetOnFriendMessage([this](uint32_t friendNumber, TOX_MESSAGE_TYPE,
                                  std::string const &message) {
    QString const identifier = QString::number(friendNumber);
    ChatMessageItem item{makeMessageIdentifier(),
                         friendDisplayName(friendNumber),
                         fromUtf8(message),
                         timestampText(),
                         false,
                         false,
                         QStringLiteral("text"),
                         -1,
                         QStringLiteral("received")};
    appendMessageToConversation(ConversationKind::Friend, identifier, item);
    if (selectedKind_ != ConversationKind::Friend || selectedIdentifier_ != identifier) {
      friendUnreadCount_[friendNumber] = friendUnreadCount_.value(friendNumber, 0) + 1;
      refreshFriendList();
    }
    addNotice(QStringLiteral("friend"),
              QStringLiteral("message from %1").arg(friendDisplayName(friendNumber)));
  });

  tox_->SetOnConferenceInvite([this](uint32_t friendNumber, TOX_CONFERENCE_TYPE,
                                     std::vector<uint8_t> const &) {
    addNotice(QStringLiteral("group"),
              QStringLiteral("收到来自 %1 的群聊邀请。")
                  .arg(friendDisplayName(friendNumber)));
  });

  tox_->SetOnConferenceConnected([this](uint32_t conferenceNumber) {
    refreshGroupList();
    selectGroup(QString::number(conferenceNumber));
    addNotice(QStringLiteral("group"),
              QStringLiteral("joined %1").arg(groupDisplayName(conferenceNumber)));
  });

  tox_->SetOnConferenceMessage([this](uint32_t conferenceNumber,
                                      uint32_t peerNumber, TOX_MESSAGE_TYPE,
                                      std::string const &message) {
    QString sender = fromUtf8(tox_->GetConferencePeerName(conferenceNumber, peerNumber));
    if (sender.isEmpty()) {
      sender = QStringLiteral("peer %1").arg(peerNumber);
    }
    QString const identifier = QString::number(conferenceNumber);
    appendMessageToConversation(
        ConversationKind::Group, identifier,
        {makeMessageIdentifier(), sender, fromUtf8(message), timestampText(), false,
         false, QStringLiteral("text"), -1, QStringLiteral("received")});
    if (selectedKind_ != ConversationKind::Group || selectedIdentifier_ != identifier) {
      groupUnreadCount_[conferenceNumber] =
          groupUnreadCount_.value(conferenceNumber, 0) + 1;
      refreshGroupList();
    }
    addNotice(QStringLiteral("group"),
              QStringLiteral("message in %1").arg(groupDisplayName(conferenceNumber)));
  });

  tox_->SetOnConferenceTitle([this](uint32_t, uint32_t, std::string const &) {
    refreshGroupList();
  });
  tox_->SetOnConferencePeerName([this](uint32_t, uint32_t, std::string const &) {
    refreshGroupList();
  });
  tox_->SetOnConferencePeerListChanged([this](uint32_t) { refreshGroupList(); });
}

void AppController::bootstrapFromConfig() {
  if (!tox_) {
    return;
  }
  QStringList const paths{QDir(QCoreApplication::applicationDirPath())
                              .filePath(QStringLiteral("bootstrap_nodes.json")),
                          QDir::current().filePath(QStringLiteral("bootstrap_nodes.json"))};
  std::vector<BootstrapNode> const nodes = loadBootstrapNodes(paths);
  int connected = 0;
  for (BootstrapNode const &node: nodes) {
    try {
      tox_->Bootstrap(node.address.toStdString(), node.port,
                      node.publicKeyHex.toStdString());
      try {
        tox_->AddTcpRelay(node.address.toStdString(), node.port,
                          node.publicKeyHex.toStdString());
      } catch (...) {}
      ++connected;
      addNotice(QStringLiteral("network"),
                QStringLiteral("bootstrapped through %1:%2")
                    .arg(node.address)
                    .arg(node.port));
    } catch (std::exception const &e) {
      addNotice(QStringLiteral("network"),
                QStringLiteral("bootstrap failed for %1:%2 (%3)")
                    .arg(node.address)
                    .arg(node.port)
                    .arg(QString::fromUtf8(e.what())),
                QStringLiteral("warning"));
    }
  }
  if (connected == 0) {
    addNotice(QStringLiteral("network"),
              QStringLiteral("no bootstrap node accepted the request"),
              QStringLiteral("warning"));
  }
}

void AppController::scheduleIterate() {
  if (!tox_) {
    return;
  }
  uint32_t const interval = tox_->IterationIntervalMs();
  iterateTimer_.start(static_cast<int>(std::clamp<uint32_t>(interval, 10, 1000)));
}

void AppController::refreshFriendList() {
  QVector<ContactItem> contacts;
  contacts.reserve(stubFriends_.size() + 8);
  for (ContactItem const &contact: stubFriends_) {
    contacts.push_back(contact);
  }

  if (tox_) {
    try {
      for (uint32_t const friendNumber: tox_->GetFriendList()) {
        contacts.push_back(contactFromFriend(friendNumber));
      }
    } catch (std::exception const &e) {
      addNotice(QStringLiteral("friend"),
                QStringLiteral("friend refresh failed: %1")
                    .arg(QString::fromUtf8(e.what())),
                QStringLiteral("warning"));
    }
  }
  friendModel_.setContacts(contacts);
}

void AppController::refreshGroupList() {
  QVector<ContactItem> contacts;
  contacts.reserve(stubGroups_.size() + 8);
  for (ContactItem const &contact: stubGroups_) {
    contacts.push_back(contact);
  }

  if (tox_) {
    for (uint32_t const conferenceNumber: tox_->GetConferenceList()) {
      contacts.push_back(contactFromGroup(conferenceNumber));
    }
  }
  groupModel_.setContacts(contacts);
}

void AppController::persistSavedata() {
  if (!tox_ || accountName_.isEmpty()) {
    return;
  }
  std::vector<uint8_t> const savedata = tox_->GetSavedata();
  QByteArray bytes(reinterpret_cast<char const *>(savedata.data()),
                   static_cast<qsizetype>(savedata.size()));
  storageService_.saveToxSavedata(accountName_, bytes);
}

void AppController::addNotice(QString const &category, QString const &text,
                              QString const &severity) {
  noticeModel_.addNotice({timestampText(), category, text, severity});
  ++noticeUnread_;
  emit noticeUnreadChanged();
}

void AppController::setProfileMessage(QString const &message) {
  if (profileMessage_ == message) {
    return;
  }
  profileMessage_ = message;
  emit profileMessageChanged();
}

void AppController::selectConversation(ConversationKind kind,
                                       QString const &identifier,
                                       QString const &title) {
  selectedKind_ = kind;
  selectedIdentifier_ = identifier;
  selectedTitle_ = title;
  emit selectedConversationChanged();
  loadSelectedConversation();
}

void AppController::loadSelectedConversation() {
  QString const key = conversationKey(selectedKind_, selectedIdentifier_);
  chatModel_.setMessages(chatHistory_.value(key));
}

void AppController::appendMessageToConversation(ConversationKind kind,
                                                QString const &identifier,
                                                ChatMessageItem message) {
  QString const key = conversationKey(kind, identifier);
  chatHistory_[key].push_back(message);
  if (selectedKind_ == kind && selectedIdentifier_ == identifier) {
    chatModel_.appendMessage(std::move(message));
  }
}

void AppController::appendCurrentSystemMessage(QString const &text,
                                               QString const &severity) {
  if (selectedKind_ == ConversationKind::None) {
    return;
  }
  appendMessageToConversation(
      selectedKind_, selectedIdentifier_,
      {makeMessageIdentifier(), QStringLiteral("system"), text, timestampText(),
       false, true, severity, -1, QStringLiteral("local")});
}

QString AppController::conversationKey(ConversationKind kind,
                                       QString const &identifier) const {
  return conversationKindText(kind) + QStringLiteral(":") + identifier;
}

QString AppController::conversationKindText(ConversationKind kind) const {
  switch (kind) {
  case ConversationKind::Friend: return QStringLiteral("friend");
  case ConversationKind::Group: return QStringLiteral("group");
  case ConversationKind::Assistant: return QStringLiteral("assistant");
  case ConversationKind::None:
  default: return QStringLiteral("none");
  }
}

QString AppController::currentConversationTitle() const {
  return selectedTitle_.isEmpty() ? QStringLiteral("当前会话") : selectedTitle_;
}

QString AppController::friendDisplayName(uint32_t friendNumber) const {
  if (!tox_ || friendNumber == kInvalidNumber) {
    return QStringLiteral("friend");
  }
  try {
    QString const name = fromUtf8(tox_->GetFriendName(friendNumber)).trimmed();
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

QString AppController::friendPublicKey(uint32_t friendNumber) {
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

QString AppController::groupDisplayName(uint32_t conferenceNumber) const {
  if (!tox_ || conferenceNumber == kInvalidNumber) {
    return QStringLiteral("group");
  }
  QString const title = fromUtf8(tox_->GetConferenceTitle(conferenceNumber)).trimmed();
  if (!title.isEmpty()) {
    return title;
  }
  return QStringLiteral("group %1").arg(conferenceNumber);
}

QString AppController::connectionLabel(TOX_CONNECTION connection) const {
  switch (connection) {
  case TOX_CONNECTION_TCP: return QStringLiteral("TCP");
  case TOX_CONNECTION_UDP: return QStringLiteral("UDP");
  case TOX_CONNECTION_NONE:
  default: return QStringLiteral("NONE");
  }
}

ContactItem AppController::contactFromFriend(uint32_t friendNumber) const {
  TOX_CONNECTION connection = TOX_CONNECTION_NONE;
  try {
    connection = tox_->GetFriendConnectionStatus(friendNumber);
  } catch (...) {}
  QString publicKey;
  try {
    publicKey = QString::fromStdString(tox_->GetFriendPublicKeyHex(friendNumber));
  } catch (...) {}
  QString const name = friendDisplayName(friendNumber);
  return {QString::number(friendNumber),
          name,
          QStringLiteral("%1 · %2").arg(connectionLabel(connection),
                                        publicKey.left(12)),
          static_cast<int>(connection),
          friendUnreadCount_.value(friendNumber, 0),
          avatarText(name),
          publicKey,
          false};
}

ContactItem AppController::contactFromGroup(uint32_t conferenceNumber) const {
  QString const name = groupDisplayName(conferenceNumber);
  uint32_t peers = 0;
  try {
    peers = tox_->GetConferencePeerCount(conferenceNumber);
  } catch (...) {}
  return {QString::number(conferenceNumber),
          name,
          QStringLiteral("%1 peers").arg(peers),
          1,
          groupUnreadCount_.value(conferenceNumber, 0),
          avatarText(name),
          {},
          false};
}

uint32_t AppController::numericIdentifier(QString const &identifier) const {
  bool ok = false;
  uint const value = identifier.toUInt(&ok);
  if (!ok) {
    return kInvalidNumber;
  }
  return value;
}

QString AppController::makeMessageIdentifier() {
  ++messageSequence_;
  return QStringLiteral("message-%1").arg(messageSequence_);
}

QString AppController::makeStubFriendIdentifier() {
  ++stubFriendSequence_;
  return QStringLiteral("stub-friend-%1").arg(stubFriendSequence_);
}

QString AppController::makeStubGroupIdentifier() {
  ++stubGroupSequence_;
  return QStringLiteral("stub-group-%1").arg(stubGroupSequence_);
}
