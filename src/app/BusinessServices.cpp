#include "app/BusinessServices.h"
#include <algorithm>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include "core/AppLog.h"

using namespace Qt::StringLiterals;

namespace {

// 获取应用数据目录路径（AppDataLocation）
QString AppDataDirOrEmpty() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

// 获取存储最近登录账号的文件路径
QString LastAccountFilePath() {
  QString const baseDir = AppDataDirOrEmpty();
  if (baseDir.isEmpty()) {
    return QString();
  }
  return QDir(baseDir).filePath(QStringLiteral(".last_account"));
}

// 读取最近登录的账号
QString ReadLastAccount() {
  QString const filePath = LastAccountFilePath();
  if (filePath.isEmpty()) {
    return QString();
  }

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QString();
  }

  QString account = QString::fromUtf8(file.readAll()).trimmed();
  file.close();
  return account;
}

// 保存最近登录的账号
void SaveLastAccount(QString const &account) {
  QString const filePath = LastAccountFilePath();
  if (filePath.isEmpty()) {
    return;
  }

  // 确保目录存在
  QDir().mkpath(QFileInfo(filePath).absolutePath());

  QFile file(filePath);
  if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
    file.write(account.toUtf8());
    file.close();
  }
}

QString LoginErrorMessage(std::exception const &e) {
  QString const detail = QString::fromUtf8(e.what());
  if (detail.contains(QStringLiteral("not a database"), Qt::CaseInsensitive) ||
      detail.contains(QStringLiteral("wrong password"), Qt::CaseInsensitive) ||
      detail.contains(QStringLiteral("file is encrypted"), Qt::CaseInsensitive)) {
    return QStringLiteral("密码错误或数据库无法打开。");
  }
  return detail;
}

QStringList normalizedKnownAccounts() {
  auto baseDir = AppDataDirOrEmpty();
  QStringList out;
  if (baseDir.isEmpty()) {
    return out;
  }
  {
    QString const defaultDb = QDir(baseDir).filePath(QStringLiteral("app.db"));
    if (QFileInfo::exists(defaultDb)) {
      out << QStringLiteral("default");
    }
  }
  {
    QDir profilesDir(QDir(baseDir).filePath(QStringLiteral("profiles")));
    if (profilesDir.exists()) {
      QStringList const subDirs =
          profilesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
      for (QString const &name: subDirs) {
        QString const dbPath =
            profilesDir.filePath(QStringLiteral("%1/app.db").arg(name));
        if (QFileInfo::exists(dbPath)) {
          out << name;
        }
      }
    }
  }
  QString const lastAccount = ReadLastAccount();
  std::ranges::sort(out, [&lastAccount](QString const &a, QString const &b) {
    if (!lastAccount.isEmpty()) {
      if (a.compare(lastAccount, Qt::CaseInsensitive) == 0) {
        return true;
      }
      if (b.compare(lastAccount, Qt::CaseInsensitive) == 0) {
        return false;
      }
    }
    return a.compare(b, Qt::CaseInsensitive) < 0;
  });
  return out;
}
}  // namespace

QStringList ProfileService::knownAccounts() const {
  return normalizedKnownAccounts();
}

bool ProfileService::isKnownAccount(QString const &account) const {
  return knownAccounts().contains(normalizedAccount(account),
                                  Qt::CaseInsensitive);
}

ProfileResult ProfileService::loginOrRegister(QString const &account,
                                              QString const &password,
                                              QString const &confirmPassword,
                                              bool registerNew) {
  QString const name = normalizedAccount(account);
  if (name.isEmpty()) {
    return {false, false, QStringLiteral("请输入账号名称。")};
  }
  if (name.size() < 5) {
    return {false, false, QStringLiteral("账号长度不得少于5个字符。")};
  }
  if (name.size() > 10) {
    return {false, false, QStringLiteral("账号长度不得超过10个字符。")};
  }
  if (!isAccountNameValid(name)) {
    return {false, false, QStringLiteral("账号仅允许英文、数字和下划线。")};
  }
  if (!isPasswordValid(password)) {
    return {false, false,
            QStringLiteral("密码仅支持英文和数字，且不得少于8个字符。")};
  }

  bool const known = isKnownAccount(name);
  bool const createAccount = registerNew || !known;
  if (createAccount && password != confirmPassword) {
    return {false, false, QStringLiteral("两次输入的密码不一致。")};
  }

  ProfileResult result = InitDb_(name, password, createAccount);
  if (!result.success) {
    return result;
  }
  rememberAccount(name);
  return result;
}

ProfileResult ProfileService::changePassword(QString const &account,
                                             QString const &oldPassword,
                                             QString const &newPassword,
                                             QString const &confirmPassword) {
  QString const name = normalizedAccount(account);
  if (name.isEmpty()) {
    return {false, false, QStringLiteral("请输入账号名称。")};
  }
  if (!isKnownAccount(name)) {
    return {false, false, QStringLiteral("账号不存在，无法更换密码。")};
  }
  if (oldPassword.isEmpty()) {
    return {false, false, QStringLiteral("请输入当前密码。")};
  }
  if (!isPasswordValid(newPassword)) {
    return {false, false,
            QStringLiteral("新密码仅支持英文和数字，且不得少于8个字符。")};
  }
  if (oldPassword == newPassword) {
    return {false, false, QStringLiteral("新密码不能与当前密码相同。")};
  }
  if (newPassword != confirmPassword) {
    return {false, false, QStringLiteral("两次输入的新密码不一致。")};
  }

  try {
    if (store_.IsOpen()) {
      store_.ChangePassword(oldPassword, newPassword);
    } else {
      Persistence::SqliteStorage store;
      store.OpenForProfile(name, oldPassword);
      store.ChangePassword(oldPassword, newPassword);
    }
    rememberAccount(name);
    return {true, false, QStringLiteral("密码已更换，请使用新密码登录。")};
  } catch (std::exception const &e) {
    return {false, false,
            QStringLiteral("密码更换失败：%1").arg(LoginErrorMessage(e))};
  }
}

bool ProfileService::isAccountNameValid(QString const &account) {
  static QRegularExpression const pattern(QStringLiteral("^[0-9A-Za-z_]+$"));
  return pattern.match(account).hasMatch();
}

bool ProfileService::isPasswordValid(QString const &password) {
  static QRegularExpression const pattern(QStringLiteral("^[0-9A-Za-z]{8,}$"));
  return pattern.match(password).hasMatch();
}

QString ProfileService::normalizedAccount(QString const &account) {
  return account.trimmed();
}

ProfileResult ProfileService::InitDb_(QString const &profileName,
                                      QString const &password,
                                      bool created) {
  try {
    store_.OpenForProfile(profileName, password);
    AppLog::LogHub::Instance().AppendInfo(
        QStringLiteral("db"),
        QStringLiteral("opened encrypted sqlite store (account=%1) DB=%2")
            .arg(profileName, store_.DbFilePath()));
    try {
      int const deletedCount = store_.CleanupDuplicateMessages();
      if (deletedCount > 0) {
        AppLog::LogHub::Instance().AppendInfo(
            QStringLiteral("db"),
            QStringLiteral("cleaned up %1 duplicate message(s)")
                .arg(deletedCount));
      }
    } catch (std::exception const &e) {
      AppLog::LogHub::Instance().AppendWarn(
          QStringLiteral("exception"),
          QStringLiteral("cleanup duplicates failed: %1")
              .arg(QString::fromUtf8(e.what())));
    }
    store_.EnsureContact(QString::fromLatin1(kAiLocalPubKey),
                         QDateTime::currentMSecsSinceEpoch());
    return {true, created,
            created ? QStringLiteral("账号已注册并打开本地加密数据库。")
                    : QStringLiteral("账号已登录并打开本地加密数据库。")};
  } catch (std::exception const &e) {
    AppLog::LogHub::Instance().AppendError(
        u"exception"_s,
        u"open sqlite store failed: %1"_s.arg(QString::fromUtf8(e.what())));
    return {false, created,
            QStringLiteral("登录失败：%1").arg(LoginErrorMessage(e))};
  }
}

void ProfileService::rememberAccount(QString const &account) const {
  SaveLastAccount(account);
  QSettings settings;
  settings.setValue(QStringLiteral("profiles/lastAccount"), account);
}

QByteArray StorageService::loadToxSavedata(QString const &account) const {
  Q_UNUSED(account)
  if (!store_.IsOpen()) {
    return {};
  }
  try {
    std::optional<QByteArray> savedata = store_.LoadToxSavedata();
    return savedata.value_or(QByteArray{});
  } catch (...) {
    return {};
  }
}

void StorageService::saveToxSavedata(QString const &account,
                                     QByteArray const &savedata) {
  Q_UNUSED(account)
  if (!store_.IsOpen() || savedata.isEmpty()) {
    return;
  }
  store_.SaveToxSavedata(savedata, QDateTime::currentMSecsSinceEpoch());
}

void StorageService::ensureContact(QString const &publicKey) const {
  QString const key = publicKey.trimmed();
  if (!store_.IsOpen() || key.isEmpty()) {
    return;
  }
  try {
    store_.EnsureContact(key, QDateTime::currentMSecsSinceEpoch());
  } catch (...) {}
}

QString StorageService::contactNickname(QString const &publicKey) const {
  QString const key = publicKey.trimmed();
  if (!store_.IsOpen() || key.isEmpty()) {
    return {};
  }
  try {
    return store_.GetContactNickname(key);
  } catch (...) {
    return {};
  }
}

void StorageService::setContactNickname(QString const &publicKey,
                                        QString const &nickname) const {
  QString const key = publicKey.trimmed();
  if (!store_.IsOpen() || key.isEmpty()) {
    return;
  }
  try {
    store_.EnsureContact(key, QDateTime::currentMSecsSinceEpoch());
    store_.SetContactNickname(key, nickname.trimmed());
  } catch (...) {}
}

void StorageService::saveFriendMessage(QString const &friendPublicKey,
                                       int direction, int toxMessageType,
                                       QString const &body,
                                       qint64 createdAtMs) const {
  QString const key = friendPublicKey.trimmed();
  if (!store_.IsOpen() || key.isEmpty() || body.isEmpty()) {
    return;
  }
  try {
    store_.EnsureContact(key, createdAtMs);
    store_.InsertMessage(key, direction, toxMessageType, body, createdAtMs);
  } catch (...) {}
}

QList<Persistence::SqliteStorage::MessageRow>
StorageService::loadRecentFriendMessages(QString const &friendPublicKey,
                                         int limit) const {
  QString const key = friendPublicKey.trimmed();
  if (!store_.IsOpen() || key.isEmpty()) {
    return {};
  }
  try {
    return store_.LoadRecentMessages(key, limit);
  } catch (...) {
    return {};
  }
}

QString StorageService::metaValue(QString const &key,
                                  QString const &fallback) const {
  QString const normalizedKey = key.trimmed();
  if (!store_.IsOpen() || normalizedKey.isEmpty()) {
    return fallback;
  }
  try {
    auto const value = store_.GetMetaValue(normalizedKey);
    return value.value_or(fallback);
  } catch (...) {
    return fallback;
  }
}

void StorageService::setMetaValue(QString const &key,
                                  QString const &value) const {
  QString const normalizedKey = key.trimmed();
  if (!store_.IsOpen() || normalizedKey.isEmpty()) {
    return;
  }
  try {
    store_.SetMetaValue(normalizedKey, value);
  } catch (...) {}
}

void StorageService::saveAiMessage(bool outgoing, QString const &body,
                                   qint64 createdAtMs) const {
  if (!store_.IsOpen() || body.isEmpty()) {
    return;
  }
  try {
    QString const aiKey = QString::fromLatin1(kAiLocalPubKey);
    store_.EnsureContact(aiKey, createdAtMs);
    store_.InsertMessage(aiKey, outgoing ? 1 : 0, 0, body, createdAtMs);
  } catch (...) {}
}

QList<Persistence::SqliteStorage::MessageRow>
StorageService::loadRecentAiMessages(int limit) const {
  if (!store_.IsOpen()) {
    return {};
  }
  try {
    return store_.LoadRecentMessages(QString::fromLatin1(kAiLocalPubKey), limit);
  } catch (...) {
    return {};
  }
}

void StorageService::clearAiMessages() const {
  if (!store_.IsOpen()) {
    return;
  }
  try {
    store_.DeleteMessagesForContact(QString::fromLatin1(kAiLocalPubKey));
    store_.EnsureContact(QString::fromLatin1(kAiLocalPubKey),
                         QDateTime::currentMSecsSinceEpoch());
  } catch (...) {}
}

QString StorageService::themePreference() const {
  QSettings settings;
  return settings.value(QStringLiteral("ui/theme"), QStringLiteral("dark"))
      .toString();
}

void StorageService::saveThemePreference(QString const &theme) {
  QSettings settings;
  settings.setValue(QStringLiteral("ui/theme"), theme);
}

QString FileTransferService::placeholderSend(
    QString const &conversationTitle) const {
  return QStringLiteral("文件发送界面已就绪，%1 的真实传输稍后接入。")
      .arg(conversationTitle);
}

QString CallService::startCall(QString const &conversationTitle,
                               bool videoEnabled) const {
  return videoEnabled ? QStringLiteral("正在打开与 %1 的视频通话窗口。")
                            .arg(conversationTitle)
                      : QStringLiteral("正在打开与 %1 的音频通话窗口。")
                            .arg(conversationTitle);
}

QString CallService::hangupCall(QString const &conversationTitle) const {
  return QStringLiteral("已结束与 %1 的通话占位流程。").arg(conversationTitle);
}

void GroupPersistenceService::rememberGroup(QString const &identifier,
                                            QString const &title) {
  groupTitles_.insert(identifier, title);
}

void GroupPersistenceService::forgetGroup(QString const &identifier) {
  groupTitles_.remove(identifier);
}

QString GroupPersistenceService::title(QString const &identifier) const {
  return groupTitles_.value(identifier);
}
