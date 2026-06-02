#pragma once

#include <toxav/toxav.h>
#include <toxcore/tox.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ToxCore {

// ==================== 回调函数类型别名定义 ====================

// 好友相关回调类型
using FriendRequestCb = std::function<void(std::string const &publicKeyHex,
                                           std::string const &message)>;
using FriendConnectionStatusCb =
    std::function<void(uint32_t friendNumber, TOX_CONNECTION status)>;
using FriendMessageCb =
    std::function<void(uint32_t, TOX_MESSAGE_TYPE, std::string const &)>;

// 文件传输相关回调类型
using FileReceiveCb =
    std::function<void(uint32_t, uint32_t, std::string const &, uint64_t)>;
using FileRecvControlCb =
    std::function<void(uint32_t, uint32_t, TOX_FILE_CONTROL)>;
using FileChunkRequestCb =
    std::function<void(uint32_t, uint32_t, uint64_t, size_t)>;
using FileRecvChunkCb =
    std::function<void(uint32_t, uint32_t, uint64_t, uint8_t const *, size_t)>;

// 音视频通话相关回调类型
using CallCb = std::function<void(uint32_t, bool, bool)>;
using CallStateCb = std::function<void(uint32_t, TOXAV_FRIEND_CALL_STATE)>;
using AudioFrameCb =
    std::function<void(uint32_t, int16_t const *, size_t, uint8_t, uint32_t)>;
using VideoFrameCb =
    std::function<void(uint32_t, uint16_t, uint16_t, uint8_t const *,
                       uint8_t const *, uint8_t const *)>;

// 群聊相关回调类型
using ConferenceInviteCb =
    std::function<void(uint32_t friendNumber, TOX_CONFERENCE_TYPE type,
                       std::vector<uint8_t> const &cookie)>;
using ConferenceConnectedCb = std::function<void(uint32_t conferenceNumber)>;
using ConferenceMessageCb =
    std::function<void(uint32_t conferenceNumber, uint32_t peerNumber,
                       TOX_MESSAGE_TYPE type, std::string const &message)>;
using ConferenceTitleCb = std::function<void(
    uint32_t conferenceNumber, uint32_t peerNumber, std::string const &title)>;
using ConferencePeerNameCb = std::function<void(
    uint32_t conferenceNumber, uint32_t peerNumber, std::string const &name)>;
using ConferencePeerListChangedCb =
    std::function<void(uint32_t conferenceNumber)>;

// ==================== 参数结构体定义 ====================

/**
 * @brief 通话选项（用于 Call/Answer）
 */
struct CallOptions {
  bool audioEnabled{true};   // 是否启用音频
  bool videoEnabled{false};  // 是否启用视频
  uint32_t audioBitrateKbps{
      48};                   // 音频码率（kbps，传给 toxav_call/toxav_answer）
  uint32_t videoBitrateKbps{
      5000};                 // 视频码率（kbps，传给 toxav_call/toxav_answer）
};

/**
 * @brief 音频帧参数（用于 SendAudioFrame）
 */
struct AudioFrameParams {
  uint8_t channels{1};           // 声道数（1=单声道, 2=立体声）
  uint32_t samplingRate{48000};  // 采样率（Hz）
};

class ToxCoreWrapper {
  public:
  // ==================== 生命周期管理接口 ====================

  /**
   * @brief 构造函数：创建新的 Tox 实例（生成新身份）
   */
  explicit ToxCoreWrapper();

  /**
   * @brief 构造函数：从 savedata 恢复 Tox 实例（恢复已有身份）
   * @param savedata Tox savedata 二进制数据（包含身份信息、好友列表等）
   */
  explicit ToxCoreWrapper(std::vector<uint8_t> const &savedata);

  // 禁用拷贝构造和拷贝赋值（资源唯一所有权）
  ToxCoreWrapper(ToxCoreWrapper const &) = delete;
  ToxCoreWrapper &operator=(ToxCoreWrapper const &) = delete;

  // 支持移动语义（资源转移）
  ToxCoreWrapper(ToxCoreWrapper &&) noexcept;
  ToxCoreWrapper &operator=(ToxCoreWrapper &&) noexcept;

  /**
   * @brief 析构函数：释放 Tox 实例资源
   */
  ~ToxCoreWrapper();

  // ==================== 核心循环管理接口 ====================

  /**
   * @brief 主循环迭代函数：处理网络事件（必须定期调用）
   *
   * 该函数处理：
   * - 网络数据包的收发
   * - DHT 节点维护
   * - 好友连接状态更新
   * - 触发各类事件回调
   */
  void Iterate();

  /**
   * @brief 获取推荐的迭代间隔（毫秒）
   * @return 推荐的 Iterate() 调用间隔（通常为 50-1000 ms）
   */
  uint32_t IterationIntervalMs() const;

  // ==================== 自身信息管理接口 ====================

  /**
   * @brief 获取自己的 Tox ID（76位十六进制字符串）
   * @return Tox ID（格式：公钥32字节 + Nospam4字节 + 校验和2字节 = 76位hex）
   */
  std::string GetSelfAddressHex() const;

  /**
   * @brief 获取 Tox savedata（用于持久化存储）
   * @return savedata 二进制数据（包含身份、好友列表、群组等所有状态）
   */
  std::vector<uint8_t> GetSavedata() const;

  /**
   * @brief 设置 Nospam 值（用于防止好友请求垃圾攻击）
   * @param nospam 新的 Nospam 值（修改后 Tox ID 会变化）
   */
  void SetSelfNospam(uint32_t nospam);

  /**
   * @brief 获取当前的 Nospam 值
   * @return 当前 Nospam 值
   */
  uint32_t GetSelfNospam() const;

  /**
   * @brief 设置自己的昵称
   * @param name 昵称（UTF-8 编码，最大 128 字节）
   */
  void SetSelfName(std::string const &name);

  /**
   * @brief 获取自己的昵称
   * @return 昵称字符串
   */
  std::string GetSelfName() const;

  /**
   * @brief 设置自己的个性签名
   * @param statusMessage 个性签名（UTF-8 编码，最大 1007 字节）
   */
  void SetSelfStatusMessage(std::string const &statusMessage);

  /**
   * @brief 获取自己的个性签名
   * @return 个性签名字符串
   */
  std::string GetSelfStatusMessage() const;

  /**
   * @brief 设置自己的在线状态
   * @param status 状态枚举（NONE=在线, AWAY=离开, BUSY=忙碌）
   */
  void SetSelfStatus(TOX_USER_STATUS status);

  /**
   * @brief 获取自己的在线状态
   * @return 当前状态枚举
   */
  TOX_USER_STATUS GetSelfStatus() const;

  // ==================== 好友管理接口 ====================

  /**
   * @brief 发送好友请求（添加好友）
   * @param toxIdHex 对方的 Tox ID（76位十六进制字符串）
   * @param message 附加消息（好友请求说明）
   * @return 好友编号（friend number），失败返回 UINT32_MAX
   */
  uint32_t AddFriend(std::string const &toxIdHex, std::string const &message);

  /**
   * @brief 无请求添加好友（用于已知对方公钥的场景，如群聊成员）
   * @param publicKeyHex 对方的公钥（64位十六进制字符串）
   * @return 好友编号（friend number），失败返回 UINT32_MAX
   */
  uint32_t AddFriendNoRequest(std::string const &publicKeyHex);

  /**
   * @brief 删除好友（本地删除，不通知对方）
   * @param friendNumber 好友编号
   */
  void DeleteFriend(uint32_t friendNumber);

  /**
   * @brief 设置好友请求事件回调
   * @param cb 回调函数，参数：(对方公钥hex, 请求消息)
   */
  void SetOnFriendRequest(FriendRequestCb cb);

  /**
   * @brief 设置好友连接状态变化回调
   * @param cb 回调函数，参数：(好友编号, 连接状态)
   *
   * 连接状态：
   * - TOX_CONNECTION_NONE: 离线
   * - TOX_CONNECTION_TCP: TCP 在线
   * - TOX_CONNECTION_UDP: UDP 在线（更快）
   */
  void SetOnFriendConnectionStatus(FriendConnectionStatusCb cb);

  /**
   * @brief 获取好友列表
   * @return 所有好友的编号列表
   */
  std::vector<uint32_t> GetFriendList() const;

  /**
   * @brief 检查好友是否存在
   * @param friendNumber 好友编号
   * @return true=存在, false=不存在
   */
  bool FriendExists(uint32_t friendNumber) const;

  /**
   * @brief 获取好友的连接状态
   * @param friendNumber 好友编号
   * @return 连接状态枚举
   */
  TOX_CONNECTION GetFriendConnectionStatus(uint32_t friendNumber) const;

  /**
   * @brief 获取好友的公钥（用作持久化标识）
   * @param friendNumber 好友编号
   * @return 公钥（64位十六进制字符串）
   */
  std::string GetFriendPublicKeyHex(uint32_t friendNumber) const;

  /**
   * @brief 获取好友的昵称
   * @param friendNumber 好友编号
   * @return 昵称字符串
   */
  std::string GetFriendName(uint32_t friendNumber) const;

  // ==================== 消息收发接口 ====================

  /**
   * @brief 发送消息给好友
   * @param friendNumber 好友编号
   * @param message 消息内容（UTF-8 编码）
   * @param type 消息类型（NORMAL=普通消息, ACTION=动作消息）
   * @return 消息 ID，失败返回 0
   */
  uint32_t SendFriendMessage(uint32_t friendNumber, std::string const &message,
                             TOX_MESSAGE_TYPE type = TOX_MESSAGE_TYPE_NORMAL);

  /**
   * @brief 设置接收好友消息的回调
   * @param cb 回调函数，参数：(好友编号, 消息类型, 消息内容)
   */
  void SetOnFriendMessage(FriendMessageCb cb);

  // ==================== 网络管理接口 ====================

  /**
   * @brief 连接到 Bootstrap 节点（加入 DHT 网络）
   * @param address 节点地址（域名或 IP）
   * @param port 节点端口（通常为 33445）
   * @param publicKeyHex 节点公钥（64位十六进制字符串）
   *
   * 连接多个 bootstrap 节点可以提高连接成功率
   */
  void Bootstrap(std::string const &address, uint16_t port,
                 std::string const &publicKeyHex);

  /**
   * @brief 添加 TCP Relay 节点（用于 NAT 穿透）
   * @param address 节点地址
   * @param port 节点端口
   * @param publicKeyHex 节点公钥
   */
  void AddTcpRelay(std::string const &address, uint16_t port,
                   std::string const &publicKeyHex);

  /**
   * @brief 获取自身的网络连接状态（是否已加入 DHT 网络）
   * @return 连接状态（NONE=未连接, TCP=已连接, UDP=已连接且更快）
   */
  TOX_CONNECTION GetSelfConnectionStatus() const;

  // ==================== 文件传输接口 ====================

  /**
   * @brief 发送文件给好友
   * @param friendNumber 好友编号
   * @param filePath 文件路径（支持跨平台路径格式，Windows
   * 下自动处理宽字符路径）
   * @param fileNameUtf8 文件名（UTF-8 编码，发送给对方的显示名称）
   * @return 文件编号（file number），失败返回 UINT32_MAX
   */
  uint32_t SendFile(uint32_t friendNumber,
                    std::filesystem::path const &filePath,
                    std::string const &fileNameUtf8);

  /**
   * @brief 控制文件传输（接受/拒绝/暂停/继续/取消）
   * @param friendNumber 好友编号
   * @param fileNumber 文件编号
   * @param control 控制命令（RESUME=接受/继续, PAUSE=暂停, CANCEL=取消）
   * @return true=成功, false=失败
   */
  bool ControlFileTransfer(uint32_t friendNumber, uint32_t fileNumber,
                           TOX_FILE_CONTROL control);

  /**
   * @brief 发送文件数据块（响应对方的 ChunkRequest）
   * @param friendNumber 好友编号
   * @param fileNumber 文件编号
   * @param position 文件偏移位置
   * @param data 数据指针
   * @param length 数据长度（最大 1024 字节）
   * @return true=成功, false=失败
   */
  bool SendFileChunk(uint32_t friendNumber, uint32_t fileNumber,
                     uint64_t position, uint8_t const *data, size_t length);

  /**
   * @brief 设置收到文件传输请求的回调（接收方）
   * @param cb 回调函数，参数：(好友编号, 文件编号, 文件名, 文件大小)
   */
  void SetOnFileReceive(FileReceiveCb cb);

  /**
   * @brief 设置收到文件传输控制命令的回调（发送方/接收方）
   * @param cb 回调函数，参数：(好友编号, 文件编号, 控制命令)
   */
  void SetOnFileRecvControl(FileRecvControlCb cb);

  /**
   * @brief 设置收到文件数据块请求的回调（发送方）
   * @param cb 回调函数，参数：(好友编号, 文件编号, 文件位置, 请求长度)
   */
  void SetOnFileChunkRequest(FileChunkRequestCb cb);

  /**
   * @brief 设置收到文件数据块的回调（接收方）
   * @param cb 回调函数，参数：(好友编号, 文件编号, 文件位置, 数据指针,
   * 数据长度)
   */
  void SetOnFileRecvChunk(FileRecvChunkCb cb);

  // ==================== 音视频通话接口 ====================

  /**
   * @brief 发起音视频通话
   * @param friendNumber 好友编号
   * @param options 通话选项（音频/视频开关）
   * @return 0=成功, 负数=失败
   */
  int Call(uint32_t friendNumber, CallOptions const &options = {});

  /**
   * @brief 接听来电
   * @param friendNumber 好友编号
   * @param options 通话选项（音频/视频开关）
   * @return 0=成功, 负数=失败
   */
  int Answer(uint32_t friendNumber, CallOptions const &options = {});

  /**
   * @brief 挂断通话
   * @param friendNumber 好友编号
   * @return 0=成功, 负数=失败
   */
  int Hangup(uint32_t friendNumber);

  /**
   * @brief 发送音频帧（通话中循环调用）
   * @param friendNumber 好友编号
   * @param pcm PCM 音频数据（16-bit 有符号整数）
   * @param samples 采样点数量（每声道）
   * @param params 音频帧参数（声道数、采样率）
   * @return 0=成功, 负数=失败
   */
  int SendAudioFrame(uint32_t friendNumber, int16_t const *pcm, size_t samples,
                     AudioFrameParams const &params = {});

  /**
   * @brief 发送视频帧（通话中循环调用）
   * @param friendNumber 好友编号
   * @param width 视频宽度
   * @param height 视频高度
   * @param y Y 平面数据（亮度）
   * @param u U 平面数据（色度）
   * @param v V 平面数据（色度）
   * @return 0=成功, 负数=失败
   *
   * 注意：视频格式为 YUV420p
   */
  int SendVideoFrame(uint32_t friendNumber, uint16_t width, uint16_t height,
                     uint8_t const *y, uint8_t const *u, uint8_t const *v);

  /**
   * @brief 设置收到来电的回调
   * @param cb 回调函数，参数：(好友编号, 是否启用音频, 是否启用视频)
   */
  void SetOnCall(CallCb cb);

  /**
   * @brief 设置通话状态变化的回调
   * @param cb 回调函数，参数：(好友编号, 通话状态)
   *
   * 通话状态包括：发送/接收音频、发送/接收视频、通话结束等
   */
  void SetOnCallState(CallStateCb cb);

  /**
   * @brief 设置接收音频帧的回调
   * @param cb 回调函数，参数：(好友编号, PCM数据, 采样数, 声道数, 采样率)
   */
  void SetOnAudioFrame(AudioFrameCb cb);

  /**
   * @brief 设置接收视频帧的回调
   * @param cb 回调函数，参数：(好友编号, 宽度, 高度, Y平面, U平面, V平面)
   */
  void SetOnVideoFrame(VideoFrameCb cb);

  // ==================== 群聊（Conference）管理接口 ====================

  /**
   * @brief 创建一个新的群聊会议
   * @return 会议编号（conference number），失败返回 UINT32_MAX
   */
  uint32_t CreateConference();

  /**
   * @brief 删除群聊会议（退出群聊）
   * @param conferenceNumber 会议编号
   * @return true=成功, false=失败
   */
  bool DeleteConference(uint32_t conferenceNumber);

  /**
   * @brief 邀请好友加入群聊
   * @param friendNumber 好友编号
   * @param conferenceNumber 会议编号
   * @return true=成功, false=失败
   */
  bool InviteFriendToConference(uint32_t friendNumber,
                                uint32_t conferenceNumber);

  /**
   * @brief 加入好友邀请的群聊
   * @param friendNumber 邀请者的好友编号
   * @param cookie 邀请 cookie（从 SetOnConferenceInvite 回调中获取）
   * @return true=成功, false=失败
   *
   * 注意：加入成功后会触发 SetOnConferenceConnected 回调，获取 conferenceNumber
   */
  bool JoinConference(uint32_t friendNumber,
                      std::vector<uint8_t> const &cookie);

  // ==================== 群聊持久化接口 ====================

  /**
   * @brief 获取群聊的唯一 ID（用于持久化）
   * @param conferenceNumber 会议编号
   * @return 会议 ID（32 字节二进制数据）
   *
   * Conference ID 在程序重启后保持不变，用于数据库存储
   */
  std::vector<uint8_t> GetConferenceId(uint32_t conferenceNumber) const;

  /**
   * @brief 通过 Conference ID 查找会议编号
   * @param idHex Conference ID 的十六进制字符串
   * @return 会议编号（如果找到），否则返回 nullopt
   */
  std::optional<uint32_t> GetConferenceByIdHex(std::string const &idHex) const;

  /**
   * @brief 获取所有群聊列表
   * @return 所有会议编号的列表
   *
   * 注意：程序重启后，已加入的群聊会自动恢复
   */
  std::vector<uint32_t> GetConferenceList() const;

  // ==================== 群聊信息接口 ====================

  /**
   * @brief 设置群聊标题（群名）
   * @param conferenceNumber 会议编号
   * @param title 群聊标题
   * @return true=成功, false=失败
   */
  bool SetConferenceTitle(uint32_t conferenceNumber, std::string const &title);

  /**
   * @brief 获取群聊标题
   * @param conferenceNumber 会议编号
   * @return 群聊标题字符串
   */
  std::string GetConferenceTitle(uint32_t conferenceNumber) const;

  /**
   * @brief 获取群聊成员数量
   * @param conferenceNumber 会议编号
   * @return 成员数量
   */
  uint32_t GetConferencePeerCount(uint32_t conferenceNumber) const;

  /**
   * @brief 获取群聊成员的昵称
   * @param conferenceNumber 会议编号
   * @param peerNumber 成员编号
   * @return 成员昵称
   */
  std::string GetConferencePeerName(uint32_t conferenceNumber,
                                    uint32_t peerNumber) const;

  /**
   * @brief 获取群聊成员的公钥
   * @param conferenceNumber 会议编号
   * @param peerNumber 成员编号
   * @return 成员公钥（64位十六进制字符串）
   */
  std::string GetConferencePeerPublicKeyHex(uint32_t conferenceNumber,
                                            uint32_t peerNumber) const;

  /**
   * @brief 发送群聊消息
   * @param conferenceNumber 会议编号
   * @param message 消息内容
   * @return true=成功, false=失败
   */
  bool SendConferenceMessage(uint32_t conferenceNumber,
                             std::string const &message);

  // ==================== 群聊事件回调接口 ====================

  /**
   * @brief 设置收到群聊邀请的回调
   * @param cb 回调函数，参数：(邀请者好友编号, 会议类型, 邀请cookie)
   */
  void SetOnConferenceInvite(ConferenceInviteCb cb);

  /**
   * @brief 设置成功连接到群聊的回调
   * @param cb 回调函数，参数：(会议编号)
   */
  void SetOnConferenceConnected(ConferenceConnectedCb cb);

  /**
   * @brief 设置接收群聊消息的回调
   * @param cb 回调函数，参数：(会议编号, 发送者成员编号, 消息类型, 消息内容)
   */
  void SetOnConferenceMessage(ConferenceMessageCb cb);

  /**
   * @brief 设置群聊标题变化的回调
   * @param cb 回调函数，参数：(会议编号, 修改者成员编号, 新标题)
   */
  void SetOnConferenceTitle(ConferenceTitleCb cb);

  /**
   * @brief 设置成员昵称变化的回调
   * @param cb 回调函数，参数：(会议编号, 成员编号, 新昵称)
   */
  void SetOnConferencePeerName(ConferencePeerNameCb cb);

  /**
   * @brief 设置成员列表变化的回调（有人加入/离开）
   * @param cb 回调函数，参数：(会议编号)
   */
  void SetOnConferencePeerListChanged(ConferencePeerListChangedCb cb);

  private:
  // ==================== 用户回调函数存储（C++ lambda/function）
  // ====================

  // 好友相关回调
  FriendRequestCb onFriendRequest_;                    // 好友请求回调
  FriendConnectionStatusCb onFriendConnectionStatus_;  // 好友连接状态变化回调
  FriendMessageCb onFriendMessage_;                    // 好友消息接收回调

  // 文件传输相关回调
  FileReceiveCb onFileReceive_;            // 文件传输请求回调
  FileRecvControlCb onFileRecvControl_;    // 文件传输控制回调
  FileChunkRequestCb onFileChunkRequest_;  // 文件数据块请求回调
  FileRecvChunkCb onFileRecvChunk_;        // 文件数据块接收回调

  // 音视频通话相关回调
  CallCb onCall_;              // 来电回调
  CallStateCb onCallState_;    // 通话状态变化回调
  AudioFrameCb onAudioFrame_;  // 音频帧接收回调
  VideoFrameCb onVideoFrame_;  // 视频帧接收回调

  // 群聊相关回调
  ConferenceInviteCb onConferenceInvite_;                    // 群聊邀请回调
  ConferenceConnectedCb onConferenceConnected_;              // 群聊连接成功回调
  ConferenceMessageCb onConferenceMessage_;                  // 群聊消息回调
  ConferenceTitleCb onConferenceTitle_;                      // 群聊标题变化回调
  ConferencePeerNameCb onConferencePeerName_;                // 成员昵称变化回调
  ConferencePeerListChangedCb onConferencePeerListChanged_;  // 成员列表变化回调

  // ==================== 私有辅助方法 ====================

  /**
   * @brief 刷新所有回调函数注册（将 C++ 回调绑定到 toxcore C 回调）
   *
   * 该方法在构造时和移动赋值时调用，确保回调正确绑定
   */
  void RefreshCallbacks_();

  /**
   * @brief 初始化 ToxAV（音视频通话模块）
   *
   * 该方法在 Tox 实例创建后调用，初始化音视频功能
   */
  void InitAv_();

  // ==================== 静态回调桥接函数（C 回调 → C++ 回调）
  // ==================== 这些静态函数作为 toxcore C API 的回调接口，内部转发到
  // C++ 成员函数

  // 好友相关静态回调
  static void OnFriendRequestCb_(Tox *tox, uint8_t const *public_key,
                                 uint8_t const *message, size_t length,
                                 void *opaque);

  static void OnFriendConnectionStatusCb_(Tox *tox, uint32_t friend_number,
                                          TOX_CONNECTION connection_status,
                                          void *opaque);

  static void OnFriendMessageCb_(Tox *tox, uint32_t friend_number,
                                 TOX_MESSAGE_TYPE type, uint8_t const *message,
                                 size_t length, void *opaque);

  // 文件传输相关静态回调
  static void OnFileRecvCb_(Tox *tox, uint32_t friend_number,
                            uint32_t file_number, uint32_t kind,
                            uint64_t file_size, uint8_t const *filename,
                            size_t filename_length, void *opaque);

  static void OnFileRecvControlCb_(Tox *tox, uint32_t friend_number,
                                   uint32_t file_number,
                                   TOX_FILE_CONTROL control, void *opaque);

  static void OnFileChunkRequestCb_(Tox *tox, uint32_t friend_number,
                                    uint32_t file_number, uint64_t position,
                                    size_t length, void *opaque);

  static void OnFileRecvChunkCb_(Tox *tox, uint32_t friend_number,
                                 uint32_t file_number, uint64_t position,
                                 uint8_t const *data, size_t length,
                                 void *opaque);

  // 音视频通话相关静态回调
  static void OnCallCb_(ToxAV *av, uint32_t friend_number, bool audio_enabled,
                        bool video_enabled, void *opaque);

  static void OnCallStateCb_(ToxAV *av, uint32_t friend_number, uint32_t state,
                             void *opaque);

  static void OnAudioFrameCb_(ToxAV *av, uint32_t friend_number,
                              int16_t const *pcm, size_t sample_count,
                              uint8_t channels, uint32_t sampling_rate,
                              void *opaque);

  static void OnVideoFrameCb_(ToxAV *av, uint32_t friend_number, uint16_t width,
                              uint16_t height, uint8_t const *y,
                              uint8_t const *u, uint8_t const *v,
                              int32_t ystride, int32_t ustride, int32_t vstride,
                              void *opaque);

  // 群聊相关静态回调
  static void OnConferenceInviteCb_(Tox *tox, uint32_t friend_number,
                                    TOX_CONFERENCE_TYPE type,
                                    uint8_t const *cookie, size_t length,
                                    void *opaque);

  static void OnConferenceConnectedCb_(Tox *tox, uint32_t conference_number,
                                       void *opaque);

  static void OnConferenceMessageCb_(Tox *tox, uint32_t conference_number,
                                     uint32_t peer_number,
                                     TOX_MESSAGE_TYPE type,
                                     uint8_t const *message, size_t length,
                                     void *opaque);

  static void OnConferenceTitleCb_(Tox *tox, uint32_t conference_number,
                                   uint32_t peer_number, uint8_t const *title,
                                   size_t length, void *opaque);

  static void OnConferencePeerNameCb_(Tox *tox, uint32_t conference_number,
                                      uint32_t peer_number, uint8_t const *name,
                                      size_t length, void *opaque);

  static void OnConferencePeerListChangedCb_(Tox *tox,
                                             uint32_t conference_number,
                                             void *opaque);

  // ==================== 成员变量 ====================

  Tox *tox_{nullptr};   // Toxcore 核心实例指针
  ToxAV *av_{nullptr};  // ToxAV 音视频实例指针
};

}  // namespace ToxCore
