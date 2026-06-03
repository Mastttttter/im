#pragma once
#include <persistence/SqliteStorage.h>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

struct ProfileResult {
  bool success{false};
  bool created{false};
  QString message;
};

inline constexpr char const *kAiLocalPubKey = "AI_LOCAL";

class ProfileService final {
  public:
  QStringList knownAccounts() const;
  bool isKnownAccount(QString const &account) const;
  ProfileResult loginOrRegister(QString const &account, QString const &password,
                                QString const &confirmPassword,
                                bool registerNew);
  ProfileResult changePassword(QString const &account,
                               QString const &oldPassword,
                               QString const &newPassword,
                               QString const &confirmPassword);

  ProfileService() : store_(Persistence::SqliteStorage::GetDb()) {}

  private:
  static bool isAccountNameValid(QString const &account);
  static bool isPasswordValid(QString const &password);
  static QString normalizedAccount(QString const &account);
  ProfileResult InitDb_(QString const &profileName, QString const &password,
                        bool created);
  void rememberAccount(QString const &account) const;
  Persistence::SqliteStorage &store_;
};

class StorageService final {
  public:
  QByteArray loadToxSavedata(QString const &account) const;
  void saveToxSavedata(QString const &account, QByteArray const &savedata);
  QString themePreference() const;
  void saveThemePreference(QString const &theme);

  StorageService() : store_(Persistence::SqliteStorage::GetDb()) {}

  private:
  Persistence::SqliteStorage &store_;
};

class FileTransferService final {
  public:
  QString placeholderSend(QString const &conversationTitle) const;
};

class CallService final {
  public:
  QString startCall(QString const &conversationTitle, bool videoEnabled) const;
  QString hangupCall(QString const &conversationTitle) const;
};

class AiAssistantService final {
  public:
  QString reply(QString const &account, QString const &message) const;
};

class GroupPersistenceService final {
  public:
  void rememberGroup(QString const &identifier, QString const &title);
  void forgetGroup(QString const &identifier);
  QString title(QString const &identifier) const;

  private:
  QHash<QString, QString> groupTitles_;
};
