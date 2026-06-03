#include "SqliteStorage.h"
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Exception.h>
#include <SQLiteCpp/Statement.h>
#include <SQLiteCpp/Transaction.h>
#include <algorithm>
#include <cstring>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QSharedMemory>
#include <QStandardPaths>
#include <QString>
#include <QUuid>
#include <stdexcept>

namespace Persistence {
using namespace Qt::StringLiterals;

namespace {
static std::string ToUtf8String_(QString const &s) {
  QByteArray const b = s.toUtf8();
  return std::string(b.constData(), static_cast<size_t>(b.size()));
}

QString SanitizeProfileName(QString name) {
  name = name.trimmed();
  if (name.isEmpty()) {
    return QStringLiteral("default");
  }
  QString out;
  out.reserve(name.size());
  for (QChar const ch: name) {
    ushort const u = ch.unicode();
    bool const ok = (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') ||
                    (u >= 'A' && u <= 'Z') || (u == '_') || (u == '-');
    out.append(ok ? ch : QChar('_'));
  }
  if (out.isEmpty()) {
    return QStringLiteral("default");
  }
  return out;
}
}  // namespace

SqliteStorage::SqliteStorage() = default;

SqliteStorage::~SqliteStorage() {
  Close_();
}

bool SqliteStorage::IsOpen() const {
  return dbOpenSuccess_;
}

QString SqliteStorage::DbFilePath() const {
  return dbPath_;
}

void SqliteStorage::OpenForProfile(QString const &profileName,
                                   QString const &password) {
  QString const baseDir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (baseDir.isEmpty()) {
    throw std::runtime_error("QStandardPaths::AppDataLocation is empty");
  }
  Close_();

  QString const p = SanitizeProfileName(profileName);
  QString dbPath;
  QString lockPath;
  if (p.compare(QStringLiteral("default"), Qt::CaseInsensitive) == 0) {
    QDir dir(baseDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
      throw std::runtime_error("failed to create AppDataLocation directory");
    }
    dbPath = dir.filePath(QStringLiteral("app.db"));
    lockPath = dir.filePath(QStringLiteral("app.lock"));
  } else {
    QDir base(baseDir);
    if (!base.exists() && !base.mkpath(QStringLiteral("."))) {
      throw std::runtime_error("failed to create AppDataLocation directory");
    }
    QString const profilePath = base.filePath(QStringLiteral("profiles/%1").arg(p));
    QDir profileDir(profilePath);
    if (!profileDir.exists() && !profileDir.mkpath(QStringLiteral("."))) {
      throw std::runtime_error("failed to create profile directory");
    }
    dbPath = profileDir.filePath(QStringLiteral("app.db"));
    lockPath = profileDir.filePath(QStringLiteral("app.lock"));
  }

  QString const sharedMemKey = u"im_lock_%1"_s.arg(p);
  sharedMemory_ = std::make_unique<QSharedMemory>(sharedMemKey);
  if (!sharedMemory_->create(1)) {
    QSharedMemory::SharedMemoryError const error = sharedMemory_->error();
    if (error == QSharedMemory::AlreadyExists && sharedMemory_->attach()) {
      sharedMemory_->detach();
      sharedMemory_.reset();
      throw std::runtime_error(
          u"account %1 has already logged in another window"_s.arg(p)
              .toStdString());
    }
    sharedMemory_.reset();
  }

  lock_ = std::make_unique<QLockFile>(lockPath);
  lock_->setStaleLockTime(1000);
  if (!lock_->tryLock(0)) {
    sharedMemory_.reset();
    lock_.reset();
    throw std::runtime_error(
        u"account %1 is locked by another process"_s.arg(p).toStdString());
  }

  OpenFileUnlocked_(dbPath, password);
}

void SqliteStorage::OpenFile(QString const &filePath, QString const &password) {
  if (filePath.trimmed().isEmpty()) {
    throw std::invalid_argument("database file path is empty");
  }
  Close_();
  OpenFileUnlocked_(filePath, password);
}

void SqliteStorage::OpenFileUnlocked_(QString const &filePath,
                                      QString const &password) {
  QByteArray const pathUtf8 = filePath.toUtf8();
  try {
    db_ = std::make_unique<SQLite::Database>(
        pathUtf8.constData(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
  } catch (SQLite::Exception const &e) {
    Close_();
    throw std::runtime_error(std::string("failed to open database: ") +
                             e.what());
  }
  dbPath_ = QFileInfo(filePath).absoluteFilePath();
  if (!password.isEmpty()) {
    try {
      db_->key(ToUtf8String_(password));
    } catch (SQLite::Exception &e) {
      Close_();
      throw std::runtime_error(
          std::string("failed to set database password: ") + e.what());
    }
  }
  try {
    Exec_("PRAGMA foreign_keys = ON;");
    Exec_("PRAGMA journal_mode = WAL;");
    Exec_("PRAGMA synchronous = NORMAL;");
  } catch (...) {
    Close_();
    throw;
  }
  try {
    InitSchema_();
  } catch (std::exception const &e) {
    Close_();
    throw std::runtime_error(
        std::string("failed to initialize schema (wrong password?):") +
        e.what());
  }
  dbOpenSuccess_ = true;
}

void SqliteStorage::Exec_(char const *sql) {
  if (!db_) {
    throw std::logic_error("database is not open");
  }
  try {
    db_->exec(sql);
  } catch (SQLite::Exception const &e) {
    throw std::runtime_error(std::string("SQL execution failed: ") + e.what() +
                             " (sql=" + sql + ")");
  }
}

void SqliteStorage::Exec_(std::string const &sql) {
  Exec_(sql.c_str());
}

void SqliteStorage::Close_() {
  db_.reset();
  sharedMemory_.reset();
  lock_.reset();
  dbPath_.clear();
  dbOpenSuccess_ = false;
}

void SqliteStorage::InitSchema_() {
  if (!db_) {
    throw std::logic_error("database is not open");
  }
  auto hasColumn = [&](char const *table, char const *column) -> bool {
    std::string const sql = std::string("PRAGMA table_info(") + table + ");";
    SQLite::Statement stmt(*db_, sql);
    while (stmt.executeStep()) {
      char const *name = stmt.getColumn(1).getText();
      if (name && std::strcmp(name, column) == 0) {
        return true;
      }
    }
    return false;
  };
  SQLite::Transaction tx(*db_);
  // meta 表
  Exec_(
      "CREATE TABLE IF NOT EXISTS meta ("
      "  key TEXT PRIMARY KEY,"
      "  value TEXT NOT NULL"
      ");");

  // tox_savedata 表
  Exec_(
      "CREATE TABLE IF NOT EXISTS tox_savedata ("
      "  id INTEGER PRIMARY KEY CHECK (id = 1),"
      "  savedata BLOB NOT NULL,"
      "  updated_at_ms INTEGER NOT NULL"
      ");");

  // contacts 表
  Exec_(
      "CREATE TABLE IF NOT EXISTS contacts ("
      "  friend_pubkey_hex TEXT PRIMARY KEY,"
      "  nickname TEXT,"  // 好友备注名（可选）
      "  created_at_ms INTEGER NOT NULL"
      ");");

  // messages 表
  Exec_(
      "CREATE TABLE IF NOT EXISTS messages ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  friend_pubkey_hex TEXT NOT NULL,"
      "  direction INTEGER NOT NULL,"
      "  tox_message_type INTEGER NOT NULL,"
      "  body TEXT NOT NULL,"
      "  created_at_ms INTEGER NOT NULL,"
      "  FOREIGN KEY (friend_pubkey_hex) REFERENCES "
      "contacts(friend_pubkey_hex) ON DELETE CASCADE"
      ");");

  // 索引
  Exec_(
      "CREATE INDEX IF NOT EXISTS idx_messages_friend ON "
      "messages(friend_pubkey_hex, created_at_ms);");

  // groups 表（旧/备用群聊信息表；对外 API 仍暴露
  // EnsureGroup/UpdateGroupTitle）
  Exec_(
      "CREATE TABLE IF NOT EXISTS groups ("
      "  group_identifier TEXT PRIMARY KEY,"
      "  title TEXT,"
      "  created_at_ms INTEGER NOT NULL"
      ");");

  // Conference 基本信息表（新版 API，使用持久化 ID）
  Exec_(
      "CREATE TABLE IF NOT EXISTS conferences ("
      "  conference_id_hex TEXT PRIMARY KEY,"  // Conference 唯一 ID（32
                                               // 字节，hex 编码）
      "  title TEXT,"
      "  created_at_ms INTEGER NOT NULL"
      ");");

  // Conference 消息表
  Exec_(
      "CREATE TABLE IF NOT EXISTS conference_messages ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  conference_id_hex TEXT NOT NULL,"
      "  peer_pubkey_hex TEXT NOT NULL,"
      "  peer_name TEXT,"
      "  direction INTEGER NOT NULL,"  // 0=incoming, 1=outgoing
      "  body TEXT NOT NULL,"
      "  created_at_ms INTEGER NOT NULL,"
      "  FOREIGN KEY (conference_id_hex) REFERENCES "
      "conferences(conference_id_hex) ON DELETE CASCADE"
      ");");

  // Conference 消息索引
  Exec_(
      "CREATE INDEX IF NOT EXISTS idx_conference_messages ON "
      "conference_messages(conference_id_hex, created_at_ms);");

  tx.commit();
}

void SqliteStorage::ChangePassword(QString const &oldPassword,
                                   QString const &newPassword) {
  if (!db_) {
    throw std::logic_error("database is not open");
  }
  if (dbPath_.isEmpty()) {
    throw std::logic_error("database path is empty");
  }
  if (oldPassword.isEmpty()) {
    throw std::invalid_argument("old password cannot be empty");
  }
  if (newPassword.isEmpty()) {
    throw std::invalid_argument("new password cannot be empty");
  }
  try {
    QByteArray const pathUtf8 = dbPath_.toUtf8();
    SQLite::Database verifier(pathUtf8.constData(), SQLite::OPEN_READWRITE);
    verifier.key(ToUtf8String_(oldPassword));
    verifier.exec("SELECT name FROM sqlite_master LIMIT 1;");
    db_->rekey(ToUtf8String_(newPassword));
  } catch (SQLite::Exception const &e) {
    throw std::runtime_error(std::string("failed to change password: ") +
                             e.what());
  }
}

std::optional<QString> SqliteStorage::GetMetaValue(QString const &key) const {
  if (!db_) {
    throw std::logic_error("database is not open");
  }
  SQLite::Statement stmt(*db_, "SELECT value FROM meta WHERE key = ?;");
  stmt.bind(1, ToUtf8String_(key));
  if (!stmt.executeStep()) {
    return std::nullopt;
  }
  return QString::fromUtf8(stmt.getColumn(0).getText());
}

// 从数据库加载 Tox savedata（如果不存在则返回 nullopt）
std::optional<QByteArray> SqliteStorage::LoadToxSavedata() const {
  if (!db_) {
    throw std::logic_error("database is not open");
  }

  SQLite::Statement stmt(*db_,
                         "SELECT savedata FROM tox_savedata WHERE id = 1;");
  if (!stmt.executeStep()) {
    return std::nullopt;
  }

  auto const col = stmt.getColumn(0);
  if (col.isNull() || col.getBytes() <= 0) {
    return std::nullopt;
  }
  return QByteArray(static_cast<char const *>(col.getBlob()), col.getBytes());
}

// 保存 Tox savedata 到数据库（插入或替换）
void SqliteStorage::SaveToxSavedata(QByteArray const &blob,
                                    qint64 updatedAtMs) {
  if (!db_) {
    throw std::logic_error("database is not open");
  }

  SQLite::Statement stmt(*db_,
                         "INSERT OR REPLACE INTO tox_savedata (id, savedata, "
                         "updated_at_ms) VALUES (1, ?, ?);");
  stmt.bind(1, blob.constData(), blob.size());
  stmt.bind(2, static_cast<int64_t>(updatedAtMs));
  stmt.exec();
}

// ==================== Contacts 操作 ====================

// 确保好友存在于 contacts 表（如果已存在则忽略）
void SqliteStorage::EnsureContact(QString const &friendPubKeyHex,
                                  qint64 createdAtMs) {
  if (!db_) {
    throw std::logic_error("database is not open");
  }

  SQLite::Statement stmt(*db_,
                         "INSERT OR IGNORE INTO contacts (friend_pubkey_hex, "
                         "created_at_ms) VALUES (?, ?);");
  stmt.bind(1, ToUtf8String_(friendPubKeyHex));
  stmt.bind(2, static_cast<int64_t>(createdAtMs));
  stmt.exec();
}

// 设置好友备注名（为空则清除备注，设置为 NULL）
void SqliteStorage::SetContactNickname(QString const &friendPubKeyHex,
                                       QString const &nickname) {
  if (!db_) {
    throw std::logic_error("database is not open");
  }

  SQLite::Statement stmt(
      *db_, "UPDATE contacts SET nickname = ? WHERE friend_pubkey_hex = ?;");
  if (nickname.isEmpty()) {
    // 清除备注：设置为 NULL
    stmt.bind(1);
  } else {
    stmt.bind(1, ToUtf8String_(nickname));
  }
  stmt.bind(2, ToUtf8String_(friendPubKeyHex));
  stmt.exec();
}

// 获取好友备注名（如果没有设置则返回空字符串）
QString SqliteStorage::GetContactNickname(
    QString const &friendPubKeyHex) const {
  if (!db_) {
    throw std::logic_error("database is not open");
  }

  SQLite::Statement stmt(
      *db_, "SELECT nickname FROM contacts WHERE friend_pubkey_hex = ?;");
  stmt.bind(1, ToUtf8String_(friendPubKeyHex));
  if (!stmt.executeStep()) {
    return QString();
  }
  auto const col = stmt.getColumn(0);
  if (col.isNull()) {
    return QString();
  }
  return QString::fromUtf8(col.getText());
}

// ==================== Messages 操作 ====================

// 插入一条消息记录到数据库
void SqliteStorage::InsertMessage(QString const &friendPubKeyHex, int direction,
                                  int toxMessageType, QString const &body,
                                  qint64 createdAtMs) {
  if (!db_) {
    throw std::logic_error("database is not open");
  }

  SQLite::Statement stmt(*db_,
                         "INSERT INTO messages (friend_pubkey_hex, direction, "
                         "tox_message_type, body, created_at_ms) "
                         "VALUES (?, ?, ?, ?, ?);");
  stmt.bind(1, ToUtf8String_(friendPubKeyHex));
  stmt.bind(2, direction);
  stmt.bind(3, toxMessageType);
  stmt.bind(4, ToUtf8String_(body));
  stmt.bind(5, static_cast<int64_t>(createdAtMs));
  stmt.exec();
}

// 加载某个好友的最近 N 条消息（按时间升序返回）
QList<SqliteStorage::MessageRow> SqliteStorage::LoadRecentMessages(
    QString const &friendPubKeyHex, int limit) const {
  if (!db_) {
    throw std::logic_error("database is not open");
  }

  // 查询最近 N 条消息（按时间升序）
  SQLite::Statement stmt(*db_,
                         "SELECT id, friend_pubkey_hex, direction, "
                         "tox_message_type, body, created_at_ms "
                         "FROM messages "
                         "WHERE friend_pubkey_hex = ? "
                         "ORDER BY created_at_ms DESC "
                         "LIMIT ?;");

  stmt.bind(1, ToUtf8String_(friendPubKeyHex));
  stmt.bind(2, limit);

  QList<MessageRow> rows;
  while (stmt.executeStep()) {
    MessageRow row;
    row.id = stmt.getColumn(0).getInt64();
    row.friendPubKeyHex = QString::fromUtf8(stmt.getColumn(1).getText());
    row.direction = stmt.getColumn(2).getInt();
    row.toxMessageType = stmt.getColumn(3).getInt();
    row.body = QString::fromUtf8(stmt.getColumn(4).getText());
    row.createdAtMs = stmt.getColumn(5).getInt64();

    rows.append(row);
  }

  // 反转顺序（查询是 DESC，但返回要 ASC）
  std::reverse(rows.begin(), rows.end());

  return rows;
}

// 清理重复的消息记录：保留每组重复记录中 ID 最小的那条，删除其他重复记录
int SqliteStorage::CleanupDuplicateMessages() {
  if (!db_) {
    throw std::logic_error("database is not open");
  }

  // 删除重复记录，保留每组重复中 id 最小的那条
  // 重复的定义：friend_pub_key_hex, direction, tox_message_type, body,
  // created_at_ms 都相同
  char const *sql =
      "DELETE FROM messages "
      "WHERE id NOT IN ( "
      "  SELECT MIN(id) "
      "  FROM messages "
      "  GROUP BY friend_pubkey_hex, direction, tox_message_type, body, "
      "created_at_ms "
      ");";

  Exec_(sql);

  // 返回删除的行数
  return db_->getChanges();
}

// ==================== 群聊操作实现 ====================

// 确保群聊存在于 groups 表（如果已存在则忽略）
void SqliteStorage::EnsureGroup(QString const &groupIdentifier,
                                QString const &title, qint64 createdAtMs) {
  if (!db_) {
    throw std::logic_error("database is not open");
  }

  SQLite::Statement stmt(*db_,
                         "INSERT OR IGNORE INTO groups (group_identifier, "
                         "title, created_at_ms) VALUES (?, ?, ?);");
  stmt.bind(1, ToUtf8String_(groupIdentifier));
  stmt.bind(2, ToUtf8String_(title));
  stmt.bind(3, static_cast<int64_t>(createdAtMs));
  stmt.exec();
}

// 更新群聊标题
void SqliteStorage::UpdateGroupTitle(QString const &groupIdentifier,
                                     QString const &title) {
  if (!db_) {
    throw std::logic_error("database is not open");
  }

  SQLite::Statement stmt(
      *db_, "UPDATE groups SET title = ? WHERE group_identifier = ?;");
  stmt.bind(1, ToUtf8String_(title));
  stmt.bind(2, ToUtf8String_(groupIdentifier));
  stmt.exec();
}

// 插入一条群聊消息记录到数据库
void SqliteStorage::InsertConferenceMessage(QString const &conferenceIdHex,
                                            QString const &peerPubKeyHex,
                                            QString const &peerName,
                                            int direction, QString const &body,
                                            qint64 createdAtMs) {
  if (!db_) {
    throw std::logic_error("database is not open");
  }

  SQLite::Statement stmt(
      *db_,
      "INSERT INTO conference_messages (conference_id_hex, peer_pubkey_hex, "
      "peer_name, direction, body, created_at_ms) "
      "VALUES (?, ?, ?, ?, ?, ?);");
  stmt.bind(1, ToUtf8String_(conferenceIdHex));
  stmt.bind(2, ToUtf8String_(peerPubKeyHex));
  stmt.bind(3, ToUtf8String_(peerName));
  stmt.bind(4, direction);
  stmt.bind(5, ToUtf8String_(body));
  stmt.bind(6, static_cast<int64_t>(createdAtMs));
  stmt.exec();
}

// 加载某个群聊的最近 N 条消息（按时间升序返回）
QList<SqliteStorage::ConferenceMessageRow>
SqliteStorage::LoadRecentConferenceMessages(QString const &conferenceIdHex,
                                            int limit) const {
  if (!db_) {
    throw std::logic_error("database is not open");
  }

  QList<ConferenceMessageRow> result;

  SQLite::Statement stmt(*db_,
                         "SELECT id, conference_id_hex, peer_pubkey_hex, "
                         "peer_name, direction, body, created_at_ms "
                         "FROM conference_messages "
                         "WHERE conference_id_hex = ? "
                         "ORDER BY created_at_ms DESC "
                         "LIMIT ?;");
  stmt.bind(1, ToUtf8String_(conferenceIdHex));
  stmt.bind(2, limit);

  while (stmt.executeStep()) {
    ConferenceMessageRow row;
    row.id = stmt.getColumn(0).getInt64();
    row.conferenceIdHex = QString::fromUtf8(stmt.getColumn(1).getText());
    row.peerPubKeyHex = QString::fromUtf8(stmt.getColumn(2).getText());
    row.peerName = QString::fromUtf8(stmt.getColumn(3).getText());
    row.direction = stmt.getColumn(4).getInt();
    row.body = QString::fromUtf8(stmt.getColumn(5).getText());
    row.createdAtMs = stmt.getColumn(6).getInt64();
    result.append(row);
  }

  // 反转顺序，使其按时间升序排列
  std::reverse(result.begin(), result.end());
  return result;
}

// 插入或更新群聊信息（存在则更新标题，不存在则插入）
void SqliteStorage::InsertOrUpdateConference(QString const &conferenceIdHex,
                                             QString const &title,
                                             qint64 createdAtMs) {
  if (!db_) {
    throw std::logic_error("database is not open");
  }

  SQLite::Statement stmt(
      *db_,
      "INSERT INTO conferences (conference_id_hex, title, created_at_ms) "
      "VALUES (?, ?, ?) "
      "ON CONFLICT(conference_id_hex) DO UPDATE SET "
      "  title = excluded.title;");
  stmt.bind(1, ToUtf8String_(conferenceIdHex));
  stmt.bind(2, ToUtf8String_(title));
  stmt.bind(3, static_cast<int64_t>(createdAtMs));
  stmt.exec();
}

// 加载所有群聊信息（按创建时间倒序排列）
QList<SqliteStorage::ConferenceRow> SqliteStorage::LoadAllConferences() const {
  if (!db_) {
    throw std::logic_error("database is not open");
  }

  QList<ConferenceRow> result;

  SQLite::Statement stmt(*db_,
                         "SELECT conference_id_hex, title, created_at_ms "
                         "FROM conferences "
                         "ORDER BY created_at_ms DESC;");

  while (stmt.executeStep()) {
    ConferenceRow row;
    row.conferenceIdHex = QString::fromUtf8(stmt.getColumn(0).getText());
    row.title = QString::fromUtf8(stmt.getColumn(1).getText());
    row.createdAtMs = stmt.getColumn(2).getInt64();
    result.append(row);
  }

  return result;
}

// 删除群聊及其所有消息（级联删除）
void SqliteStorage::DeleteConference(QString const &conferenceIdHex) {
  if (!db_) {
    throw std::logic_error("database is not open");
  }

  SQLite::Statement stmt(
      *db_, "DELETE FROM conferences WHERE conference_id_hex = ?;");
  stmt.bind(1, ToUtf8String_(conferenceIdHex));
  stmt.exec();
}

}  // namespace Persistence
