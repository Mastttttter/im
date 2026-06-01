#pragma once

#include "core/ToxCoreWrapper.h"
#include <QMainWindow>
#include <QHash>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <memory>

class ChatInputEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QDockWidget;
class QTabWidget;
class QTextBrowser;
class QTimer;

class MainWindow final : public QMainWindow {
  Q_OBJECT

  public:
  explicit MainWindow(QString const &profileName, QString const &password,
                      QWidget *parent = nullptr);
  ~MainWindow() override = default;

  private slots:
  void OnAddFriendClicked_();
  void OnDeleteFriendClicked_();
  void OnSendClicked_();
  void OnFriendSelectionChanged_();
  void OnThemeToggleClicked_();
  void OnCreateGroupClicked_();
  void OnInviteToGroupClicked_();
  void OnGroupSelectionChanged_();
  void OnLeaveGroupClicked_();
  void OnIterateTick_();
  void OnRefreshTick_();

  private:
  void BuildUi_();
  void WireSignals_();
  void ApplyTheme_();

  void StartTox_();
  void BootstrapFromConfig_();
  void ScheduleIterate_();

  void RefreshFriendList_();
  uint32_t CurrentFriendNumber_() const;
  QString FriendPubKeyHex_(uint32_t friendNumber);
  QString GetFriendDisplayName_(uint32_t friendNumber) const;

  void RefreshGroupList_();
  uint32_t CurrentGroupNumber_() const;
  QString GetGroupDisplayName_(uint32_t conferenceNumber) const;
  QStringList GetGroupPeerNames_(uint32_t conferenceNumber) const;

  void AppendSystemMessage_(QString const &text, QString const &color);
  void AppendFriendMessage_(uint32_t friendNumber, QString const &sender,
                            QString const &message, bool outgoing);
  void AppendGroupMessage_(uint32_t conferenceNumber, QString const &sender,
                           QString const &message, bool outgoing);
  void RenderCurrentConversation_();

  void SetupNotificationCenter_();
  void AppendEventLine_(QString const &category, QString const &text);
  void UpdateNoticeBadge_();

  QString ConnectionLabel_(TOX_CONNECTION connection) const;

  std::unique_ptr<ToxCore::ToxCoreWrapper> tox_;
  QString profileName_;
  QString password_;

  QLabel *accountNameLabel_{nullptr};
  QLineEdit *selfIdEdit_{nullptr};
  QLabel *networkStatusLabel_{nullptr};

  QTabWidget *contactTabs_{nullptr};
  QListWidget *friendList_{nullptr};
  QListWidget *groupList_{nullptr};
  QPushButton *addFriendBtn_{nullptr};
  QPushButton *deleteFriendBtn_{nullptr};
  QPushButton *createGroupBtn_{nullptr};
  QPushButton *inviteToGroupBtn_{nullptr};
  QPushButton *leaveGroupBtn_{nullptr};

  QTextBrowser *chatView_{nullptr};
  ChatInputEdit *messageEdit_{nullptr};
  QPushButton *sendBtn_{nullptr};
  QPushButton *themeToggleBtn_{nullptr};
  QPushButton *noticeBtn_{nullptr};

  QDockWidget *noticeDock_{nullptr};
  QTabWidget *noticeTabs_{nullptr};
  QTextBrowser *noticeStatusView_{nullptr};
  QTextBrowser *noticeLogView_{nullptr};

  QTimer *iterateTimer_{nullptr};
  QTimer *refreshTimer_{nullptr};

  QHash<uint32_t, int> friendUnreadCount_;
  QHash<uint32_t, int> groupUnreadCount_;
  QHash<uint32_t, TOX_CONNECTION> friendConnectionCache_;
  QHash<uint32_t, QString> friendPublicKeyCache_;
  QHash<uint32_t, QStringList> friendChatLines_;
  QHash<uint32_t, QStringList> groupChatLines_;
  QStringList noticeRecentLines_;

  TOX_CONNECTION selfConnection_{TOX_CONNECTION_NONE};
  uint32_t currentFriendNumber_{UINT32_MAX};
  uint32_t currentGroupNumber_{UINT32_MAX};
  bool groupChatActive_{false};
  bool isDarkTheme_{true};
  int noticeUnread_{0};
};
