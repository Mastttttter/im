#pragma once

#include "app/BusinessServices.h"
#include "app/ChatMessageModel.h"
#include "app/ContactListModel.h"
#include "app/NoticeModel.h"
#include "core/ToxCoreWrapper.h"

#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <cstdint>
#include <memory>

class AppController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loggedInChanged)
  Q_PROPERTY(QString accountName READ accountName NOTIFY accountNameChanged)
  Q_PROPERTY(QString selfToxId READ selfToxId NOTIFY selfToxIdChanged)
  Q_PROPERTY(QString networkStatus READ networkStatus NOTIFY networkStatusChanged)
  Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
  Q_PROPERTY(bool darkTheme READ darkTheme NOTIFY darkThemeChanged)
  Q_PROPERTY(int noticeUnread READ noticeUnread NOTIFY noticeUnreadChanged)
  Q_PROPERTY(QStringList knownAccounts READ knownAccounts NOTIFY knownAccountsChanged)
  Q_PROPERTY(QString selectedConversationIdentifier READ selectedConversationIdentifier NOTIFY selectedConversationChanged)
  Q_PROPERTY(QString selectedConversationKind READ selectedConversationKind NOTIFY selectedConversationChanged)
  Q_PROPERTY(QString selectedConversationTitle READ selectedConversationTitle NOTIFY selectedConversationChanged)
  Q_PROPERTY(bool callActive READ callActive NOTIFY callStateChanged)
  Q_PROPERTY(bool callVideoEnabled READ callVideoEnabled NOTIFY callStateChanged)
  Q_PROPERTY(QString callTitle READ callTitle NOTIFY callStateChanged)
  Q_PROPERTY(QString callStatus READ callStatus NOTIFY callStateChanged)

  public:
  explicit AppController(QObject *parent = nullptr);
  ~AppController() override = default;

  bool loggedIn() const;
  QString accountName() const;
  QString selfToxId() const;
  QString networkStatus() const;
  QString statusMessage() const;
  bool darkTheme() const;
  int noticeUnread() const;
  QStringList knownAccounts() const;
  QString selectedConversationIdentifier() const;
  QString selectedConversationKind() const;
  QString selectedConversationTitle() const;
  bool callActive() const;
  bool callVideoEnabled() const;
  QString callTitle() const;
  QString callStatus() const;

  ContactListModel *friendModel();
  ContactListModel *groupModel();
  ChatMessageModel *chatModel();
  NoticeModel *noticeModel();

  Q_INVOKABLE bool isKnownAccount(QString const &account) const;
  Q_INVOKABLE void loginOrRegister(QString const &account, QString const &password,
                                   QString const &confirmPassword,
                                   bool registerNew);
  Q_INVOKABLE void changePassword(QString const &account,
                                  QString const &oldPassword,
                                  QString const &newPassword,
                                  QString const &confirmPassword);
  Q_INVOKABLE void exitApplication();
  Q_INVOKABLE void copySelfId();
  Q_INVOKABLE void toggleTheme();
  Q_INVOKABLE void setStatusMessage(QString const &message);
  Q_INVOKABLE void markNoticesRead();
  Q_INVOKABLE void selectFriend(QString const &identifier);
  Q_INVOKABLE void selectGroup(QString const &identifier);
  Q_INVOKABLE void selectAssistant();
  Q_INVOKABLE void addFriend(QString const &toxId, QString const &message);
  Q_INVOKABLE void deleteSelectedFriend();
  Q_INVOKABLE void createGroup(QString const &title);
  Q_INVOKABLE void inviteSelectedFriendToGroup();
  Q_INVOKABLE void leaveSelectedGroup();
  Q_INVOKABLE void sendMessage(QString const &text);
  Q_INVOKABLE void sendFileStub();
  Q_INVOKABLE void startAudioCall();
  Q_INVOKABLE void startVideoCall();
  Q_INVOKABLE void answerCall();
  Q_INVOKABLE void hangupCall();

  signals:
  void loggedInChanged();
  void accountNameChanged();
  void selfToxIdChanged();
  void networkStatusChanged();
  void statusMessageChanged();
  void darkThemeChanged();
  void noticeUnreadChanged();
  void knownAccountsChanged();
  void selectedConversationChanged();
  void callStateChanged();
  void callShellRequested();

  private slots:
  void onIterateTick();
  void onRefreshTick();

  private:
  enum class ConversationKind { None, Friend, Group, Assistant };

  void startTox();
  void registerToxCallbacks();
  void bootstrapFromConfig();
  void scheduleIterate();
  void refreshFriendList();
  void refreshGroupList();
  void persistSavedata();
  void addNotice(QString const &category, QString const &text,
                 QString const &severity = QStringLiteral("info"));
  void selectConversation(ConversationKind kind, QString const &identifier,
                          QString const &title);
  void loadSelectedConversation();
  void appendMessageToConversation(ConversationKind kind, QString const &identifier,
                                   ChatMessageItem message);
  void appendCurrentSystemMessage(QString const &text,
                                  QString const &severity = QStringLiteral("info"));
  QString conversationKey(ConversationKind kind, QString const &identifier) const;
  QString conversationKindText(ConversationKind kind) const;
  QString currentConversationTitle() const;
  QString friendDisplayName(uint32_t friendNumber) const;
  QString friendPublicKey(uint32_t friendNumber);
  QString groupDisplayName(uint32_t conferenceNumber) const;
  QString connectionLabel(TOX_CONNECTION connection) const;
  ContactItem contactFromFriend(uint32_t friendNumber) const;
  ContactItem contactFromGroup(uint32_t conferenceNumber) const;
  uint32_t numericIdentifier(QString const &identifier) const;
  QString makeMessageIdentifier();
  QString makeStubFriendIdentifier();
  QString makeStubGroupIdentifier();

  ProfileService profileService_;
  StorageService storageService_;
  FileTransferService fileTransferService_;
  CallService callService_;
  AiAssistantService aiAssistantService_;
  GroupPersistenceService groupPersistenceService_;

  ContactListModel friendModel_;
  ContactListModel groupModel_;
  ChatMessageModel chatModel_;
  NoticeModel noticeModel_;

  std::unique_ptr<ToxCore::ToxCoreWrapper> tox_;
  QTimer iterateTimer_;
  QTimer refreshTimer_;

  bool loggedIn_{false};
  QString accountName_;
  QString selfToxId_{QStringLiteral("Tox ID will appear after startup")};
  QString networkStatus_{QStringLiteral("network: offline")};
  QString statusMessage_{QStringLiteral("session-only profile")};
  bool darkTheme_{true};
  int noticeUnread_{0};

  ConversationKind selectedKind_{ConversationKind::None};
  QString selectedIdentifier_;
  QString selectedTitle_{QStringLiteral("未选择会话")};

  bool callActive_{false};
  bool callVideoEnabled_{false};
  QString callTitle_;
  QString callStatus_;

  QHash<uint32_t, int> friendUnreadCount_;
  QHash<uint32_t, int> groupUnreadCount_;
  QHash<uint32_t, TOX_CONNECTION> friendConnectionCache_;
  QHash<uint32_t, QString> friendPublicKeyCache_;
  QHash<QString, ContactItem> stubFriends_;
  QHash<QString, ContactItem> stubGroups_;
  QHash<QString, QVector<ChatMessageItem>> chatHistory_;

  TOX_CONNECTION selfConnection_{TOX_CONNECTION_NONE};
  uint64_t messageSequence_{0};
  uint32_t stubFriendSequence_{0};
  uint32_t stubGroupSequence_{0};
};
