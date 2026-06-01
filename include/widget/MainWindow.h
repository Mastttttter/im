#pragma once
#include <QMainWindow>

class QListWidget;
class QListWidgetItem;
class QLabel;
class QTextBrowser;
class QLineEdit;
class QPushButton;
class QCloseEvent;
class CallWindow;
class ChatInputEdit;
class QDockWidget;
class QTabWidget;
class AiCurlClient;

class MainWindow : public QMainWindow {
  Q_OBJECT
  public:
  explicit MainWindow(QString const &profileName, QString const &password,
                      QWidget *parent = nullptr);
  ~MainWindow() override = default;

  private slots:
  // 点击"添加好友"按钮：弹出 AddFriendDialog，accept 后调用 addFriend
  void OnAddFriendClicked_();
  // 点击"删除好友"按钮：删除当前选中好友（本地删除）
  void OnDeleteFriendClicked_();
  // 点击"编辑备注"按钮：编辑当前选中好友的备注名
  void OnEditNicknameClicked_();
  // 点击"发送"或回车：对当前选中好友发送消息
  void OnSendClicked_();
  // 点击"发送文件"按钮：选择文件并发送给当前好友
  void OnSendFileClicked_();
  // 好友选择变化：切换会话显示
  void OnFriendSelectionChanged_();
  // iterate 定时器 tick：调用 tox.iterate() 并重新调度
  void OnIterateTick_();
  // save 定时器 tick：写入 tox savedata
  void OnSaveTick_();
  // 编辑个签
  void OnEditStatusMessageClicked_();
  // 音频通话
  void OnCallClicked_();
  // 视频通话
  void OnVideoCallClicked_();
  // 切换主题
  void OnThemeToggleClicked_();
  // 群聊相关槽函数
  void OnCreateGroupClicked_();     // 创建群聊
  void OnInviteToGroupClicked_();   // 邀请好友入群
  void OnGroupSelectionChanged_();  // 群聊选择变化
  void OnLeaveGroupClicked_();      // 退出群聊

  private:
  void BuildUi_();
  void WireSignals_();
  // ==================== 成员变量：基础配置 ====================
  QString profileName_{QStringLiteral("default")};  // 账号名称
  QString password_;           // 数据库密码（内存中，用于 SQLCipher）
  bool dbOpenSuccess_{false};  // 标记数据库是否成功打开
  bool exitRequested_{false};  // 标记用户是否请求退出

  // ==================== 成员变量：UI 组件 ====================
  // 顶部信息
  QLabel *accountNameLabel_{nullptr};  // 账号名称标签
  QLineEdit *selfIdEdit_{nullptr};     // 显示自己的 ToxID，支持选择复制
  QPushButton *statusBtn_{nullptr};    // 个性签名按钮

  // 好友列表
  QListWidget *friendList_{nullptr};       // 好友列表
  QPushButton *addFriendBtn_{nullptr};     // 添加好友按钮
  QPushButton *deleteFriendBtn_{nullptr};  // 删除好友按钮
  QPushButton *editNicknameBtn_{nullptr};  // 编辑备注按钮

  // 群聊列表
  QListWidget *groupList_{nullptr};         // 群聊列表
  QPushButton *createGroupBtn_{nullptr};    // 创建群聊按钮
  QPushButton *inviteToGroupBtn_{nullptr};  // 邀请好友入群按钮
  QPushButton *leaveGroupBtn_{nullptr};     // 退出群聊按钮
  QTabWidget *contactTabWidget_{nullptr};   // 好友/群聊切换Tab

  // 聊天区域
  QTextBrowser *chatView_{nullptr};       // 聊天消息显示区
  ChatInputEdit *messageEdit_{nullptr};   // 消息输入框
  QPushButton *sendBtn_{nullptr};         // 发送消息按钮
  QPushButton *sendFileBtn_{nullptr};     // 发送文件按钮
  QPushButton *callBtn_{nullptr};         // 音频通话按钮
  QPushButton *videoCallBtn_{nullptr};    // 视频通话按钮
  QPushButton *themeToggleBtn_{nullptr};  // 主题切换按钮
  QPushButton *noticeBtn_{nullptr};       // 通知中心按钮
  bool isDarkTheme_{true};                // 当前是否为深色主题
  int noticeUnread_{0};                   // 通知中心未读计数（窗口隐藏时累计）

  // 通知中心 Dock（用于展示异常/提示/配置等）
  QDockWidget *noticeDock_{nullptr};
  QTabWidget *noticeTabs_{nullptr};
  QTextBrowser *noticeLogView_{nullptr};
  QTextBrowser *noticeConfigView_{nullptr};
  // ==================== 成员变量：定时器 ====================
  std::unique_ptr<QTimer>
      iterateTimer_;  // iterate 定时器（单次触发并动态调整间隔）
  std::unique_ptr<QTimer> friendListTimer_;  // 好友列表刷新定时器
  std::unique_ptr<QTimer>
      saveTimer_;  // savedata 保存定时器（debounce 合并写入）
};
