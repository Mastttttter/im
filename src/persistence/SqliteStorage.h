#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>
#include <memory>
#include <optional>

namespace SQLite {
class Database;
}
class QLockFile;
class QSharedMemory;

namespace Persistence {
class SqliteStorage {
  public:
  struct MessageRow {
    int64_t id{0};
    QString friendPubKeyHex;
    int direction{0};
    int toxMessageType{0};
    QString body;
    int64_t createdAtMs{0};
  };

  struct ConferenceRow {
    QString conferenceIdHex;
    QString title;
    int64_t createdAtMs{0};
  };

  struct ConferenceMessageRow {
    int64_t id{0};
    QString conferenceIdHex;
    QString peerPubKeyHex;
    QString peerName;
    int direction{0};
    QString body;
    int64_t createdAtMs{0};
  };

  SqliteStorage();
  ~SqliteStorage();

  bool IsOpen() const;
  QString DbFilePath() const;
  void OpenForProfile(QString const &profileName, QString const &password);
  void OpenFile(QString const &filePath, QString const &password);
  void OpenFileUnlocked_(QString const &filePath, QString const &password);
  void ChangePassword(QString const &oldPassword, QString const &newPassword);
  std::optional<QString> GetMetaValue(QString const &key) const;
  void SetMetaValue(QString const &key, QString const &value);
  std::optional<QByteArray> LoadToxSavedata() const;
  void SaveToxSavedata(QByteArray const &blob, qint64 updatedAtMs);

  // ==================== Contacts 操作 ====================

  /**
   * @brief 确保某个好友存在于 contacts 表
   *
   * @param friendPubKeyHex 好友公钥（hex 字符串）
   * @param createdAtMs 创建时间戳（毫秒）
   */
  void EnsureContact(QString const &friendPubKeyHex, qint64 createdAtMs);

  /**
   * @brief 设置好友备注名
   *
   * @param friendPubKeyHex 好友公钥（hex 字符串）
   * @param nickname 备注名（为空则清除备注）
   */
  void SetContactNickname(QString const &friendPubKeyHex,
                          QString const &nickname);

  /**
   * @brief 获取好友备注名
   *
   * @param friendPubKeyHex 好友公钥（hex 字符串）
   * @return 备注名，如果没有设置则返回空字符串
   */
  QString GetContactNickname(QString const &friendPubKeyHex) const;

  // ==================== Messages 操作 ====================

  /**
   * @brief 插入一条消息记录
   *
   * @param friendPubKeyHex 好友公钥（hex 字符串）
   * @param direction 0=incoming, 1=outgoing
   * @param toxMessageType TOX_MESSAGE_TYPE as int
   * @param body 消息内容
   * @param createdAtMs 创建时间戳（毫秒）
   */
  void InsertMessage(QString const &friendPubKeyHex, int direction,
                     int toxMessageType, QString const &body,
                     qint64 createdAtMs);

  /**
   * @brief 查询某个好友的最近 N 条消息（按时间升序）
   *
   * @param friendPubKeyHex 好友公钥（hex 字符串）
   * @param limit 最多返回多少条
   * @return 消息列表（按时间升序）
   */
  QList<MessageRow> LoadRecentMessages(QString const &friendPubKeyHex,
                                       int limit) const;

  /**
   * @brief 清理重复的消息记录（保留每组重复记录中 ID 最小的那条）
   *
   * 根据 friend_pub_key_hex, direction, tox_message_type, body, created_at_ms
   * 判断是否重复
   * @return 删除的记录数量
   */
  int CleanupDuplicateMessages();

  // ==================== 群聊操作 ====================

  /**
   * @brief 确保某个群聊存在于 groups 表
   *
   * @param groupIdentifier 群聊唯一标识
   * @param title 群聊标题
   * @param createdAtMs 创建时间戳（毫秒）
   */
  void EnsureGroup(QString const &groupIdentifier, QString const &title,
                   qint64 createdAtMs);

  /**
   * @brief 更新群聊标题
   *
   * @param groupIdentifier 群聊唯一标识
   * @param title 新标题
   */
  void UpdateGroupTitle(QString const &groupIdentifier, QString const &title);

  /**
   * @brief 加载所有 conference
   *
   * @return Conference 列表（按创建时间排序）
   */
  QList<ConferenceRow> LoadAllConferences() const;

  /**
   * @brief 插入一条 conference 消息记录
   *
   * @param conferenceIdHex Conference ID（hex 字符串）
   * @param peerPubKeyHex 发送者公钥（hex 字符串）
   * @param peerName 发送者昵称（消息发送时的快照）
   * @param direction 0=incoming, 1=outgoing
   * @param body 消息内容
   * @param createdAtMs 创建时间戳（毫秒）
   */
  void InsertConferenceMessage(QString const &conferenceIdHex,
                               QString const &peerPubKeyHex,
                               QString const &peerName, int direction,
                               QString const &body, qint64 createdAtMs);

  /**
   * @brief 查询某个 conference 的最近 N 条消息（按时间升序）
   *
   * @param conferenceIdHex Conference ID（hex 字符串）
   * @param limit 最多返回多少条
   * @return 消息列表（按时间升序）
   */
  QList<ConferenceMessageRow> LoadRecentConferenceMessages(
      QString const &conferenceIdHex, int limit) const;

  /**
   * @brief 保存 conference 信息
   *
   * @param conferenceIdHex Conference 唯一 ID（hex 字符串）
   * @param title Conference 标题
   * @param createdAtMs 创建时间戳
   */
  void InsertOrUpdateConference(QString const &conferenceIdHex,
                                QString const &title, qint64 createdAtMs);

  /**
   * @brief 删除 conference
   *
   * @param conferenceIdHex Conference ID（hex 字符串）
   */
  void DeleteConference(QString const &conferenceIdHex);

  static SqliteStorage &GetDb() {
    static SqliteStorage obj;
    return obj;
  }

  private:
  bool dbOpenSuccess_ = false;
  void Close_();
  void Exec_(char const *sql);
  void Exec_(std::string const &sql);
  void InitSchema_();
  std::unique_ptr<SQLite::Database> db_;

  std::unique_ptr<QLockFile> lock_;
  std::unique_ptr<QSharedMemory> sharedMemory_;

  QString dbPath_;
};
}  // namespace Persistence
