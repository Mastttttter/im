#include "app/AppController.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringView>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

namespace {
constexpr uint32_t kInvalidNumber = std::numeric_limits<uint32_t>::max();
constexpr int kCallRecordMessageType = 100;
constexpr int kFileRecordMessageType = 101;
constexpr int kAiRecentMessageLimit = 200;
constexpr int kAiRequestHistoryMessageLimit = 10000;
constexpr int kDefaultAiContextLength = 32768;
constexpr int kMaxAiContextLength = 1000000;

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

QString timestampText(qint64 timestampMs) {
  return QDateTime::fromMSecsSinceEpoch(timestampMs).toString(
      QStringLiteral("HH:mm:ss"));
}

QString fileTransferKey(uint32_t friendNumber, uint32_t fileNumber) {
  return QStringLiteral("%1:%2").arg(friendNumber).arg(fileNumber);
}

QString localFilePathFromUrl(QString const &localFileUrlOrPath) {
  QString const text = localFileUrlOrPath.trimmed();
  if (text.isEmpty()) {
    return {};
  }
  QUrl const url(text);
  if (url.isValid() && url.isLocalFile()) {
    return url.toLocalFile();
  }
  return text;
}

QString safeFileName(QString fileName) {
  fileName = fileName.trimmed();
  fileName.replace(QChar('/'), QChar('_'));
  fileName.replace(QChar('\\'), QChar('_'));
  return fileName.isEmpty() ? QStringLiteral("received-file") : fileName;
}

QString suggestedReceivedFileUrl(QString const &fileName) {
  QString directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  if (directory.isEmpty()) {
    directory = QDir::homePath();
  }
  QDir().mkpath(directory);
  return QUrl::fromLocalFile(QDir(directory).filePath(safeFileName(fileName))).toString();
}

QString fileSizeText(uint64_t bytes) {
  static QStringList const units{QStringLiteral("B"), QStringLiteral("KB"),
                                 QStringLiteral("MB"), QStringLiteral("GB")};
  double value = static_cast<double>(bytes);
  qsizetype unit = 0;
  while (value >= 1024.0 && unit + 1 < units.size()) {
    value /= 1024.0;
    ++unit;
  }
  if (unit == 0) {
    return QStringLiteral("%1 B").arg(static_cast<qulonglong>(bytes));
  }
  return QStringLiteral("%1 %2").arg(value, 0, 'f', 1).arg(units.at(unit));
}

qint64 estimatedAiContextUnits(QString const &role, QString const &content) {
  qint64 const roleUnits = (role.toUtf8().size() + 3) / 4;
  qint64 const contentUnits = (content.toUtf8().size() + 3) / 4;
  return roleUnits + contentUnits + 8;
}

int fileProgress(uint64_t transferred, uint64_t fileSize) {
  if (fileSize == 0) {
    return 100;
  }
  double const ratio = static_cast<double>(transferred) * 100.0 /
                       static_cast<double>(fileSize);
  return std::clamp(static_cast<int>(ratio), 0, 100);
}

QString fileStatusText(QString const &status) {
  if (status == QStringLiteral("completed")) {
    return QStringLiteral("已完成");
  }
  if (status == QStringLiteral("cancelled")) {
    return QStringLiteral("已取消");
  }
  if (status == QStringLiteral("failed")) {
    return QStringLiteral("失败");
  }
  if (status == QStringLiteral("receiving")) {
    return QStringLiteral("接收中");
  }
  if (status == QStringLiteral("sending")) {
    return QStringLiteral("发送中");
  }
  return QStringLiteral("等待中");
}

QString fileMessageText(bool isSending, QString const &fileName,
                        uint64_t fileSize, QString const &status,
                        int progress) {
  QString progressText;
  if (progress >= 0 && status != QStringLiteral("completed") &&
      status != QStringLiteral("cancelled") && status != QStringLiteral("failed")) {
    progressText = QStringLiteral(" · %1%").arg(progress);
  }
  return QStringLiteral("%1：%2\n%3 · %4%5")
      .arg(isSending ? QStringLiteral("发送文件") : QStringLiteral("接收文件"),
           fileName, fileSizeText(fileSize), fileStatusText(status), progressText);
}

QString fileRecordBody(QString const &fileName, uint64_t fileSize,
                       QString const &status) {
  return QStringLiteral("FILE:%1:%2:%3")
      .arg(fileName)
      .arg(static_cast<qulonglong>(fileSize))
      .arg(status);
}

QString formatCallDuration(int seconds) {
  if (seconds <= 0) {
    return {};
  }
  int const hours = seconds / 3600;
  int const minutes = (seconds % 3600) / 60;
  int const secs = seconds % 60;
  if (hours > 0) {
    return QStringLiteral("%1小时%2分钟%3秒").arg(hours).arg(minutes).arg(secs);
  }
  if (minutes > 0) {
    return QStringLiteral("%1分钟%2秒").arg(minutes).arg(secs);
  }
  return QStringLiteral("%1秒").arg(secs);
}

QString callRecordBody(bool videoEnabled, QString const &statusKey,
                       int durationSeconds) {
  QString body = QStringLiteral("CALL:%1:%2")
                     .arg(videoEnabled ? QStringLiteral("VIDEO")
                                       : QStringLiteral("AUDIO"),
                          statusKey);
  if (durationSeconds > 0) {
    body += QStringLiteral(":%1").arg(durationSeconds);
  }
  return body;
}

QString callStatusText(QString const &statusKey) {
  if (statusKey == QStringLiteral("HANGUP_SELF")) {
    return QStringLiteral("您已挂断");
  }
  if (statusKey == QStringLiteral("HANGUP_REMOTE")) {
    return QStringLiteral("对方已挂断");
  }
  if (statusKey == QStringLiteral("CANCEL_SELF")) {
    return QStringLiteral("您已取消");
  }
  if (statusKey == QStringLiteral("CANCEL_REMOTE")) {
    return QStringLiteral("对方已取消");
  }
  if (statusKey == QStringLiteral("REJECT_SELF")) {
    return QStringLiteral("您已拒绝");
  }
  if (statusKey == QStringLiteral("REJECT_REMOTE")) {
    return QStringLiteral("对方已拒绝");
  }
  if (statusKey == QStringLiteral("ERROR")) {
    return QStringLiteral("通话异常结束");
  }
  return QStringLiteral("通话已结束");
}

bool callStatusIsLocal(QString const &statusKey) {
  return statusKey.endsWith(QStringLiteral("_SELF"));
}

QString callMessageText(bool videoEnabled, QString const &statusKey,
                        int durationSeconds) {
  QString text = QStringLiteral("%1 %2 · %3")
                     .arg(videoEnabled ? QStringLiteral("📹")
                                       : QStringLiteral("📞"),
                          videoEnabled ? QStringLiteral("视频通话")
                                       : QStringLiteral("语音通话"),
                          callStatusText(statusKey));
  QString const duration = formatCallDuration(durationSeconds);
  if (!duration.isEmpty() && statusKey.startsWith(QStringLiteral("HANGUP_"))) {
    text += QStringLiteral("，时长：%1").arg(duration);
  }
  return text;
}

QString avatarText(QString const &displayName) {
  QString const trimmed = displayName.trimmed();
  if (trimmed.isEmpty()) {
    return QStringLiteral("?");
  }
  return trimmed.left(1).toUpper();
}

bool isHexString(QString const &text) {
  static QRegularExpression const pattern(QStringLiteral("^[0-9A-Fa-f]+$"));
  return pattern.match(text).hasMatch();
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
  aiClient_ = std::make_unique<AiCurlClient>(this);
  connect(aiClient_.get(), &AiCurlClient::ReplyReady, this,
          &AppController::onAiReplyReady);
  connect(aiClient_.get(), &AiCurlClient::ErrorOccurred, this,
          &AppController::onAiError);

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
QString AppController::aiBaseUrl() const {
  return aiClient_ ? aiClient_->BaseUrl() : QStringLiteral("https://api.tokenpony.cn/v1");
}
QString AppController::aiModelName() const {
  return aiClient_ ? aiClient_->ModelName() : QStringLiteral("qwen3-8b");
}
QString AppController::aiApiKey() const {
  return aiClient_ ? aiClient_->ApiKey() : QString();
}
bool AppController::aiApiKeyConfigured() const {
  return aiClient_ && !aiClient_->ApiKey().isEmpty();
}
QString AppController::aiProvider() const {
  return aiClient_ ? aiClient_->Provider() : QStringLiteral("tokenpony");
}
double AppController::aiTemperature() const {
  return aiClient_ ? aiClient_->Temperature() : 0.0;
}
int AppController::aiMaxTokens() const {
  return aiClient_ ? aiClient_->MaxTokens() : 4096;
}
int AppController::aiContextLength() const {
  return aiContextLength_;
}
bool AppController::aiBusy() const {
  return aiClient_ && aiClient_->IsBusy();
}

bool AppController::hasPendingFriendRequest() const {
  return !pendingFriendRequests_.isEmpty();
}

QString AppController::pendingFriendRequestPublicKey() const {
  return pendingFriendRequests_.isEmpty() ? QString() : pendingFriendRequests_.first().publicKey;
}

QString AppController::pendingFriendRequestMessage() const {
  return pendingFriendRequests_.isEmpty() ? QString() : pendingFriendRequests_.first().message;
}

bool AppController::hasSelectedFriend() const {
  return selectedKind_ == ConversationKind::Friend && !selectedIdentifier_.isEmpty();
}

QString AppController::selectedFriendDisplayName() const {
  if (!hasSelectedFriend()) {
    return {};
  }
  ContactItem item;
  if (friendModel_.contact(selectedIdentifier_, item)) {
    return item.displayName;
  }
  return selectedTitle_;
}

QString AppController::selectedFriendRemark() const {
  QString const publicKey = publicKeyForFriendIdentifier(selectedIdentifier_);
  if (publicKey.isEmpty()) {
    return {};
  }
  return storageService_.contactNickname(publicKey);
}

bool AppController::callActive() const { return callPhase_ != CallPhase::Idle; }
bool AppController::callIncoming() const {
  return callPhase_ == CallPhase::IncomingRinging;
}
bool AppController::callCanAnswer() const {
  return callPhase_ == CallPhase::IncomingRinging;
}
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
  loadAiSettings();

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
}

bool AppController::saveAiSettings(QString const &baseUrl,
                                   QString const &modelName,
                                   QString const &apiKey,
                                   QString const &provider,
                                   double temperature, int maxTokens,
                                   int contextLength) {
  QString const normalizedBaseUrl = baseUrl.trimmed();
  QString const normalizedModel = modelName.trimmed();
  QString const normalizedApiKey = apiKey.trimmed();
  QString const normalizedProvider = provider.trimmed();
  if (normalizedBaseUrl.isEmpty()) {
    addNotice(QStringLiteral("ai"), QStringLiteral("请输入 AI Base URL。"),
              QStringLiteral("warning"));
    return false;
  }
  if (normalizedModel.isEmpty()) {
    addNotice(QStringLiteral("ai"), QStringLiteral("请输入 AI 模型名称。"),
              QStringLiteral("warning"));
    return false;
  }
  if (!aiClient_) {
    return false;
  }

  double const normalizedTemperature =
      std::isfinite(temperature) ? std::clamp(temperature, 0.0, 2.0) : 0.0;
  int const normalizedMaxTokens = std::clamp(maxTokens, 64, 65536);
  int const normalizedContextLength =
      std::clamp(contextLength, 0, kMaxAiContextLength);

  aiClient_->SetBaseUrl(normalizedBaseUrl);
  aiClient_->SetModelName(normalizedModel);
  aiClient_->SetApiKey(normalizedApiKey);
  aiClient_->SetProvider(normalizedProvider);
  aiClient_->SetTemperature(normalizedTemperature);
  aiClient_->SetMaxTokens(normalizedMaxTokens);
  aiContextLength_ = normalizedContextLength;

  storageService_.setMetaValue(QStringLiteral("ai.base_url"), aiClient_->BaseUrl());
  storageService_.setMetaValue(QStringLiteral("ai.model_name"), aiClient_->ModelName());
  storageService_.setMetaValue(QStringLiteral("ai.api_key"), aiClient_->ApiKey());
  storageService_.setMetaValue(QStringLiteral("ai.provider"), aiClient_->Provider());
  storageService_.setMetaValue(QStringLiteral("ai.temperature"),
                               QString::number(aiClient_->Temperature(), 'f', 1));
  storageService_.setMetaValue(QStringLiteral("ai.max_tokens"),
                               QString::number(aiClient_->MaxTokens()));
  storageService_.setMetaValue(QStringLiteral("ai.context_length"),
                               QString::number(aiContextLength_));

  emit aiSettingsChanged();
  addNotice(QStringLiteral("ai"),
            normalizedApiKey.isEmpty()
                ? QStringLiteral("AI 设置已保存；未设置 API Key，暂不能请求。")
                : QStringLiteral("AI 设置已保存。"),
            normalizedApiKey.isEmpty() ? QStringLiteral("warning")
                                       : QStringLiteral("info"));
  return true;
}

void AppController::addFriend(QString const &toxId, QString const &message) {
  QString cleaned = toxId.trimmed();
  cleaned.remove(QChar(' '));
  cleaned.remove(QChar(':'));
  cleaned = cleaned.toUpper();
  QString request = message.trimmed().isEmpty()
                        ? QStringLiteral("您好，我是%1").arg(accountName_)
                        : message.trimmed();

  if (cleaned.size() != 76 || !isHexString(cleaned)) {
    addNotice(QStringLiteral("friend"), QStringLiteral("请输入 76 位十六进制 ToxID。"),
              QStringLiteral("warning"));
    return;
  }
  if (!tox_) {
    addNotice(QStringLiteral("friend"), QStringLiteral("Tox 尚未就绪，无法发送好友请求。"),
              QStringLiteral("warning"));
    return;
  }

  try {
    uint32_t const friendNumber = tox_->AddFriend(cleaned.toStdString(),
                                                  request.toStdString());
    storageService_.ensureContact(cleaned.left(64));
    persistSavedata();
    refreshFriendList();
    selectFriend(QString::number(friendNumber));
    addNotice(QStringLiteral("friend"),
              QStringLiteral("好友请求已发送到 %1。").arg(cleaned.left(12)));
  } catch (std::exception const &e) {
    addNotice(QStringLiteral("friend"),
              QStringLiteral("好友请求发送失败：%1").arg(QString::fromUtf8(e.what())),
              QStringLiteral("warning"));
  }
}

bool AppController::acceptPendingFriendRequest() {
  if (pendingFriendRequests_.isEmpty()) {
    addNotice(QStringLiteral("friend"), QStringLiteral("当前没有待处理的好友请求。"),
              QStringLiteral("warning"));
    return false;
  }
  if (!tox_) {
    addNotice(QStringLiteral("friend"), QStringLiteral("Tox 尚未就绪，暂时无法同意好友请求。"),
              QStringLiteral("warning"));
    return false;
  }

  PendingFriendRequest const request = pendingFriendRequests_.first();
  try {
    for (uint32_t const existingFriend: tox_->GetFriendList()) {
      if (friendPublicKey(existingFriend) == request.publicKey) {
        storageService_.ensureContact(request.publicKey);
        pendingFriendRequests_.removeFirst();
        emit pendingFriendRequestChanged();
        refreshFriendList();
        selectFriend(QString::number(existingFriend));
        addNotice(QStringLiteral("friend"), QStringLiteral("好友已在列表中。"));
        if (!pendingFriendRequests_.isEmpty()) {
          emit friendRequestPromptRequested();
        }
        return true;
      }
    }
    uint32_t const friendNumber = tox_->AddFriendNoRequest(request.publicKey.toStdString());
    storageService_.ensureContact(request.publicKey);
    persistSavedata();
    pendingFriendRequests_.removeFirst();
    emit pendingFriendRequestChanged();
    refreshFriendList();
    selectFriend(QString::number(friendNumber));
    addNotice(QStringLiteral("friend"),
              QStringLiteral("已同意来自 %1 的好友请求。").arg(request.publicKey.left(12)));
    if (!pendingFriendRequests_.isEmpty()) {
      emit friendRequestPromptRequested();
    }
    return true;
  } catch (std::exception const &e) {
    addNotice(QStringLiteral("friend"),
              QStringLiteral("同意好友请求失败：%1").arg(QString::fromUtf8(e.what())),
              QStringLiteral("warning"));
    return false;
  }
}

bool AppController::rejectPendingFriendRequest() {
  if (pendingFriendRequests_.isEmpty()) {
    return false;
  }
  PendingFriendRequest const request = pendingFriendRequests_.first();
  pendingFriendRequests_.removeFirst();
  emit pendingFriendRequestChanged();
  addNotice(QStringLiteral("friend"),
            QStringLiteral("已拒绝来自 %1 的好友请求。").arg(request.publicKey.left(12)));
  if (!pendingFriendRequests_.isEmpty()) {
    emit friendRequestPromptRequested();
  }
  return true;
}

void AppController::deleteSelectedFriend() {
  if (selectedKind_ != ConversationKind::Friend || selectedIdentifier_.isEmpty()) {
    addNotice(QStringLiteral("friend"), QStringLiteral("请先选择好友。"),
              QStringLiteral("warning"));
    return;
  }

  bool ok = false;
  uint32_t const friendNumber = selectedIdentifier_.toUInt(&ok);
  if (ok) {
    if (!tox_) {
      addNotice(QStringLiteral("friend"), QStringLiteral("Tox 尚未就绪，无法删除真实好友。"),
                QStringLiteral("warning"));
      return;
    }
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

  refreshFriendList();
  addNotice(QStringLiteral("friend"), QStringLiteral("好友已从本地列表删除。"));
  selectAssistant();
}

bool AppController::setSelectedFriendRemark(QString const &remark) {
  if (!hasSelectedFriend()) {
    addNotice(QStringLiteral("friend"), QStringLiteral("请先选择好友。"),
              QStringLiteral("warning"));
    return false;
  }
  QString const publicKey = publicKeyForFriendIdentifier(selectedIdentifier_);
  if (publicKey.isEmpty()) {
    addNotice(QStringLiteral("friend"), QStringLiteral("当前好友没有可保存备注的公钥。"),
              QStringLiteral("warning"));
    return false;
  }

  storageService_.setContactNickname(publicKey, remark);
  refreshFriendList();
  refreshSelectedFriendTitle();
  addNotice(QStringLiteral("friend"),
            remark.trimmed().isEmpty() ? QStringLiteral("好友备注已清除。")
                                       : QStringLiteral("好友备注已更新。"));
  return true;
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

void AppController::clearAssistantHistory() {
  if (aiClient_ && aiClient_->IsBusy()) {
    addNotice(QStringLiteral("ai"), QStringLiteral("AI 正在生成回复，暂不能清空历史。"),
              QStringLiteral("warning"));
    return;
  }

  storageService_.clearAiMessages();
  chatHistory_.remove(conversationKey(ConversationKind::Assistant,
                                      QStringLiteral("assistant")));
  if (selectedKind_ == ConversationKind::Assistant &&
      selectedIdentifier_ == QStringLiteral("assistant")) {
    chatModel_.clear();
  }
  addNotice(QStringLiteral("ai"), QStringLiteral("AI 助手历史已清空。"));
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

  sendAssistantMessage(body);
}

void AppController::sendFile(QString const &localFileUrlOrPath) {
  if (selectedKind_ != ConversationKind::Friend || selectedIdentifier_.isEmpty()) {
    addNotice(QStringLiteral("file"), QStringLiteral("请先选择一个在线好友。"),
              QStringLiteral("warning"));
    return;
  }
  if (!tox_) {
    addNotice(QStringLiteral("file"), QStringLiteral("Tox 尚未就绪，无法发送文件。"),
              QStringLiteral("warning"));
    return;
  }

  uint32_t const friendNumber = numericIdentifier(selectedIdentifier_);
  if (friendNumber == kInvalidNumber) {
    addNotice(QStringLiteral("file"), QStringLiteral("占位好友不能发送真实文件。"),
              QStringLiteral("warning"));
    return;
  }

  try {
    if (tox_->GetFriendConnectionStatus(friendNumber) == TOX_CONNECTION_NONE) {
      addNotice(QStringLiteral("file"), QStringLiteral("好友离线，无法发送文件。"),
                QStringLiteral("warning"));
      return;
    }
  } catch (std::exception const &e) {
    addNotice(QStringLiteral("file"),
              QStringLiteral("检查好友在线状态失败：%1").arg(QString::fromUtf8(e.what())),
              QStringLiteral("warning"));
    return;
  }

  QString const filePath = localFilePathFromUrl(localFileUrlOrPath);
  QFileInfo const info(filePath);
  if (!info.exists() || !info.isFile() || !info.isReadable()) {
    addNotice(QStringLiteral("file"), QStringLiteral("请选择可读取的本地文件。"),
              QStringLiteral("warning"));
    return;
  }

  QString const fileName = info.fileName();
  QByteArray const fileNameBytes = fileName.toUtf8();
  if (fileNameBytes.isEmpty() || fileNameBytes.size() > TOX_MAX_FILENAME_LENGTH) {
    addNotice(QStringLiteral("file"), QStringLiteral("文件名为空或过长。"),
              QStringLiteral("warning"));
    return;
  }
  uint64_t const fileSize = static_cast<uint64_t>(std::max<qint64>(0, info.size()));
  std::string const fileNameUtf8(fileNameBytes.constData(),
                                 static_cast<size_t>(fileNameBytes.size()));

  uint32_t fileNumber = kInvalidNumber;
  try {
    fileNumber = tox_->SendFile(friendNumber,
                                std::filesystem::path(filePath.toStdString()),
                                fileNameUtf8);
  } catch (std::exception const &e) {
    addNotice(QStringLiteral("file"),
              QStringLiteral("发起文件传输失败：%1").arg(QString::fromUtf8(e.what())),
              QStringLiteral("warning"));
    return;
  }
  if (fileNumber == kInvalidNumber) {
    addNotice(QStringLiteral("file"), QStringLiteral("Tox 未接受文件传输请求。"),
              QStringLiteral("warning"));
    return;
  }

  auto stream = std::make_shared<std::fstream>(filePath.toStdString(),
                                               std::ios::binary | std::ios::in);
  if (!stream->is_open()) {
    tox_->ControlFileTransfer(friendNumber, fileNumber, TOX_FILE_CONTROL_CANCEL);
    addNotice(QStringLiteral("file"), QStringLiteral("打开待发送文件失败。"),
              QStringLiteral("warning"));
    return;
  }

  QString const messageIdentifier = makeMessageIdentifier();
  FileTransfer transfer{friendNumber,
                        fileNumber,
                        filePath,
                        fileName,
                        messageIdentifier,
                        fileSize,
                        0,
                        true,
                        stream};
  fileTransfers_.insert(fileTransferKey(friendNumber, fileNumber), transfer);
  appendMessageToConversation(
      ConversationKind::Friend, QString::number(friendNumber),
      {messageIdentifier, QStringLiteral("我"),
       fileMessageText(true, fileName, fileSize, QStringLiteral("sending"),
                       fileProgress(0, fileSize)),
       timestampText(), true, false, QStringLiteral("file"),
       fileProgress(0, fileSize), QStringLiteral("sending")});
  addNotice(QStringLiteral("file"), QStringLiteral("文件传输已开始。"));
}

void AppController::acceptIncomingFile(QString const &localFileUrlOrPath) {
  if (pendingIncomingFiles_.isEmpty()) {
    return;
  }
  PendingIncomingFile const pending = pendingIncomingFiles_.takeFirst();
  QString const filePath = localFilePathFromUrl(localFileUrlOrPath);
  QFileInfo const info(filePath);
  if (filePath.isEmpty()) {
    if (tox_) {
      tox_->ControlFileTransfer(pending.friendNumber, pending.fileNumber,
                                TOX_FILE_CONTROL_CANCEL);
    }
    appendFileRecord(pending.friendNumber, false, pending.fileName,
                     pending.fileSize, QStringLiteral("cancelled"));
    promptNextIncomingFile();
    return;
  }
  QDir parentDir(info.absolutePath());
  if (!parentDir.exists() && !parentDir.mkpath(QStringLiteral("."))) {
    tox_->ControlFileTransfer(pending.friendNumber, pending.fileNumber,
                              TOX_FILE_CONTROL_CANCEL);
    appendFileRecord(pending.friendNumber, false, pending.fileName,
                     pending.fileSize, QStringLiteral("failed"));
    addNotice(QStringLiteral("file"), QStringLiteral("创建保存目录失败。"),
              QStringLiteral("warning"));
    promptNextIncomingFile();
    return;
  }

  auto stream = std::make_shared<std::fstream>(filePath.toStdString(),
                                               std::ios::binary | std::ios::out |
                                                   std::ios::trunc);
  if (!stream->is_open()) {
    tox_->ControlFileTransfer(pending.friendNumber, pending.fileNumber,
                              TOX_FILE_CONTROL_CANCEL);
    appendFileRecord(pending.friendNumber, false, pending.fileName,
                     pending.fileSize, QStringLiteral("failed"));
    addNotice(QStringLiteral("file"), QStringLiteral("打开保存文件失败。"),
              QStringLiteral("warning"));
    promptNextIncomingFile();
    return;
  }

  QString const messageIdentifier = makeMessageIdentifier();
  FileTransfer transfer{pending.friendNumber,
                        pending.fileNumber,
                        filePath,
                        pending.fileName,
                        messageIdentifier,
                        pending.fileSize,
                        0,
                        false,
                        stream};
  fileTransfers_.insert(fileTransferKey(pending.friendNumber, pending.fileNumber),
                        transfer);
  appendMessageToConversation(
      ConversationKind::Friend, QString::number(pending.friendNumber),
      {messageIdentifier, friendDisplayName(pending.friendNumber),
       fileMessageText(false, pending.fileName, pending.fileSize,
                       QStringLiteral("receiving"), fileProgress(0, pending.fileSize)),
       timestampText(), false, false, QStringLiteral("file"),
       fileProgress(0, pending.fileSize), QStringLiteral("receiving")});

  bool const resumed =
      tox_ && tox_->ControlFileTransfer(pending.friendNumber, pending.fileNumber,
                                        TOX_FILE_CONTROL_RESUME);
  if (!resumed) {
    updateMessageInConversation(ConversationKind::Friend,
                                QString::number(pending.friendNumber),
                                messageIdentifier,
                                fileMessageText(false, pending.fileName,
                                                pending.fileSize,
                                                QStringLiteral("failed"), -1),
                                -1, QStringLiteral("failed"));
    fileTransfers_.remove(fileTransferKey(pending.friendNumber, pending.fileNumber));
    saveFileRecord(pending.friendNumber, false, pending.fileName,
                   pending.fileSize, QStringLiteral("failed"));
    addNotice(QStringLiteral("file"), QStringLiteral("接受文件传输失败。"),
              QStringLiteral("warning"));
  } else {
    addNotice(QStringLiteral("file"), QStringLiteral("已接受文件传输。"));
  }
  promptNextIncomingFile();
}

void AppController::rejectIncomingFile() {
  if (pendingIncomingFiles_.isEmpty()) {
    return;
  }
  PendingIncomingFile const pending = pendingIncomingFiles_.takeFirst();
  if (tox_) {
    tox_->ControlFileTransfer(pending.friendNumber, pending.fileNumber,
                              TOX_FILE_CONTROL_CANCEL);
  }
  appendFileRecord(pending.friendNumber, false, pending.fileName, pending.fileSize,
                   QStringLiteral("cancelled"));
  addNotice(QStringLiteral("file"), QStringLiteral("已拒绝文件传输。"));
  promptNextIncomingFile();
}

void AppController::sendFileStub() {
  addNotice(QStringLiteral("file"), QStringLiteral("请选择文件后发送。"));
}

void AppController::startAudioCall() { beginOutgoingCall(false); }

void AppController::startVideoCall() { beginOutgoingCall(true); }

void AppController::answerCall() {
  if (callPhase_ != CallPhase::IncomingRinging || !tox_ ||
      currentCallFriend_ == kInvalidNumber) {
    return;
  }

  if (tox_->Answer(currentCallFriend_, callOptions(callVideoEnabled_)) != 0) {
    addNotice(QStringLiteral("call"), QStringLiteral("接听失败。"),
              QStringLiteral("warning"));
    return;
  }

  markCallActive();
  if (callVideoEnabled_ && video_) {
    video_->StartCamera();
  }
}

void AppController::hangupCall() {
  if (callPhase_ == CallPhase::Idle) {
    return;
  }

  QString statusKey;
  if (callPhase_ == CallPhase::Active) {
    statusKey = QStringLiteral("HANGUP_SELF");
  } else if (callPhase_ == CallPhase::OutgoingRinging) {
    statusKey = QStringLiteral("CANCEL_SELF");
  } else if (callPhase_ == CallPhase::IncomingRinging) {
    statusKey = QStringLiteral("REJECT_SELF");
  } else {
    statusKey = QStringLiteral("HANGUP_SELF");
  }

  localHangupPending_ = true;
  callPhase_ = CallPhase::Ending;
  if (!callRecordWritten_ && currentCallFriend_ != kInvalidNumber) {
    appendCallRecord(currentCallFriend_, callVideoEnabled_, statusKey);
    callRecordWritten_ = true;
  }
  if (tox_ && currentCallFriend_ != kInvalidNumber) {
    tox_->Hangup(currentCallFriend_);
  }
  resetCallState();
}

void AppController::setLocalVideoSink(QObject *sink) {
  if (video_) {
    video_->SetLocalPreviewSink(sink);
  }
}

void AppController::setRemoteVideoSink(QObject *sink) {
  if (video_) {
    video_->SetRemoteVideoSink(sink);
  }
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
    sendActiveCallMedia();
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
  pendingFriendRequests_.clear();
  friendUnreadCount_.clear();
  groupUnreadCount_.clear();
  friendConnectionCache_.clear();
  friendPublicKeyCache_.clear();
  fileTransfers_.clear();
  pendingIncomingFiles_.clear();
  resetCallState();
  emit pendingFriendRequestChanged();
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

    configureCallMedia();
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
    QString const publicKey = QString::fromStdString(publicKeyHex).toUpper();
    QString const requestMessage = fromUtf8(message);
    for (PendingFriendRequest &request: pendingFriendRequests_) {
      if (request.publicKey == publicKey) {
        request.message = requestMessage;
        emit pendingFriendRequestChanged();
        emit friendRequestPromptRequested();
        return;
      }
    }
    pendingFriendRequests_.push_back({publicKey, requestMessage});
    addNotice(QStringLiteral("friend"),
              QStringLiteral("收到来自 %1 的好友请求：%2")
                  .arg(publicKey.left(12), requestMessage));
    emit pendingFriendRequestChanged();
    emit friendRequestPromptRequested();
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

  tox_->SetOnFileReceive([this](uint32_t friendNumber, uint32_t fileNumber,
                                std::string const &fileName, uint64_t fileSize) {
    onFileReceive(friendNumber, fileNumber, fileName, fileSize);
  });
  tox_->SetOnFileRecvControl([this](uint32_t friendNumber, uint32_t fileNumber,
                                    TOX_FILE_CONTROL control) {
    onFileRecvControl(friendNumber, fileNumber, control);
  });
  tox_->SetOnFileChunkRequest([this](uint32_t friendNumber, uint32_t fileNumber,
                                     uint64_t position, size_t length) {
    onFileChunkRequest(friendNumber, fileNumber, position, length);
  });
  tox_->SetOnFileRecvChunk([this](uint32_t friendNumber, uint32_t fileNumber,
                                  uint64_t position, uint8_t const *data,
                                  size_t length) {
    onFileRecvChunk(friendNumber, fileNumber, position, data, length);
  });

  tox_->SetOnCall([this](uint32_t friendNumber, bool audioEnabled,
                         bool videoEnabled) {
    onIncomingCall(friendNumber, audioEnabled, videoEnabled);
  });
  tox_->SetOnCallState([this](uint32_t friendNumber,
                              TOXAV_FRIEND_CALL_STATE state) {
    onCallState(friendNumber, state);
  });
  tox_->SetOnAudioFrame([this](uint32_t friendNumber, int16_t const *pcm,
                               size_t samples, uint8_t channels,
                               uint32_t samplingRate) {
    onAudioFrame(friendNumber, pcm, samples, channels, samplingRate);
  });
  tox_->SetOnVideoFrame([this](uint32_t friendNumber, uint16_t width,
                               uint16_t height, uint8_t const *y,
                               uint8_t const *u, uint8_t const *v) {
    onVideoFrame(friendNumber, width, height, y, u, v);
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
  refreshSelectedFriendTitle();
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
  QVector<ChatMessageItem> messages = chatHistory_.value(key);
  if (messages.isEmpty() && selectedKind_ == ConversationKind::Friend) {
    uint32_t const friendNumber = numericIdentifier(selectedIdentifier_);
    if (friendNumber != kInvalidNumber) {
      loadPersistedFriendRecords(friendNumber, messages);
      if (!messages.isEmpty()) {
        chatHistory_.insert(key, messages);
      }
    }
  }
  if (messages.isEmpty() && selectedKind_ == ConversationKind::Assistant &&
      selectedIdentifier_ == QStringLiteral("assistant")) {
    loadPersistedAiMessages(messages);
    if (!messages.isEmpty()) {
      chatHistory_.insert(key, messages);
    }
  }
  chatModel_.setMessages(messages);
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

void AppController::sendAssistantMessage(QString const &body) {
  if (!aiClient_) {
    return;
  }
  if (aiClient_->IsBusy()) {
    addNotice(QStringLiteral("ai"), QStringLiteral("AI 正在生成回复，请稍候。"),
              QStringLiteral("warning"));
    return;
  }
  if (aiClient_->ApiKey().isEmpty()) {
    addNotice(QStringLiteral("ai"), QStringLiteral("请先在 AI 助手设置中填写 API Key。"),
              QStringLiteral("warning"));
    if (selectedKind_ == ConversationKind::Assistant) {
      appendCurrentSystemMessage(QStringLiteral("请先打开 AI 助手设置并填写 API Key。"),
                                 QStringLiteral("warning"));
    }
    return;
  }

  QVector<AiChatMessage> const history = buildAssistantRequestHistory();
  qint64 const now = QDateTime::currentMSecsSinceEpoch();
  appendAssistantMessage(true, body, now, true);
  aiClient_->SendAiMessage(body, history);
  emit aiBusyChanged();
}

QVector<AiChatMessage> AppController::buildAssistantRequestHistory() const {
  if (aiContextLength_ <= 0) {
    return {};
  }

  QVector<AiChatMessage> candidates;
  QList<Persistence::SqliteStorage::MessageRow> const rows =
      storageService_.loadRecentAiMessages(kAiRequestHistoryMessageLimit);
  if (!rows.isEmpty()) {
    candidates.reserve(rows.size());
    for (Persistence::SqliteStorage::MessageRow const &row: rows) {
      QString const content = row.body.trimmed();
      if (content.isEmpty()) {
        continue;
      }
      candidates.push_back({row.direction == 1 ? QStringLiteral("user")
                                                : QStringLiteral("assistant"),
                            content});
    }
  } else {
    QString const key = conversationKey(ConversationKind::Assistant,
                                        QStringLiteral("assistant"));
    QVector<ChatMessageItem> const cached = chatHistory_.value(key);
    candidates.reserve(cached.size());
    for (ChatMessageItem const &message: cached) {
      QString const content = message.text.trimmed();
      if (message.system || content.isEmpty()) {
        continue;
      }
      if (message.outgoing) {
        if (message.deliveryState == QStringLiteral("sent")) {
          candidates.push_back({QStringLiteral("user"), content});
        }
        continue;
      }
      if (message.messageType == QStringLiteral("assistant") &&
          message.deliveryState == QStringLiteral("received")) {
        candidates.push_back({QStringLiteral("assistant"), content});
      }
    }
  }

  QVector<AiChatMessage> selected;
  qint64 usedUnits = 0;
  qint64 const maxUnits = aiContextLength_;
  for (qsizetype i = candidates.size() - 1; i >= 0; --i) {
    AiChatMessage const &message = candidates.at(i);
    qint64 const units = estimatedAiContextUnits(message.role, message.content);
    if (usedUnits + units > maxUnits) {
      break;
    }
    selected.prepend(message);
    usedUnits += units;
  }
  return selected;
}

void AppController::appendAssistantMessage(bool outgoing, QString const &text,
                                           qint64 createdAtMs, bool saveToDb) {
  appendMessageToConversation(
      ConversationKind::Assistant, QStringLiteral("assistant"),
      {makeMessageIdentifier(),
       outgoing ? QStringLiteral("我") : QStringLiteral("AI 助手"),
       text,
       timestampText(createdAtMs),
       outgoing,
       false,
       outgoing ? QStringLiteral("text") : QStringLiteral("assistant"),
       -1,
       outgoing ? QStringLiteral("sent") : QStringLiteral("received")});
  if (saveToDb) {
    storageService_.saveAiMessage(outgoing, text, createdAtMs);
  }
}

void AppController::loadPersistedAiMessages(QVector<ChatMessageItem> &messages) {
  for (Persistence::SqliteStorage::MessageRow const &row:
       storageService_.loadRecentAiMessages(kAiRecentMessageLimit)) {
    bool const outgoing = row.direction == 1;
    messages.push_back({makeMessageIdentifier(),
                        outgoing ? QStringLiteral("我")
                                 : QStringLiteral("AI 助手"),
                        row.body,
                        timestampText(row.createdAtMs),
                        outgoing,
                        false,
                        outgoing ? QStringLiteral("text")
                                 : QStringLiteral("assistant"),
                        -1,
                        outgoing ? QStringLiteral("sent")
                                 : QStringLiteral("received")});
  }
}

void AppController::loadAiSettings() {
  if (!aiClient_) {
    return;
  }

  aiClient_->SetBaseUrl(QStringLiteral("https://api.tokenpony.cn/v1"));
  aiClient_->SetModelName(QStringLiteral("qwen3-8b"));
  aiClient_->SetApiKey({});
  aiClient_->SetProvider(QStringLiteral("tokenpony"));
  aiClient_->SetTemperature(0.0);
  aiClient_->SetMaxTokens(4096);
  aiContextLength_ = kDefaultAiContextLength;

  QString const baseUrl = storageService_.metaValue(QStringLiteral("ai.base_url"),
                                                    aiClient_->BaseUrl());
  if (!baseUrl.trimmed().isEmpty()) {
    aiClient_->SetBaseUrl(baseUrl);
  }
  QString const modelName = storageService_.metaValue(QStringLiteral("ai.model_name"),
                                                      aiClient_->ModelName());
  if (!modelName.trimmed().isEmpty()) {
    aiClient_->SetModelName(modelName);
  }
  aiClient_->SetApiKey(storageService_.metaValue(QStringLiteral("ai.api_key")));
  aiClient_->SetProvider(storageService_.metaValue(QStringLiteral("ai.provider"),
                                                   aiClient_->Provider()));
  QString const temperature = storageService_.metaValue(QStringLiteral("ai.temperature"));
  bool temperatureOk = false;
  double const parsedTemperature = temperature.toDouble(&temperatureOk);
  if (temperatureOk && std::isfinite(parsedTemperature)) {
    aiClient_->SetTemperature(std::clamp(parsedTemperature, 0.0, 2.0));
  }
  QString const maxTokens = storageService_.metaValue(QStringLiteral("ai.max_tokens"));
  bool maxTokensOk = false;
  int const parsedMaxTokens = maxTokens.toInt(&maxTokensOk);
  if (maxTokensOk) {
    aiClient_->SetMaxTokens(std::clamp(parsedMaxTokens, 64, 65536));
  }
  QString const contextLength = storageService_.metaValue(QStringLiteral("ai.context_length"));
  bool contextLengthOk = false;
  int const parsedContextLength = contextLength.toInt(&contextLengthOk);
  if (contextLengthOk) {
    aiContextLength_ = std::clamp(parsedContextLength, 0, kMaxAiContextLength);
  }

  emit aiSettingsChanged();
  emit aiBusyChanged();
}

void AppController::onAiReplyReady(QString const &aiText) {
  emit aiBusyChanged();
  appendAssistantMessage(false, aiText, QDateTime::currentMSecsSinceEpoch(), true);
}

void AppController::onAiError(QString const &errorMsg) {
  emit aiBusyChanged();
  appendAssistantMessage(false, errorMsg, QDateTime::currentMSecsSinceEpoch(), false);
  addNotice(QStringLiteral("ai"), errorMsg, QStringLiteral("warning"));
}

void AppController::updateMessageInConversation(
    ConversationKind kind, QString const &identifier,
    QString const &messageIdentifier, QString const &text, int progress,
    QString const &deliveryState) {
  QString const key = conversationKey(kind, identifier);
  QVector<ChatMessageItem> &messages = chatHistory_[key];
  for (ChatMessageItem &message: messages) {
    if (message.identifier != messageIdentifier) {
      continue;
    }
    message.text = text;
    message.progress = progress;
    message.deliveryState = deliveryState;
    break;
  }
  if (selectedKind_ == kind && selectedIdentifier_ == identifier) {
    chatModel_.updateMessage(messageIdentifier, text, progress, deliveryState);
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

void AppController::appendFileRecord(uint32_t friendNumber, bool isSending,
                                     QString const &fileName, uint64_t fileSize,
                                     QString const &status, bool saveToDb) {
  int const progress = status == QStringLiteral("completed") ? 100 : -1;
  appendMessageToConversation(
      ConversationKind::Friend, QString::number(friendNumber),
      {makeMessageIdentifier(),
       isSending ? QStringLiteral("我") : friendDisplayName(friendNumber),
       fileMessageText(isSending, fileName, fileSize, status, progress),
       timestampText(), isSending, false, QStringLiteral("file"), progress,
       status});

  if (saveToDb) {
    saveFileRecord(friendNumber, isSending, fileName, fileSize, status);
  }
}

void AppController::saveFileRecord(uint32_t friendNumber, bool isSending,
                                   QString const &fileName, uint64_t fileSize,
                                   QString const &status) {
  QString const publicKey = friendPublicKey(friendNumber);
  if (publicKey.isEmpty()) {
    return;
  }
  storageService_.saveFriendMessage(
      publicKey, isSending ? 1 : 0, kFileRecordMessageType,
      fileRecordBody(fileName, fileSize, status),
      QDateTime::currentMSecsSinceEpoch());
}

void AppController::appendCallRecord(uint32_t friendNumber, bool videoEnabled,
                                      QString const &statusKey,
                                      int durationSeconds, bool saveToDb) {
  int const recordDuration = durationSeconds >= 0
                                 ? durationSeconds
                                 : (statusKey.startsWith(QStringLiteral("HANGUP_"))
                                        ? currentCallDurationSeconds()
                                        : 0);
  bool const isLocal = callStatusIsLocal(statusKey);
  appendMessageToConversation(
      ConversationKind::Friend, QString::number(friendNumber),
      {makeMessageIdentifier(),
       isLocal ? QStringLiteral("我") : friendDisplayName(friendNumber),
       callMessageText(videoEnabled, statusKey, recordDuration), timestampText(),
       isLocal, false, QStringLiteral("call"), -1, statusKey});

  if (saveToDb) {
    saveCallRecord(friendNumber, videoEnabled, statusKey, recordDuration);
  }
}

void AppController::saveCallRecord(uint32_t friendNumber, bool videoEnabled,
                                    QString const &statusKey,
                                    int durationSeconds) {
  QString const publicKey = friendPublicKey(friendNumber);
  if (publicKey.isEmpty()) {
    return;
  }
  storageService_.saveFriendMessage(
      publicKey, callStatusIsLocal(statusKey) ? 1 : 0, kCallRecordMessageType,
      callRecordBody(videoEnabled, statusKey, durationSeconds),
      QDateTime::currentMSecsSinceEpoch());
}

void AppController::updateFileTransferMessage(FileTransfer const &transfer,
                                              QString const &status,
                                              QString const &deliveryState) {
  int const progress = status == QStringLiteral("completed")
                           ? 100
                           : fileProgress(transfer.transferred, transfer.fileSize);
  updateMessageInConversation(
      ConversationKind::Friend, QString::number(transfer.friendNumber),
      transfer.messageIdentifier,
      fileMessageText(transfer.isSending, transfer.fileName, transfer.fileSize,
                      status, progress),
      progress, deliveryState);
}

void AppController::loadPersistedFriendRecords(
    uint32_t friendNumber, QVector<ChatMessageItem> &messages) {
  QString const publicKey = friendPublicKey(friendNumber);
  if (publicKey.isEmpty()) {
    return;
  }
  for (Persistence::SqliteStorage::MessageRow const &row:
       storageService_.loadRecentFriendMessages(publicKey, 200)) {
    if (row.toxMessageType == kCallRecordMessageType &&
        row.body.startsWith(QStringLiteral("CALL:"))) {
      QStringList const parts = row.body.split(QChar(':'));
      if (parts.size() < 3) {
        continue;
      }
      bool const videoEnabled = parts.at(1) == QStringLiteral("VIDEO");
      QString const statusKey = parts.at(2);
      bool durationOk = false;
      int const durationSeconds = parts.size() >= 4 ? parts.at(3).toInt(&durationOk) : 0;
      bool const isLocal = row.direction == 1;
      messages.push_back({makeMessageIdentifier(),
                          isLocal ? QStringLiteral("我")
                                  : friendDisplayName(friendNumber),
                          callMessageText(videoEnabled, statusKey,
                                          durationOk ? durationSeconds : 0),
                          timestampText(row.createdAtMs),
                          isLocal,
                          false,
                          QStringLiteral("call"),
                          -1,
                          statusKey});
      continue;
    }

    if (row.toxMessageType != kFileRecordMessageType ||
        !row.body.startsWith(QStringLiteral("FILE:"))) {
      continue;
    }
    QString const body = row.body.mid(5);
    int const statusSeparator = body.lastIndexOf(QChar(':'));
    int const sizeSeparator =
        statusSeparator > 0 ? body.lastIndexOf(QChar(':'), statusSeparator - 1) : -1;
    if (sizeSeparator <= 0 || statusSeparator <= sizeSeparator) {
      continue;
    }
    bool ok = false;
    QString const fileName = body.left(sizeSeparator);
    uint64_t const fileSize = body.mid(sizeSeparator + 1,
                                       statusSeparator - sizeSeparator - 1)
                                  .toULongLong(&ok);
    QString const status = body.mid(statusSeparator + 1);
    if (!ok || fileName.isEmpty()) {
      continue;
    }
    bool const isSending = row.direction == 1;
    int const progress = status == QStringLiteral("completed") ? 100 : -1;
    messages.push_back({makeMessageIdentifier(),
                        isSending ? QStringLiteral("我")
                                  : friendDisplayName(friendNumber),
                        fileMessageText(isSending, fileName, fileSize, status,
                                        progress),
                        timestampText(row.createdAtMs),
                        isSending,
                        false,
                        QStringLiteral("file"),
                        progress,
                        status});
  }
}

void AppController::onFileReceive(uint32_t friendNumber, uint32_t fileNumber,
                                  std::string const &fileName,
                                  uint64_t fileSize) {
  QString const safeName = safeFileName(fromUtf8(fileName));
  pendingIncomingFiles_.push_back({friendNumber, fileNumber, safeName, fileSize});
  addNotice(QStringLiteral("file"),
            QStringLiteral("收到来自 %1 的文件：%2")
                .arg(friendDisplayName(friendNumber), safeName));
  if (pendingIncomingFiles_.size() == 1) {
    promptNextIncomingFile();
  }
}

void AppController::onFileRecvControl(uint32_t friendNumber, uint32_t fileNumber,
                                      TOX_FILE_CONTROL control) {
  QString const key = fileTransferKey(friendNumber, fileNumber);
  auto transferIt = fileTransfers_.find(key);
  if (transferIt == fileTransfers_.end()) {
    if (control == TOX_FILE_CONTROL_CANCEL) {
      for (qsizetype i = 0; i < pendingIncomingFiles_.size(); ++i) {
        PendingIncomingFile const &pending = pendingIncomingFiles_.at(i);
        if (pending.friendNumber == friendNumber && pending.fileNumber == fileNumber) {
          pendingIncomingFiles_.removeAt(i);
          addNotice(QStringLiteral("file"), QStringLiteral("对方已取消文件传输。"));
          promptNextIncomingFile();
          return;
        }
      }
    }
    return;
  }
  FileTransfer &transfer = transferIt.value();
  if (control == TOX_FILE_CONTROL_CANCEL) {
    updateMessageInConversation(
        ConversationKind::Friend, QString::number(friendNumber),
        transfer.messageIdentifier,
        fileMessageText(transfer.isSending, transfer.fileName, transfer.fileSize,
                        QStringLiteral("cancelled"), -1),
        -1, QStringLiteral("cancelled"));
    if (transfer.fileStream) {
      transfer.fileStream->close();
    }
    saveFileRecord(friendNumber, transfer.isSending, transfer.fileName,
                   transfer.fileSize, QStringLiteral("cancelled"));
    fileTransfers_.remove(key);
    addNotice(QStringLiteral("file"), QStringLiteral("文件传输已取消。"));
    return;
  }
  if (control == TOX_FILE_CONTROL_PAUSE) {
    updateFileTransferMessage(transfer, QStringLiteral("queued"),
                              QStringLiteral("queued"));
    return;
  }
  if (control == TOX_FILE_CONTROL_RESUME) {
    updateFileTransferMessage(
        transfer, transfer.isSending ? QStringLiteral("sending")
                                     : QStringLiteral("receiving"),
        transfer.isSending ? QStringLiteral("sending")
                           : QStringLiteral("receiving"));
  }
}

void AppController::onFileChunkRequest(uint32_t friendNumber, uint32_t fileNumber,
                                       uint64_t position, size_t length) {
  QString const key = fileTransferKey(friendNumber, fileNumber);
  auto transferIt = fileTransfers_.find(key);
  if (transferIt == fileTransfers_.end()) {
    return;
  }
  FileTransfer &transfer = transferIt.value();
  if (length == 0) {
    transfer.transferred = transfer.fileSize;
    updateFileTransferMessage(transfer, QStringLiteral("completed"),
                              QStringLiteral("completed"));
    if (transfer.fileStream) {
      transfer.fileStream->close();
    }
    saveFileRecord(friendNumber, true, transfer.fileName, transfer.fileSize,
                   QStringLiteral("completed"));
    fileTransfers_.remove(key);
    addNotice(QStringLiteral("file"), QStringLiteral("文件发送完成。"));
    return;
  }
  if (!transfer.fileStream || !transfer.fileStream->is_open()) {
    tox_->ControlFileTransfer(friendNumber, fileNumber, TOX_FILE_CONTROL_CANCEL);
    updateFileTransferMessage(transfer, QStringLiteral("failed"),
                              QStringLiteral("failed"));
    saveFileRecord(friendNumber, true, transfer.fileName, transfer.fileSize,
                   QStringLiteral("failed"));
    fileTransfers_.remove(key);
    return;
  }

  std::vector<uint8_t> buffer(length);
  transfer.fileStream->seekg(static_cast<std::streamoff>(position));
  transfer.fileStream->read(reinterpret_cast<char *>(buffer.data()),
                            static_cast<std::streamsize>(length));
  std::streamsize const bytesRead = transfer.fileStream->gcount();
  if (bytesRead <= 0) {
    tox_->ControlFileTransfer(friendNumber, fileNumber, TOX_FILE_CONTROL_CANCEL);
    updateFileTransferMessage(transfer, QStringLiteral("failed"),
                              QStringLiteral("failed"));
    saveFileRecord(friendNumber, true, transfer.fileName, transfer.fileSize,
                   QStringLiteral("failed"));
    fileTransfers_.remove(key);
    return;
  }

  if (!tox_->SendFileChunk(friendNumber, fileNumber, position, buffer.data(),
                           static_cast<size_t>(bytesRead))) {
    tox_->ControlFileTransfer(friendNumber, fileNumber, TOX_FILE_CONTROL_CANCEL);
    updateFileTransferMessage(transfer, QStringLiteral("failed"),
                              QStringLiteral("failed"));
    saveFileRecord(friendNumber, true, transfer.fileName, transfer.fileSize,
                   QStringLiteral("failed"));
    fileTransfers_.remove(key);
    return;
  }
  transfer.transferred = std::min<uint64_t>(
      transfer.fileSize, std::max<uint64_t>(transfer.transferred,
                                            position + static_cast<uint64_t>(bytesRead)));
  updateFileTransferMessage(transfer, QStringLiteral("sending"),
                            QStringLiteral("sending"));
}

void AppController::onFileRecvChunk(uint32_t friendNumber, uint32_t fileNumber,
                                    uint64_t position, uint8_t const *data,
                                    size_t length) {
  QString const key = fileTransferKey(friendNumber, fileNumber);
  auto transferIt = fileTransfers_.find(key);
  if (transferIt == fileTransfers_.end()) {
    return;
  }
  FileTransfer &transfer = transferIt.value();
  if (length == 0) {
    transfer.transferred = transfer.fileSize;
    updateFileTransferMessage(transfer, QStringLiteral("completed"),
                              QStringLiteral("completed"));
    if (transfer.fileStream) {
      transfer.fileStream->close();
    }
    saveFileRecord(friendNumber, false, transfer.fileName, transfer.fileSize,
                   QStringLiteral("completed"));
    fileTransfers_.remove(key);
    addNotice(QStringLiteral("file"), QStringLiteral("文件接收完成。"));
    return;
  }
  if (!transfer.fileStream || !transfer.fileStream->is_open() || !data) {
    tox_->ControlFileTransfer(friendNumber, fileNumber, TOX_FILE_CONTROL_CANCEL);
    updateFileTransferMessage(transfer, QStringLiteral("failed"),
                              QStringLiteral("failed"));
    saveFileRecord(friendNumber, false, transfer.fileName, transfer.fileSize,
                   QStringLiteral("failed"));
    fileTransfers_.remove(key);
    return;
  }

  transfer.fileStream->seekp(static_cast<std::streamoff>(position));
  transfer.fileStream->write(reinterpret_cast<char const *>(data),
                             static_cast<std::streamsize>(length));
  transfer.fileStream->flush();
  if (!transfer.fileStream->good()) {
    tox_->ControlFileTransfer(friendNumber, fileNumber, TOX_FILE_CONTROL_CANCEL);
    updateFileTransferMessage(transfer, QStringLiteral("failed"),
                              QStringLiteral("failed"));
    saveFileRecord(friendNumber, false, transfer.fileName, transfer.fileSize,
                   QStringLiteral("failed"));
    fileTransfers_.remove(key);
    return;
  }
  transfer.transferred = std::min<uint64_t>(
      transfer.fileSize,
      std::max<uint64_t>(transfer.transferred, position + static_cast<uint64_t>(length)));
  updateFileTransferMessage(transfer, QStringLiteral("receiving"),
                            QStringLiteral("receiving"));
}

void AppController::onIncomingCall(uint32_t friendNumber, bool audioEnabled,
                                   bool videoEnabled) {
  Q_UNUSED(audioEnabled)
  if (!tox_) {
    return;
  }
  if (callPhase_ != CallPhase::Idle) {
    tox_->Hangup(friendNumber);
    appendCallRecord(friendNumber, videoEnabled, QStringLiteral("REJECT_SELF"));
    addNotice(QStringLiteral("call"), QStringLiteral("已有通话，已拒绝新的来电。"),
              QStringLiteral("warning"));
    return;
  }

  currentCallFriend_ = friendNumber;
  currentCallOutgoing_ = false;
  callVideoEnabled_ = videoEnabled;
  localHangupPending_ = false;
  callRecordWritten_ = false;
  callAnsweredAtMs_ = 0;
  callPhase_ = CallPhase::IncomingRinging;
  callTitle_ = friendDisplayName(friendNumber);
  callStatus_ = videoEnabled ? QStringLiteral("视频来电，等待接听...")
                             : QStringLiteral("语音来电，等待接听...");
  emit callStateChanged();
  addNotice(QStringLiteral("call"), QStringLiteral("收到来自 %1 的来电。")
                                    .arg(callTitle_));
  emit callShellRequested();
}

void AppController::onCallState(uint32_t friendNumber,
                                TOXAV_FRIEND_CALL_STATE state) {
  if (friendNumber != currentCallFriend_) {
    return;
  }
  if (state & TOXAV_FRIEND_CALL_STATE_FINISHED) {
    finishCallFromRemote(false);
    return;
  }
  if (state & TOXAV_FRIEND_CALL_STATE_ERROR) {
    finishCallFromRemote(true);
    return;
  }
  if (state & (TOXAV_FRIEND_CALL_STATE_SENDING_A |
               TOXAV_FRIEND_CALL_STATE_SENDING_V |
               TOXAV_FRIEND_CALL_STATE_ACCEPTING_A |
               TOXAV_FRIEND_CALL_STATE_ACCEPTING_V)) {
    markCallActive();
  }
}

void AppController::onAudioFrame(uint32_t friendNumber, int16_t const *pcm,
                                 size_t samples, uint8_t channels,
                                 uint32_t samplingRate) {
  if (friendNumber != currentCallFriend_ || callPhase_ != CallPhase::Active ||
      !audio_ || !pcm) {
    return;
  }
  if (channels != audioFrameParams_.channels ||
      samplingRate != audioFrameParams_.samplingRate) {
    return;
  }
  size_t const totalSamples = samples * static_cast<size_t>(channels);
  audio_->Play(reinterpret_cast<uint8_t const *>(pcm),
               static_cast<qsizetype>(totalSamples * sizeof(int16_t)));
}

void AppController::onVideoFrame(uint32_t friendNumber, uint16_t width,
                                 uint16_t height, uint8_t const *y,
                                 uint8_t const *u, uint8_t const *v) {
  if (friendNumber != currentCallFriend_ || callPhase_ != CallPhase::Active ||
      !callVideoEnabled_ || !video_) {
    return;
  }
  video_->RenderRemoteFrame(width, height, y, u, v);
}

void AppController::beginOutgoingCall(bool videoEnabled) {
  if (callPhase_ != CallPhase::Idle) {
    addNotice(QStringLiteral("call"), QStringLiteral("当前已有通话。"),
              QStringLiteral("warning"));
    return;
  }
  if (selectedKind_ != ConversationKind::Friend || selectedIdentifier_.isEmpty()) {
    addNotice(QStringLiteral("call"), QStringLiteral("请先选择好友。"),
              QStringLiteral("warning"));
    return;
  }
  if (!tox_) {
    addNotice(QStringLiteral("call"), QStringLiteral("Tox 尚未就绪，无法发起通话。"),
              QStringLiteral("warning"));
    return;
  }

  uint32_t const friendNumber = numericIdentifier(selectedIdentifier_);
  if (friendNumber == kInvalidNumber) {
    addNotice(QStringLiteral("call"), QStringLiteral("占位好友不能发起真实通话。"),
              QStringLiteral("warning"));
    return;
  }
  try {
    if (!tox_->FriendExists(friendNumber)) {
      addNotice(QStringLiteral("call"), QStringLiteral("请选择真实好友。"),
                QStringLiteral("warning"));
      return;
    }
  } catch (...) {
    addNotice(QStringLiteral("call"), QStringLiteral("检查好友状态失败。"),
              QStringLiteral("warning"));
    return;
  }

  currentCallFriend_ = friendNumber;
  currentCallOutgoing_ = true;
  callVideoEnabled_ = videoEnabled;
  localHangupPending_ = false;
  callRecordWritten_ = false;
  callAnsweredAtMs_ = 0;
  callPhase_ = CallPhase::OutgoingRinging;
  callTitle_ = friendDisplayName(friendNumber);
  callStatus_ = videoEnabled ? QStringLiteral("等待对方接听视频通话...")
                             : QStringLiteral("等待对方接听语音通话...");

  if (tox_->Call(friendNumber, callOptions(videoEnabled)) != 0) {
    addNotice(QStringLiteral("call"), QStringLiteral("发起通话失败。"),
              QStringLiteral("warning"));
    resetCallState();
    return;
  }

  if (videoEnabled && video_) {
    video_->StartCamera();
  }
  emit callStateChanged();
  addNotice(QStringLiteral("call"), QStringLiteral("已向 %1 发起通话。")
                                  .arg(callTitle_));
  emit callShellRequested();
}

void AppController::configureCallMedia() {
  avConfig_ = AppConfig::LoadAvCallConfig();
  audioFrameParams_.channels = static_cast<uint8_t>(
      std::clamp(avConfig_.audio.channels, 1, 2));
  audioFrameParams_.samplingRate = static_cast<uint32_t>(
      std::clamp(avConfig_.audio.sampleRate, 8000, 48000));
  audioFrameSamplesPerChannel_ = std::max(
      1, static_cast<int>((audioFrameParams_.samplingRate *
                           static_cast<uint32_t>(std::max(1, avConfig_.audio.frameDurationMs))) /
                          1000));
  audioSendBuffer_.resize(static_cast<size_t>(audioFrameSamplesPerChannel_) *
                          audioFrameParams_.channels);
  audioCaptureBuffer_.clear();
  videoSendIntervalMs_ = avConfig_.video.sendFps <= 0
                             ? 0
                             : std::max(1, 1000 / avConfig_.video.sendFps);
  videoSendTimer_.invalidate();

  if (!audio_) {
    audio_ = std::make_unique<AudioManager>(this, audioFrameParams_.samplingRate,
                                            audioFrameParams_.channels);
  }
  if (!video_) {
    video_ = std::make_unique<VideoManager>(this);
  }
  if (video_) {
    video_->SetSendTargetSize(avConfig_.video.targetWidth,
                              avConfig_.video.targetHeight,
                              avConfig_.video.keepAspect);
  }
}

ToxCore::CallOptions AppController::callOptions(bool videoEnabled) const {
  return {.audioEnabled = true,
          .videoEnabled = videoEnabled,
          .audioBitrateKbps = static_cast<uint32_t>(
              std::max(1, avConfig_.audio.bitrateKbps)),
          .videoBitrateKbps = static_cast<uint32_t>(
              std::max(1, avConfig_.video.bitrateKbps))};
}

void AppController::markCallActive() {
  if (callPhase_ == CallPhase::Active) {
    return;
  }
  callPhase_ = CallPhase::Active;
  if (callAnsweredAtMs_ <= 0) {
    callAnsweredAtMs_ = QDateTime::currentMSecsSinceEpoch();
  }
  callStatus_ = callVideoEnabled_ ? QStringLiteral("视频通话中")
                                  : QStringLiteral("通话中");
  if (audio_) {
    audio_->Start();
  }
  if (callVideoEnabled_ && video_) {
    video_->StartCamera();
  }
  emit callStateChanged();
}

void AppController::finishCallFromRemote(bool error) {
  if (currentCallFriend_ == kInvalidNumber) {
    resetCallState();
    return;
  }
  if (localHangupPending_ && callRecordWritten_) {
    resetCallState();
    return;
  }

  QString statusKey;
  if (error) {
    statusKey = QStringLiteral("ERROR");
  } else if (callPhase_ == CallPhase::Active) {
    statusKey = QStringLiteral("HANGUP_REMOTE");
  } else if (callPhase_ == CallPhase::OutgoingRinging) {
    statusKey = QStringLiteral("REJECT_REMOTE");
  } else if (callPhase_ == CallPhase::IncomingRinging) {
    statusKey = QStringLiteral("CANCEL_REMOTE");
  } else {
    statusKey = QStringLiteral("HANGUP_REMOTE");
  }
  appendCallRecord(currentCallFriend_, callVideoEnabled_, statusKey);
  callRecordWritten_ = true;
  addNotice(QStringLiteral("call"), callStatusText(statusKey));
  resetCallState();
}

void AppController::resetCallState() {
  cleanupCallMedia();
  callPhase_ = CallPhase::Idle;
  currentCallFriend_ = kInvalidNumber;
  currentCallOutgoing_ = false;
  callVideoEnabled_ = false;
  localHangupPending_ = false;
  callRecordWritten_ = false;
  callAnsweredAtMs_ = 0;
  callTitle_.clear();
  callStatus_ = QStringLiteral("通话已结束");
  emit callStateChanged();
}

void AppController::cleanupCallMedia() {
  if (audio_) {
    audio_->Stop();
  }
  audioCaptureBuffer_.clear();
  if (video_) {
    video_->StopCamera();
  }
  videoSendTimer_.invalidate();
}

int AppController::currentCallDurationSeconds() const {
  if (callAnsweredAtMs_ <= 0) {
    return 0;
  }
  qint64 const elapsed = QDateTime::currentMSecsSinceEpoch() - callAnsweredAtMs_;
  return static_cast<int>(std::max<qint64>(0, elapsed / 1000));
}

void AppController::sendActiveCallMedia() {
  if (callPhase_ != CallPhase::Active || !tox_ || currentCallFriend_ == kInvalidNumber) {
    videoSendTimer_.invalidate();
    return;
  }

  if (audio_) {
    if (audioSendBuffer_.empty()) {
      audioSendBuffer_.resize(static_cast<size_t>(audioFrameSamplesPerChannel_) *
                              audioFrameParams_.channels);
    }
    size_t const frameBytes = audioSendBuffer_.size() * sizeof(int16_t);
    std::vector<uint8_t> readBuffer(frameBytes);
    qsizetype const bytes = audio_->ReadCapture(
        readBuffer.data(), static_cast<qsizetype>(readBuffer.size()));
    if (bytes > 0) {
      audioCaptureBuffer_.insert(audioCaptureBuffer_.end(), readBuffer.begin(),
                                 readBuffer.begin() + bytes);
    }
    if (frameBytes > 0 && audioCaptureBuffer_.size() > frameBytes * 10) {
      audioCaptureBuffer_.erase(
          audioCaptureBuffer_.begin(),
          audioCaptureBuffer_.begin() +
              static_cast<std::ptrdiff_t>(audioCaptureBuffer_.size() - frameBytes * 10));
    }
    while (frameBytes > 0 && audioCaptureBuffer_.size() >= frameBytes) {
      std::memcpy(audioSendBuffer_.data(), audioCaptureBuffer_.data(), frameBytes);
      tox_->SendAudioFrame(currentCallFriend_, audioSendBuffer_.data(),
                           static_cast<size_t>(audioFrameSamplesPerChannel_),
                           audioFrameParams_);
      audioCaptureBuffer_.erase(audioCaptureBuffer_.begin(),
                                audioCaptureBuffer_.begin() +
                                    static_cast<std::ptrdiff_t>(frameBytes));
    }
  }

  if (!callVideoEnabled_ || !video_) {
    videoSendTimer_.invalidate();
    return;
  }
  if (!videoSendTimer_.isValid()) {
    videoSendTimer_.start();
  }
  bool const shouldSend = videoSendIntervalMs_ <= 0 ||
                          videoSendTimer_.elapsed() >= videoSendIntervalMs_;
  if (!shouldSend) {
    return;
  }
  if (videoSendIntervalMs_ > 0) {
    videoSendTimer_.restart();
  }
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t *y = nullptr;
  uint8_t *u = nullptr;
  uint8_t *v = nullptr;
  if (video_->GetFrame(width, height, y, u, v)) {
    tox_->SendVideoFrame(currentCallFriend_, width, height, y, u, v);
  }
}

void AppController::promptNextIncomingFile() {
  if (pendingIncomingFiles_.isEmpty()) {
    return;
  }
  PendingIncomingFile const &pending = pendingIncomingFiles_.first();
  emit incomingFileSaveRequested(
      friendDisplayName(pending.friendNumber), pending.fileName,
      fileSizeText(pending.fileSize), suggestedReceivedFileUrl(pending.fileName));
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

  QString const publicKey = friendPublicKey(friendNumber);
  if (!publicKey.isEmpty()) {
    storageService_.ensureContact(publicKey);
    QString const remark = storageService_.contactNickname(publicKey).trimmed();
    if (!remark.isEmpty()) {
      return remark;
    }
  }

  try {
    QString const name = fromUtf8(tox_->GetFriendName(friendNumber)).trimmed();
    if (!name.isEmpty()) {
      return name;
    }
  } catch (...) {}
  if (!publicKey.isEmpty()) {
    return QStringLiteral("friend %1").arg(publicKey.left(8));
  }
  return QStringLiteral("friend %1").arg(friendNumber);
}

QString AppController::friendPublicKey(uint32_t friendNumber) const {
  if (friendNumber == kInvalidNumber || !tox_) {
    return {};
  }
  if (friendPublicKeyCache_.contains(friendNumber)) {
    return friendPublicKeyCache_.value(friendNumber);
  }
  try {
    QString const publicKey =
        QString::fromStdString(tox_->GetFriendPublicKeyHex(friendNumber)).toUpper();
    friendPublicKeyCache_[friendNumber] = publicKey;
    return publicKey;
  } catch (...) {
    return {};
  }
}

QString AppController::publicKeyForFriendIdentifier(QString const &identifier) const {
  bool ok = false;
  uint32_t const friendNumber = identifier.toUInt(&ok);
  if (ok) {
    return friendPublicKey(friendNumber);
  }
  ContactItem item;
  if (stubFriends_.contains(identifier)) {
    return stubFriends_.value(identifier).publicKey;
  }
  if (friendModel_.contact(identifier, item)) {
    return item.publicKey;
  }
  return {};
}

void AppController::refreshSelectedFriendTitle() {
  if (!hasSelectedFriend()) {
    return;
  }
  ContactItem item;
  if (!friendModel_.contact(selectedIdentifier_, item) ||
      item.displayName == selectedTitle_) {
    return;
  }
  selectedTitle_ = item.displayName;
  emit selectedConversationChanged();
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
  QString const publicKey = friendPublicKey(friendNumber);
  if (!publicKey.isEmpty()) {
    storageService_.ensureContact(publicKey);
  }
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
