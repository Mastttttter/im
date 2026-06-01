#include "core/ToxCoreWrapper.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace ToxCore {

namespace {

// 将单个 hex 字符转换为 0~15 的半字节值
uint8_t HexNibble(char c) {
  if (c >= '0' && c <= '9') {
    return static_cast<uint8_t>(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<uint8_t>(10 + (c - 'a'));
  }
  if (c >= 'A' && c <= 'F') {
    return static_cast<uint8_t>(10 + (c - 'A'));
  }

  throw std::invalid_argument("hex string contains non-hex character");
}

// 将固定长度的 hex 字符串解码为字节数组
template <size_t N>
std::array<uint8_t, N> DecodeHexFixed(std::string_view hex) {
  if (hex.size() != N * 2) {
    throw std::invalid_argument("hex string has invalid length");
  }

  std::array<uint8_t, N> out{};
  for (size_t i = 0; i < N; ++i) {
    uint8_t const hi = HexNibble(hex[i * 2]);
    uint8_t const lo = HexNibble(hex[i * 2 + 1]);
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return out;
}

// 将 bytes 编码成十六进制字符串（大写）
//
// - 用途：toxcore 的许多“身份类数据”是二进制（例如 address/public
// key），上层通常需要 hex 文本展示/复制
// - 约定：这里固定输出大写 hex，便于统一 UI/日志格式（大小写对 toxcore
// 本身并不重要）
std::string EncodeHexUpper(uint8_t const *data, size_t len) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out(len * 2, '\0');
  for (size_t i = 0; i < len; ++i) {
    uint8_t const b = data[i];
    out[i * 2] = kHex[(b >> 4) & 0x0F];
    out[i * 2 + 1] = kHex[b & 0x0F];
  }
  return out;
}

// 将任意长度的 hex 字符串解码为字节向量（用于 Conference ID 等）
bool DecodeHex(std::string const &hex, std::vector<uint8_t> &out) {
  if (hex.size() % 2 != 0) {
    return false;
  }
  out.clear();
  out.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    try {
      uint8_t const hi = HexNibble(hex[i]);
      uint8_t const lo = HexNibble(hex[i + 1]);
      out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    } catch (...) {
      return false;
    }
  }
  return true;
}

}  // namespace

// 刷新所有回调注册：将 C++ 回调函数绑定到 toxcore C API
void ToxCoreWrapper::RefreshCallbacks_() {
  if (!tox_) {
    return;
  }
  tox_callback_friend_request(
      tox_, onFriendRequest_ ? &ToxCoreWrapper::OnFriendRequestCb_ : nullptr);

  tox_callback_friend_connection_status(
      tox_, onFriendConnectionStatus_
                ? &ToxCoreWrapper::OnFriendConnectionStatusCb_
                : nullptr);

  tox_callback_friend_message(
      tox_, onFriendMessage_ ? &ToxCoreWrapper::OnFriendMessageCb_ : nullptr);

  // 文件传输回调注册
  tox_callback_file_recv(
      tox_, onFileReceive_ ? &ToxCoreWrapper::OnFileRecvCb_ : nullptr);

  tox_callback_file_recv_control(
      tox_,
      onFileRecvControl_ ? &ToxCoreWrapper::OnFileRecvControlCb_ : nullptr);

  tox_callback_file_chunk_request(
      tox_,
      onFileChunkRequest_ ? &ToxCoreWrapper::OnFileChunkRequestCb_ : nullptr);

  tox_callback_file_recv_chunk(
      tox_, onFileRecvChunk_ ? &ToxCoreWrapper::OnFileRecvChunkCb_ : nullptr);

  if (av_) {
    toxav_callback_call(av_, onCall_ ? &ToxCoreWrapper::OnCallCb_ : nullptr,
                        this);

    toxav_callback_call_state(
        av_, onCallState_ ? &ToxCoreWrapper::OnCallStateCb_ : nullptr, this);

    toxav_callback_audio_receive_frame(
        av_, onAudioFrame_ ? &ToxCoreWrapper::OnAudioFrameCb_ : nullptr, this);

    toxav_callback_video_receive_frame(
        av_, onVideoFrame_ ? &ToxCoreWrapper::OnVideoFrameCb_ : nullptr, this);
  }

  // Conference 回调注册（新版 API）
  tox_callback_conference_invite(
      tox_,
      onConferenceInvite_ ? &ToxCoreWrapper::OnConferenceInviteCb_ : nullptr);

  tox_callback_conference_connected(
      tox_, onConferenceConnected_ ? &ToxCoreWrapper::OnConferenceConnectedCb_
                                   : nullptr);

  tox_callback_conference_message(
      tox_,
      onConferenceMessage_ ? &ToxCoreWrapper::OnConferenceMessageCb_ : nullptr);

  tox_callback_conference_title(
      tox_,
      onConferenceTitle_ ? &ToxCoreWrapper::OnConferenceTitleCb_ : nullptr);

  tox_callback_conference_peer_name(
      tox_, onConferencePeerName_ ? &ToxCoreWrapper::OnConferencePeerNameCb_
                                  : nullptr);

  tox_callback_conference_peer_list_changed(
      tox_, onConferencePeerListChanged_
                ? &ToxCoreWrapper::OnConferencePeerListChangedCb_
                : nullptr);
}

// 静态回调：收到好友请求（C API → C++ 转发）
void ToxCoreWrapper::OnFriendRequestCb_(Tox *, uint8_t const *public_key,
                                        uint8_t const *message, size_t length,
                                        void *opaque) {
  auto *self = static_cast<ToxCoreWrapper *>(opaque);
  if (!self) {
    return;
  }
  if (!self->onFriendRequest_) {
    return;
  }
  if (!public_key) {
    return;
  }

  // public_key 固定 TOX_PUBLIC_KEY_SIZE 字节
  std::string const publicKeyHex =
      EncodeHexUpper(public_key, TOX_PUBLIC_KEY_SIZE);
  std::string const msg(reinterpret_cast<char const *>(message), length);
  self->onFriendRequest_(publicKeyHex, msg);
}

// 静态回调：好友连接状态变化（C API → C++ 转发）
void ToxCoreWrapper::OnFriendConnectionStatusCb_(
    Tox *, uint32_t friend_number, TOX_CONNECTION connection_status,
    void *opaque) {
  auto *self = static_cast<ToxCoreWrapper *>(opaque);
  if (!self) {
    return;
  }
  if (!self->onFriendConnectionStatus_) {
    return;
  }
  self->onFriendConnectionStatus_(friend_number, connection_status);
}

// 静态回调：收到好友消息（C API → C++ 转发）
void ToxCoreWrapper::OnFriendMessageCb_(Tox *, uint32_t friend_number,
                                        TOX_MESSAGE_TYPE type,
                                        uint8_t const *message, size_t length,
                                        void *opaque) {
  auto *self = static_cast<ToxCoreWrapper *>(opaque);
  if (!self) {
    return;
  }
  if (!self->onFriendMessage_) {
    return;
  }
  // toxcore 的 message 是 bytes，这里按原样拷贝到 std::string。
  // 如果上层约定使用 UTF-8 文本，这里也能直接作为 UTF-8 字符串使用。
  std::string const msg(reinterpret_cast<char const *>(message), length);
  self->onFriendMessage_(friend_number, type, msg);
}

// 初始化 ToxAV 音视频模块
void ToxCoreWrapper::InitAv_() {
  TOXAV_ERR_NEW err = TOXAV_ERR_NEW_OK;
  av_ = toxav_new(tox_, &err);
  if (!av_ || err != TOXAV_ERR_NEW_OK) {
    throw std::runtime_error("toxav_new failed");
  }
  RefreshCallbacks_();
}

// 发起音视频通话
int ToxCoreWrapper::Call(uint32_t friendNumber, CallOptions const &options) {
  if (!av_) {
    return -1;
  }
  TOXAV_ERR_CALL err = TOXAV_ERR_CALL_OK;
  bool const ok =
      toxav_call(av_, friendNumber,
                 options.audioEnabled ? options.audioBitrateKbps
                                      : 0,  // audio bit rate kbps or disabled
                 options.videoEnabled ? options.videoBitrateKbps
                                      : 0,  // video bit rate kbps or disabled
                 &err);
  return (ok && err == TOXAV_ERR_CALL_OK) ? 0 : -1;
}

// 接听来电
int ToxCoreWrapper::Answer(uint32_t friendNumber, CallOptions const &options) {
  if (!av_) {
    return -1;
  }
  TOXAV_ERR_ANSWER err = TOXAV_ERR_ANSWER_OK;
  bool const ok = toxav_answer(
      av_, friendNumber, options.audioEnabled ? options.audioBitrateKbps : 0,
      options.videoEnabled ? options.videoBitrateKbps : 0, &err);
  return (ok && err == TOXAV_ERR_ANSWER_OK) ? 0 : -1;
}

// 挂断通话
int ToxCoreWrapper::Hangup(uint32_t friendNumber) {
  if (!av_) {
    return -1;
  }
  TOXAV_ERR_CALL_CONTROL err = TOXAV_ERR_CALL_CONTROL_OK;
  bool const ok =
      toxav_call_control(av_, friendNumber, TOXAV_CALL_CONTROL_CANCEL, &err);
  return (ok && err == TOXAV_ERR_CALL_CONTROL_OK) ? 0 : -1;
}

// 发送音频帧
int ToxCoreWrapper::SendAudioFrame(uint32_t friendNumber, int16_t const *pcm,
                                   size_t samples,
                                   AudioFrameParams const &params) {
  if (!av_) {
    return -1;
  }
  TOXAV_ERR_SEND_FRAME err = TOXAV_ERR_SEND_FRAME_OK;
  bool const ok =
      toxav_audio_send_frame(av_, friendNumber, pcm, samples, params.channels,
                             params.samplingRate, &err);
  return (ok && err == TOXAV_ERR_SEND_FRAME_OK) ? 0 : -1;
}

// 设置来电回调
void ToxCoreWrapper::SetOnCall(CallCb cb) {
  onCall_ = std::move(cb);
  RefreshCallbacks_();
}

// 设置通话状态变化回调
void ToxCoreWrapper::SetOnCallState(CallStateCb cb) {
  onCallState_ = std::move(cb);
  RefreshCallbacks_();
}

// 设置音频帧接收回调
void ToxCoreWrapper::SetOnAudioFrame(AudioFrameCb cb) {
  onAudioFrame_ = std::move(cb);
  RefreshCallbacks_();
}

// 发送视频帧
int ToxCoreWrapper::SendVideoFrame(uint32_t friendNumber, uint16_t width,
                                   uint16_t height, uint8_t const *y,
                                   uint8_t const *u, uint8_t const *v) {
  if (!av_) {
    return -1;
  }
  TOXAV_ERR_SEND_FRAME err = TOXAV_ERR_SEND_FRAME_OK;
  bool const ok =
      toxav_video_send_frame(av_, friendNumber, width, height, y, u, v, &err);
  return (ok && err == TOXAV_ERR_SEND_FRAME_OK) ? 0 : -1;
}

// 设置视频帧接收回调
void ToxCoreWrapper::SetOnVideoFrame(VideoFrameCb cb) {
  onVideoFrame_ = std::move(cb);
  RefreshCallbacks_();
}

// 静态回调：收到来电（C API → C++ 转发）
void ToxCoreWrapper::OnCallCb_(ToxAV *, uint32_t friend_number,
                               bool audio_enabled, bool video_enabled,
                               void *opaque) {
  auto *self = static_cast<ToxCoreWrapper *>(opaque);
  if (!self || !self->onCall_) {
    return;
  }
  self->onCall_(friend_number, audio_enabled, video_enabled);
}

// 静态回调：通话状态变化（C API → C++ 转发）
void ToxCoreWrapper::OnCallStateCb_(ToxAV *, uint32_t friend_number,
                                    uint32_t state, void *opaque) {
  auto *self = static_cast<ToxCoreWrapper *>(opaque);
  if (!self || !self->onCallState_) {
    return;
  }
  self->onCallState_(friend_number,
                     static_cast<TOXAV_FRIEND_CALL_STATE>(state));
}

// 静态回调：收到音频帧（C API → C++ 转发）
void ToxCoreWrapper::OnAudioFrameCb_(ToxAV *, uint32_t friend_number,
                                     int16_t const *pcm, size_t sample_count,
                                     uint8_t channels, uint32_t sampling_rate,
                                     void *opaque) {
  auto *self = static_cast<ToxCoreWrapper *>(opaque);
  if (!self || !self->onAudioFrame_) {
    return;
  }
  self->onAudioFrame_(friend_number, pcm, sample_count, channels,
                      sampling_rate);
}

// 静态回调：收到视频帧（C API → C++ 转发）
void ToxCoreWrapper::OnVideoFrameCb_(ToxAV *, uint32_t friend_number,
                                     uint16_t width, uint16_t height,
                                     uint8_t const *y, uint8_t const *u,
                                     uint8_t const *v, int32_t ystride,
                                     int32_t ustride, int32_t vstride,
                                     void *opaque) {
  auto *self = static_cast<ToxCoreWrapper *>(opaque);
  if (!self || !self->onVideoFrame_) {
    return;
  }

  // 处理 stride: 如果 stride != width，需要复制数据
  // 为简化，这里创建临时缓冲区
  std::vector<uint8_t> yPlane(width * height);
  std::vector<uint8_t> uPlane((width / 2) * (height / 2));
  std::vector<uint8_t> vPlane((width / 2) * (height / 2));

  // 复制 Y 平面
  for (uint16_t i = 0; i < height; ++i) {
    memcpy(yPlane.data() + i * width, y + i * ystride, width);
  }

  // 复制 U 平面
  for (uint16_t i = 0; i < height / 2; ++i) {
    memcpy(uPlane.data() + i * (width / 2), u + i * ustride, width / 2);
  }

  // 复制 V 平面
  for (uint16_t i = 0; i < height / 2; ++i) {
    memcpy(vPlane.data() + i * (width / 2), v + i * vstride, width / 2);
  }

  self->onVideoFrame_(friend_number, width, height, yPlane.data(),
                      uPlane.data(), vPlane.data());
}

// 构造函数：创建新的 Tox 实例
ToxCoreWrapper::ToxCoreWrapper() {
  Tox_Options *options = tox_options_new(nullptr);

  if (!options) {
    throw std::runtime_error("tox_options_new failed");
  }

  TOX_ERR_NEW new_err;
  tox_ = tox_new(options, &new_err);

  tox_options_free(options);

  if (!tox_ || new_err != TOX_ERR_NEW_OK) {
    throw std::runtime_error("tox_new failed");
  }
  InitAv_();
  // 构造时不默认注册回调；只有用户设置了 onFriendMessage 才会注册
}

// 构造函数：从 savedata 恢复 Tox 实例
ToxCoreWrapper::ToxCoreWrapper(std::vector<uint8_t> const &savedata) {
  if (savedata.empty()) {
    throw std::invalid_argument("savedata is empty");
  }

  Tox_Options *options = tox_options_new(nullptr);
  if (!options) {
    throw std::runtime_error("tox_options_new failed");
  }

  options->savedata_type = TOX_SAVEDATA_TYPE_TOX_SAVE;
  options->savedata_data = savedata.data();
  options->savedata_length = savedata.size();

  TOX_ERR_NEW new_err;
  tox_ = tox_new(options, &new_err);
  tox_options_free(options);

  if (!tox_ || new_err != TOX_ERR_NEW_OK) {
    throw std::runtime_error("tox_new (with savedata) failed");
  }
  InitAv_();
}

// 移动构造函数：转移资源所有权
ToxCoreWrapper::ToxCoreWrapper(ToxCoreWrapper &&other) noexcept
    : onFriendRequest_(std::move(other.onFriendRequest_)),
      onFriendConnectionStatus_(std::move(other.onFriendConnectionStatus_)),
      onFriendMessage_(std::move(other.onFriendMessage_)),
      onFileReceive_(std::move(other.onFileReceive_)),
      onFileRecvControl_(std::move(other.onFileRecvControl_)),
      onFileChunkRequest_(std::move(other.onFileChunkRequest_)),
      onFileRecvChunk_(std::move(other.onFileRecvChunk_)),
      onCall_(std::move(other.onCall_)),
      onCallState_(std::move(other.onCallState_)),
      onAudioFrame_(std::move(other.onAudioFrame_)),
      onVideoFrame_(std::move(other.onVideoFrame_)),
      onConferenceInvite_(std::move(other.onConferenceInvite_)),
      onConferenceConnected_(std::move(other.onConferenceConnected_)),
      onConferenceMessage_(std::move(other.onConferenceMessage_)),
      onConferenceTitle_(std::move(other.onConferenceTitle_)),
      onConferencePeerName_(std::move(other.onConferencePeerName_)),
      onConferencePeerListChanged_(
          std::move(other.onConferencePeerListChanged_)),
      tox_(other.tox_),
      av_(other.av_) {
  other.tox_ = nullptr;
  other.av_ = nullptr;
  RefreshCallbacks_();
}

// 移动赋值运算符：转移资源所有权
ToxCoreWrapper &ToxCoreWrapper::operator=(ToxCoreWrapper &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  if (tox_) {
    tox_kill(tox_);
    tox_ = nullptr;
  }
  if (av_) {
    toxav_kill(av_);
    av_ = nullptr;
  }

  onFriendRequest_ = std::move(other.onFriendRequest_);
  onFriendConnectionStatus_ = std::move(other.onFriendConnectionStatus_);
  onFriendMessage_ = std::move(other.onFriendMessage_);
  onFileReceive_ = std::move(other.onFileReceive_);
  onFileRecvControl_ = std::move(other.onFileRecvControl_);
  onFileChunkRequest_ = std::move(other.onFileChunkRequest_);
  onFileRecvChunk_ = std::move(other.onFileRecvChunk_);
  onCall_ = std::move(other.onCall_);
  onCallState_ = std::move(other.onCallState_);
  onAudioFrame_ = std::move(other.onAudioFrame_);
  onVideoFrame_ = std::move(other.onVideoFrame_);
  onConferenceInvite_ = std::move(other.onConferenceInvite_);
  onConferenceConnected_ = std::move(other.onConferenceConnected_);
  onConferenceMessage_ = std::move(other.onConferenceMessage_);
  onConferenceTitle_ = std::move(other.onConferenceTitle_);
  onConferencePeerName_ = std::move(other.onConferencePeerName_);
  onConferencePeerListChanged_ = std::move(other.onConferencePeerListChanged_);
  tox_ = other.tox_;
  av_ = other.av_;
  other.tox_ = nullptr;
  other.av_ = nullptr;

  RefreshCallbacks_();
  return *this;
}

// 连接到 Bootstrap 节点（加入 DHT 网络）
void ToxCoreWrapper::Bootstrap(std::string const &address, uint16_t port,
                               std::string const &publicKeyHex) {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }

  // toxcore 需要二进制公钥（TOX_PUBLIC_KEY_SIZE 字节），这里从 hex 解码
  auto const pubKey = DecodeHexFixed<TOX_PUBLIC_KEY_SIZE>(publicKeyHex);

  TOX_ERR_BOOTSTRAP err = TOX_ERR_BOOTSTRAP_OK;
  bool const ok =
      tox_bootstrap(tox_, address.c_str(), port, pubKey.data(), &err);
  if (!ok || err != TOX_ERR_BOOTSTRAP_OK) {
    throw std::runtime_error("tox_bootstrap failed, err=" +
                             std::to_string(static_cast<int>(err)));
  }
}

// 添加 TCP Relay 节点（用于 NAT 穿透）
void ToxCoreWrapper::AddTcpRelay(std::string const &address, uint16_t port,
                                 std::string const &publicKeyHex) {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }

  auto const pubKey = DecodeHexFixed<TOX_PUBLIC_KEY_SIZE>(publicKeyHex);

  TOX_ERR_BOOTSTRAP err = TOX_ERR_BOOTSTRAP_OK;
  bool const ok =
      tox_add_tcp_relay(tox_, address.c_str(), port, pubKey.data(), &err);
  if (!ok || err != TOX_ERR_BOOTSTRAP_OK) {
    throw std::runtime_error("tox_add_tcp_relay failed, err=" +
                             std::to_string(static_cast<int>(err)));
  }
}

// 获取自身网络连接状态
// 获取自身网络连接状态
TOX_CONNECTION ToxCoreWrapper::GetSelfConnectionStatus() const {
  if (!tox_) {
    return TOX_CONNECTION_NONE;
  }
  return tox_self_get_connection_status(tox_);
}

// 执行一次网络事件循环迭代
// 主循环迭代：处理网络事件和回调
void ToxCoreWrapper::Iterate() {
  if (!tox_) {
    return;
  }
  tox_iterate(tox_, this);
  if (av_) {
    toxav_iterate(av_);
  }
}

// 获取推荐的迭代间隔（毫秒）
uint32_t ToxCoreWrapper::IterationIntervalMs() const {
  if (!tox_) {
    return 0;
  }
  uint32_t const coreMs = tox_iteration_interval(tox_);
  uint32_t const avMs = av_ ? toxav_iteration_interval(av_) : coreMs;
  return std::min(coreMs, avMs);
}

// 发送好友请求
uint32_t ToxCoreWrapper::AddFriend(std::string const &toxIdHex,
                                   std::string const &message) {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }

  if (message.empty()) {
    throw std::invalid_argument("friend request message is empty");
  }
  if (message.size() > TOX_MAX_FRIEND_REQUEST_LENGTH) {
    throw std::invalid_argument("friend request message is too long");
  }

  // toxcore 的 tox_friend_add 需要的是二进制 TOX 地址（固定 TOX_ADDRESS_SIZE
  // 字节）。 上层常用 hex 表示，因此这里先做严格的 hex 解码与长度校验。
  auto const address = DecodeHexFixed<TOX_ADDRESS_SIZE>(toxIdHex);

  TOX_ERR_FRIEND_ADD err = TOX_ERR_FRIEND_ADD_OK;
  uint32_t const friendNum = tox_friend_add(
      tox_, address.data(), reinterpret_cast<uint8_t const *>(message.data()),
      message.size(), &err);

  if (err != TOX_ERR_FRIEND_ADD_OK) {
    throw std::runtime_error("tox_friend_add failed, err=" +
                             std::to_string(static_cast<int>(err)));
  }

  return friendNum;
}

// 无请求添加好友（用于已知公钥的场景）
uint32_t ToxCoreWrapper::AddFriendNoRequest(std::string const &publicKeyHex) {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }

  // toxcore 的 tox_friend_add_norequest 需要的是对方公钥（固定
  // TOX_PUBLIC_KEY_SIZE 字节） 典型场景：在 friend_request
  // 回调里拿到对方公钥后，“同意”即调用此接口。
  auto const pubKey = DecodeHexFixed<TOX_PUBLIC_KEY_SIZE>(publicKeyHex);

  TOX_ERR_FRIEND_ADD err = TOX_ERR_FRIEND_ADD_OK;
  uint32_t const friendNum =
      tox_friend_add_norequest(tox_, pubKey.data(), &err);

  if (err != TOX_ERR_FRIEND_ADD_OK) {
    throw std::runtime_error("tox_friend_add_norequest failed, err=" +
                             std::to_string(static_cast<int>(err)));
  }

  return friendNum;
}

// 删除好友
void ToxCoreWrapper::DeleteFriend(uint32_t friendNumber) {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }

  TOX_ERR_FRIEND_DELETE err{};  // 删除好友错误码
  bool const ok = tox_friend_delete(tox_, friendNumber, &err);
  if (!ok) {
    throw std::runtime_error("tox_friend_delete failed, err=" +
                             std::to_string(static_cast<int>(err)));
  }
}

// 发送消息给好友
uint32_t ToxCoreWrapper::SendFriendMessage(uint32_t friendNumber,
                                           std::string const &message,
                                           TOX_MESSAGE_TYPE type) {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }

  if (message.empty()) {
    throw std::invalid_argument("message is empty");
  }
  if (message.size() > TOX_MAX_MESSAGE_LENGTH) {
    throw std::invalid_argument("message is too long");
  }

  TOX_ERR_FRIEND_SEND_MESSAGE err = TOX_ERR_FRIEND_SEND_MESSAGE_OK;
  uint32_t const msgId = tox_friend_send_message(
      tox_, friendNumber, type,
      reinterpret_cast<uint8_t const *>(message.data()), message.size(), &err);

  if (err != TOX_ERR_FRIEND_SEND_MESSAGE_OK) {
    throw std::runtime_error("tox_friend_send_message failed, err=" +
                             std::to_string(static_cast<int>(err)));
  }

  return msgId;
}

// 设置好友消息接收回调
void ToxCoreWrapper::SetOnFriendMessage(FriendMessageCb cb) {
  onFriendMessage_ = std::move(cb);
  RefreshCallbacks_();
}

// 设置好友请求回调
void ToxCoreWrapper::SetOnFriendRequest(FriendRequestCb cb) {
  onFriendRequest_ = std::move(cb);
  RefreshCallbacks_();
}

// 设置好友连接状态变化回调
void ToxCoreWrapper::SetOnFriendConnectionStatus(FriendConnectionStatusCb cb) {
  onFriendConnectionStatus_ = std::move(cb);
  RefreshCallbacks_();
}

// 获取好友列表
std::vector<uint32_t> ToxCoreWrapper::GetFriendList() const {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }

  size_t const n = tox_self_get_friend_list_size(tox_);
  std::vector<uint32_t> list(n);
  if (n > 0) {
    tox_self_get_friend_list(tox_, list.data());
  }
  return list;
}

// 检查好友是否存在
bool ToxCoreWrapper::FriendExists(uint32_t friendNumber) const {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }
  return tox_friend_exists(tox_, friendNumber);
}

// 获取好友连接状态
TOX_CONNECTION ToxCoreWrapper::GetFriendConnectionStatus(
    uint32_t friendNumber) const {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }

  TOX_ERR_FRIEND_QUERY err = TOX_ERR_FRIEND_QUERY_OK;
  const TOX_CONNECTION status =
      tox_friend_get_connection_status(tox_, friendNumber, &err);
  if (err != TOX_ERR_FRIEND_QUERY_OK) {
    throw std::runtime_error("tox_friend_get_connection_status failed, err=" +
                             std::to_string(static_cast<int>(err)));
  }
  return status;
}

// 获取好友公钥（hex 格式）
std::string ToxCoreWrapper::GetFriendPublicKeyHex(uint32_t friendNumber) const {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }

  std::array<uint8_t, TOX_PUBLIC_KEY_SIZE> pk{};
  TOX_ERR_FRIEND_GET_PUBLIC_KEY err = TOX_ERR_FRIEND_GET_PUBLIC_KEY_OK;
  bool const ok =
      tox_friend_get_public_key(tox_, friendNumber, pk.data(), &err);
  if (!ok || err != TOX_ERR_FRIEND_GET_PUBLIC_KEY_OK) {
    throw std::runtime_error("tox_friend_get_public_key failed, err=" +
                             std::to_string(static_cast<int>(err)));
  }
  return EncodeHexUpper(pk.data(), pk.size());
}

// 获取好友昵称
std::string ToxCoreWrapper::GetFriendName(uint32_t friendNumber) const {
  if (!tox_) {
    return "";
  }

  TOX_ERR_FRIEND_QUERY err = TOX_ERR_FRIEND_QUERY_OK;
  size_t const nameLen = tox_friend_get_name_size(tox_, friendNumber, &err);
  if (err != TOX_ERR_FRIEND_QUERY_OK || nameLen == 0) {
    return "";
  }

  std::vector<uint8_t> nameBuf(nameLen);
  if (!tox_friend_get_name(tox_, friendNumber, nameBuf.data(), &err) ||
      err != TOX_ERR_FRIEND_QUERY_OK) {
    return "";
  }

  return std::string(reinterpret_cast<char const *>(nameBuf.data()), nameLen);
}

// 获取自己的 Tox ID（hex 格式）
std::string ToxCoreWrapper::GetSelfAddressHex() const {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }
  std::array<uint8_t, TOX_ADDRESS_SIZE> address{};
  tox_self_get_address(tox_, address.data());
  return EncodeHexUpper(address.data(), address.size());
}

// 获取 Tox savedata（用于持久化）
std::vector<uint8_t> ToxCoreWrapper::GetSavedata() const {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }
  size_t const n = tox_get_savedata_size(tox_);
  if (n == 0) {
    return {};
  }
  std::vector<uint8_t> out(n);
  tox_get_savedata(tox_, out.data());
  return out;
}

// 设置 Nospam 值
void ToxCoreWrapper::SetSelfNospam(uint32_t nospam) {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }
  tox_self_set_nospam(tox_, nospam);
}

// 获取 Nospam 值
uint32_t ToxCoreWrapper::GetSelfNospam() const {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }
  return tox_self_get_nospam(tox_);
}

// 设置自己的昵称
void ToxCoreWrapper::SetSelfName(std::string const &name) {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }
  if (name.size() > TOX_MAX_NAME_LENGTH) {
    throw std::invalid_argument("name is too long");
  }

  // toxcore 的 set-name 支持 length=0：此时 name 指针可以为 NULL，表示清空昵称
  TOX_ERR_SET_INFO err = TOX_ERR_SET_INFO_OK;
  uint8_t const *ptr =
      name.empty() ? nullptr : reinterpret_cast<uint8_t const *>(name.data());
  bool const ok = tox_self_set_name(tox_, ptr, name.size(), &err);
  if (!ok || err != TOX_ERR_SET_INFO_OK) {
    throw std::runtime_error("tox_self_set_name failed, err=" +
                             std::to_string(static_cast<int>(err)));
  }
}

// 获取自己的昵称
std::string ToxCoreWrapper::GetSelfName() const {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }

  size_t const len = tox_self_get_name_size(tox_);
  if (len == 0) {
    return {};
  }

  std::string name(len, '\0');
  tox_self_get_name(tox_, reinterpret_cast<uint8_t *>(name.data()));
  return name;
}

// 设置自己的个性签名
void ToxCoreWrapper::SetSelfStatusMessage(std::string const &statusMessage) {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }
  if (statusMessage.size() > TOX_MAX_STATUS_MESSAGE_LENGTH) {
    throw std::invalid_argument("status message is too long");
  }

  // toxcore 的 set-status-message 支持 length=0：此时指针可为
  // NULL，表示清空状态消息。
  TOX_ERR_SET_INFO err = TOX_ERR_SET_INFO_OK;
  uint8_t const *ptr =
      statusMessage.empty()
          ? nullptr
          : reinterpret_cast<uint8_t const *>(statusMessage.data());
  bool const ok =
      tox_self_set_status_message(tox_, ptr, statusMessage.size(), &err);
  if (!ok || err != TOX_ERR_SET_INFO_OK) {
    throw std::runtime_error("tox_self_set_status_message failed, err=" +
                             std::to_string(static_cast<int>(err)));
  }
}

// 获取自己的个性签名
std::string ToxCoreWrapper::GetSelfStatusMessage() const {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }

  size_t const len = tox_self_get_status_message_size(tox_);
  if (len == 0) {
    return {};
  }

  std::string msg(len, '\0');
  tox_self_get_status_message(tox_, reinterpret_cast<uint8_t *>(msg.data()));
  return msg;
}

// 设置自己的在线状态
void ToxCoreWrapper::SetSelfStatus(TOX_USER_STATUS status) {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }
  tox_self_set_status(tox_, status);
}

// 获取自己的在线状态
TOX_USER_STATUS ToxCoreWrapper::GetSelfStatus() const {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }
  return tox_self_get_status(tox_);
}

// 发送文件给好友
uint32_t ToxCoreWrapper::SendFile(uint32_t friendNumber,
                                  std::filesystem::path const &filePath,
                                  std::string const &fileNameUtf8) {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }
  std::ifstream file(filePath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    std::cerr << "Failed to open file: " << filePath.string() << std::endl;
    return UINT32_MAX;
  }

  // 获取文件大小
  uint64_t fileSize = file.tellg();
  file.close();

  // 发送文件传输请求
  TOX_ERR_FILE_SEND error;
  uint32_t fileNumber =
      tox_file_send(tox_, friendNumber, TOX_FILE_KIND_DATA, fileSize,
                    nullptr,  // 不使用 file_id
                    reinterpret_cast<uint8_t const *>(fileNameUtf8.c_str()),
                    fileNameUtf8.size(), &error);

  if (error != TOX_ERR_FILE_SEND_OK) {
    std::cerr << "tox_file_send failed with error: " << error << std::endl;
    return UINT32_MAX;
  }

  return fileNumber;
}

// 控制文件传输（接受/拒绝/暂停/继续/取消）
bool ToxCoreWrapper::ControlFileTransfer(uint32_t friendNumber,
                                         uint32_t fileNumber,
                                         TOX_FILE_CONTROL control) {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }

  TOX_ERR_FILE_CONTROL error;
  bool success =
      tox_file_control(tox_, friendNumber, fileNumber, control, &error);

  if (!success || error != TOX_ERR_FILE_CONTROL_OK) {
    std::cerr << "tox_file_control failed with error: " << error << std::endl;
    return false;
  }

  return true;
}

// 发送文件数据块
bool ToxCoreWrapper::SendFileChunk(uint32_t friendNumber, uint32_t fileNumber,
                                   uint64_t position, uint8_t const *data,
                                   size_t length) {
  if (!tox_) {
    throw std::logic_error("tox instance is null");
  }

  TOX_ERR_FILE_SEND_CHUNK error;
  bool success = tox_file_send_chunk(tox_, friendNumber, fileNumber, position,
                                     data, length, &error);

  if (!success || error != TOX_ERR_FILE_SEND_CHUNK_OK) {
    std::cerr << "tox_file_send_chunk failed with error: " << error
              << std::endl;
    return false;
  }

  return true;
}

// 设置文件传输请求回调
void ToxCoreWrapper::SetOnFileReceive(FileReceiveCb cb) {
  onFileReceive_ = std::move(cb);
  RefreshCallbacks_();
}

// 设置文件传输控制回调
void ToxCoreWrapper::SetOnFileRecvControl(FileRecvControlCb cb) {
  onFileRecvControl_ = std::move(cb);
  RefreshCallbacks_();
}

// 设置文件数据块请求回调
void ToxCoreWrapper::SetOnFileChunkRequest(FileChunkRequestCb cb) {
  onFileChunkRequest_ = std::move(cb);
  RefreshCallbacks_();
}

// 设置文件数据块接收回调
void ToxCoreWrapper::SetOnFileRecvChunk(FileRecvChunkCb cb) {
  onFileRecvChunk_ = std::move(cb);
  RefreshCallbacks_();
}

// 静态回调：收到文件传输请求（C API → C++ 转发）
void ToxCoreWrapper::OnFileRecvCb_(Tox *, uint32_t friend_number,
                                   uint32_t file_number, uint32_t kind,
                                   uint64_t file_size, uint8_t const *filename,
                                   size_t filename_length, void *opaque) {
  auto *self = static_cast<ToxCoreWrapper *>(opaque);
  if (!self || !self->onFileReceive_) {
    return;
  }

  // 只处理普通文件，暂不处理头像
  if (kind != TOX_FILE_KIND_DATA) {
    return;
  }

  std::string fileName(reinterpret_cast<char const *>(filename),
                       filename_length);
  self->onFileReceive_(friend_number, file_number, fileName, file_size);
}

// 静态回调：收到文件传输控制命令（C API → C++ 转发）
void ToxCoreWrapper::OnFileRecvControlCb_(Tox *, uint32_t friend_number,
                                          uint32_t file_number,
                                          TOX_FILE_CONTROL control,
                                          void *opaque) {
  auto *self = static_cast<ToxCoreWrapper *>(opaque);
  if (!self || !self->onFileRecvControl_) {
    return;
  }

  self->onFileRecvControl_(friend_number, file_number, control);
}

// 静态回调：收到文件数据块请求（C API → C++ 转发）
void ToxCoreWrapper::OnFileChunkRequestCb_(Tox *, uint32_t friend_number,
                                           uint32_t file_number,
                                           uint64_t position, size_t length,
                                           void *opaque) {
  auto *self = static_cast<ToxCoreWrapper *>(opaque);
  if (!self || !self->onFileChunkRequest_) {
    return;
  }

  self->onFileChunkRequest_(friend_number, file_number, position, length);
}

// 静态回调：收到文件数据块（C API → C++ 转发）
void ToxCoreWrapper::OnFileRecvChunkCb_(Tox *, uint32_t friend_number,
                                        uint32_t file_number, uint64_t position,
                                        uint8_t const *data, size_t length,
                                        void *opaque) {
  auto *self = static_cast<ToxCoreWrapper *>(opaque);
  if (!self || !self->onFileRecvChunk_) {
    return;
  }

  self->onFileRecvChunk_(friend_number, file_number, position, data, length);
}

// 静态回调：收到群聊邀请（C API → C++ 转发）
void ToxCoreWrapper::OnConferenceInviteCb_(Tox *, uint32_t friend_number,
                                           TOX_CONFERENCE_TYPE type,
                                           uint8_t const *cookie, size_t length,
                                           void *opaque) {
  auto *self = static_cast<ToxCoreWrapper *>(opaque);
  if (!self || !self->onConferenceInvite_) {
    return;
  }
  std::vector<uint8_t> cookieData(cookie, cookie + length);
  self->onConferenceInvite_(friend_number, type, cookieData);
}

// 静态回调：成功连接到群聊（C API → C++ 转发）
void ToxCoreWrapper::OnConferenceConnectedCb_(Tox *, uint32_t conference_number,
                                              void *opaque) {
  auto *self = static_cast<ToxCoreWrapper *>(opaque);
  if (!self || !self->onConferenceConnected_) {
    return;
  }
  self->onConferenceConnected_(conference_number);
}

// 静态回调：收到群聊消息（C API → C++ 转发）
void ToxCoreWrapper::OnConferenceMessageCb_(Tox *, uint32_t conference_number,
                                            uint32_t peer_number,
                                            TOX_MESSAGE_TYPE type,
                                            uint8_t const *message,
                                            size_t length, void *opaque) {
  auto *self = static_cast<ToxCoreWrapper *>(opaque);
  if (!self || !self->onConferenceMessage_) {
    return;
  }
  std::string const msg(reinterpret_cast<char const *>(message), length);
  self->onConferenceMessage_(conference_number, peer_number, type, msg);
}

// 静态回调：群聊标题变化（C API → C++ 转发）
void ToxCoreWrapper::OnConferenceTitleCb_(Tox *, uint32_t conference_number,
                                          uint32_t peer_number,
                                          uint8_t const *title, size_t length,
                                          void *opaque) {
  auto *self = static_cast<ToxCoreWrapper *>(opaque);
  if (!self || !self->onConferenceTitle_) {
    return;
  }
  std::string const t(reinterpret_cast<char const *>(title), length);
  self->onConferenceTitle_(conference_number, peer_number, t);
}

// 静态回调：群聊成员昵称变化（C API → C++ 转发）
void ToxCoreWrapper::OnConferencePeerNameCb_(Tox *, uint32_t conference_number,
                                             uint32_t peer_number,
                                             uint8_t const *name, size_t length,
                                             void *opaque) {
  auto *self = static_cast<ToxCoreWrapper *>(opaque);
  if (!self || !self->onConferencePeerName_) {
    return;
  }
  std::string const n(reinterpret_cast<char const *>(name), length);
  self->onConferencePeerName_(conference_number, peer_number, n);
}

// 静态回调：群聊成员列表变化（C API → C++ 转发）
void ToxCoreWrapper::OnConferencePeerListChangedCb_(Tox *,
                                                    uint32_t conference_number,
                                                    void *opaque) {
  auto *self = static_cast<ToxCoreWrapper *>(opaque);
  if (!self || !self->onConferencePeerListChanged_) {
    return;
  }
  self->onConferencePeerListChanged_(conference_number);
}

// 设置群聊邀请回调
void ToxCoreWrapper::SetOnConferenceInvite(ConferenceInviteCb cb) {
  onConferenceInvite_ = std::move(cb);
  RefreshCallbacks_();
}

// 设置群聊连接成功回调
void ToxCoreWrapper::SetOnConferenceConnected(ConferenceConnectedCb cb) {
  onConferenceConnected_ = std::move(cb);
  RefreshCallbacks_();
}

// 设置群聊消息接收回调
void ToxCoreWrapper::SetOnConferenceMessage(ConferenceMessageCb cb) {
  onConferenceMessage_ = std::move(cb);
  RefreshCallbacks_();
}

// 设置群聊标题变化回调
void ToxCoreWrapper::SetOnConferenceTitle(ConferenceTitleCb cb) {
  onConferenceTitle_ = std::move(cb);
  RefreshCallbacks_();
}

// 设置群聊成员昵称变化回调
void ToxCoreWrapper::SetOnConferencePeerName(ConferencePeerNameCb cb) {
  onConferencePeerName_ = std::move(cb);
  RefreshCallbacks_();
}

// 设置群聊成员列表变化回调
void ToxCoreWrapper::SetOnConferencePeerListChanged(
    ConferencePeerListChangedCb cb) {
  onConferencePeerListChanged_ = std::move(cb);
  RefreshCallbacks_();
}

// 创建新的群聊
uint32_t ToxCoreWrapper::CreateConference() {
  if (!tox_) {
    return UINT32_MAX;
  }
  TOX_ERR_CONFERENCE_NEW error;
  uint32_t conferenceNumber = tox_conference_new(tox_, &error);
  if (error != TOX_ERR_CONFERENCE_NEW_OK) {
    return UINT32_MAX;
  }
  return conferenceNumber;
}

// 删除群聊（退出群聊）
bool ToxCoreWrapper::DeleteConference(uint32_t conferenceNumber) {
  if (!tox_) {
    return false;
  }
  TOX_ERR_CONFERENCE_DELETE error;
  bool result = tox_conference_delete(tox_, conferenceNumber, &error);
  return result && (error == TOX_ERR_CONFERENCE_DELETE_OK);
}

// 邀请好友加入群聊
bool ToxCoreWrapper::InviteFriendToConference(uint32_t friendNumber,
                                              uint32_t conferenceNumber) {
  if (!tox_) {
    return false;
  }
  TOX_ERR_CONFERENCE_INVITE error;
  bool result =
      tox_conference_invite(tox_, friendNumber, conferenceNumber, &error);
  return result && (error == TOX_ERR_CONFERENCE_INVITE_OK);
}

// 加入好友邀请的群聊
bool ToxCoreWrapper::JoinConference(uint32_t friendNumber,
                                    std::vector<uint8_t> const &cookie) {
  if (!tox_ || cookie.empty()) {
    return false;
  }
  TOX_ERR_CONFERENCE_JOIN error;
  uint32_t conferenceNumber = tox_conference_join(
      tox_, friendNumber, cookie.data(), cookie.size(), &error);
  return (error == TOX_ERR_CONFERENCE_JOIN_OK) &&
         (conferenceNumber != UINT32_MAX);
}

// 获取群聊的唯一 ID（用于持久化）
std::vector<uint8_t> ToxCoreWrapper::GetConferenceId(
    uint32_t conferenceNumber) const {
  if (!tox_) {
    return {};
  }
  std::vector<uint8_t> id(TOX_CONFERENCE_ID_SIZE);
  if (!tox_conference_get_id(tox_, conferenceNumber, id.data())) {
    return {};
  }
  return id;
}

// 通过 Conference ID 查找群聊编号
std::optional<uint32_t> ToxCoreWrapper::GetConferenceByIdHex(
    std::string const &idHex) const {
  if (!tox_ || idHex.size() != TOX_CONFERENCE_ID_SIZE * 2) {
    return std::nullopt;
  }
  std::vector<uint8_t> id;
  if (!DecodeHex(idHex, id) || id.size() != TOX_CONFERENCE_ID_SIZE) {
    return std::nullopt;
  }

  TOX_ERR_CONFERENCE_BY_ID error;
  uint32_t conferenceNumber = tox_conference_by_id(tox_, id.data(), &error);
  if (error != TOX_ERR_CONFERENCE_BY_ID_OK) {
    return std::nullopt;
  }
  return conferenceNumber;
}

// 获取所有群聊列表
std::vector<uint32_t> ToxCoreWrapper::GetConferenceList() const {
  if (!tox_) {
    return {};
  }
  size_t count = tox_conference_get_chatlist_size(tox_);
  if (count == 0) {
    return {};
  }
  std::vector<uint32_t> list(count);
  tox_conference_get_chatlist(tox_, list.data());
  return list;
}

// 设置群聊标题（群名）
bool ToxCoreWrapper::SetConferenceTitle(uint32_t conferenceNumber,
                                        std::string const &title) {
  if (!tox_) {
    return false;
  }
  TOX_ERR_CONFERENCE_TITLE error;
  bool result = tox_conference_set_title(
      tox_, conferenceNumber, reinterpret_cast<uint8_t const *>(title.c_str()),
      title.size(), &error);
  return result && (error == TOX_ERR_CONFERENCE_TITLE_OK);
}

// 获取群聊标题
std::string ToxCoreWrapper::GetConferenceTitle(
    uint32_t conferenceNumber) const {
  if (!tox_) {
    return {};
  }
  TOX_ERR_CONFERENCE_TITLE error;
  size_t titleSize =
      tox_conference_get_title_size(tox_, conferenceNumber, &error);
  if (error != TOX_ERR_CONFERENCE_TITLE_OK || titleSize == 0) {
    return {};
  }

  std::vector<uint8_t> title(titleSize);
  if (!tox_conference_get_title(tox_, conferenceNumber, title.data(), &error)) {
    return {};
  }
  return std::string(reinterpret_cast<char const *>(title.data()), titleSize);
}

// 获取群聊成员数量
uint32_t ToxCoreWrapper::GetConferencePeerCount(
    uint32_t conferenceNumber) const {
  if (!tox_) {
    return 0;
  }
  TOX_ERR_CONFERENCE_PEER_QUERY error;
  uint32_t count = tox_conference_peer_count(tox_, conferenceNumber, &error);
  if (error != TOX_ERR_CONFERENCE_PEER_QUERY_OK) {
    return 0;
  }
  return count;
}

// 获取群聊成员昵称
std::string ToxCoreWrapper::GetConferencePeerName(uint32_t conferenceNumber,
                                                  uint32_t peerNumber) const {
  if (!tox_) {
    return {};
  }
  TOX_ERR_CONFERENCE_PEER_QUERY error;
  size_t nameSize = tox_conference_peer_get_name_size(tox_, conferenceNumber,
                                                      peerNumber, &error);
  if (error != TOX_ERR_CONFERENCE_PEER_QUERY_OK || nameSize == 0) {
    return {};
  }

  std::vector<uint8_t> name(nameSize);
  if (!tox_conference_peer_get_name(tox_, conferenceNumber, peerNumber,
                                    name.data(), &error)) {
    return {};
  }
  return std::string(reinterpret_cast<char const *>(name.data()), nameSize);
}

// 获取群聊成员公钥（hex 格式）
std::string ToxCoreWrapper::GetConferencePeerPublicKeyHex(
    uint32_t conferenceNumber, uint32_t peerNumber) const {
  if (!tox_) {
    return {};
  }
  uint8_t pubKey[TOX_PUBLIC_KEY_SIZE] = {0};
  TOX_ERR_CONFERENCE_PEER_QUERY error;
  if (!tox_conference_peer_get_public_key(tox_, conferenceNumber, peerNumber,
                                          pubKey, &error)) {
    return {};
  }
  return EncodeHexUpper(pubKey, TOX_PUBLIC_KEY_SIZE);
}

// 发送群聊消息
bool ToxCoreWrapper::SendConferenceMessage(uint32_t conferenceNumber,
                                           std::string const &message) {
  if (!tox_) {
    return false;
  }
  TOX_ERR_CONFERENCE_SEND_MESSAGE error;
  bool result = tox_conference_send_message(
      tox_, conferenceNumber, TOX_MESSAGE_TYPE_NORMAL,
      reinterpret_cast<uint8_t const *>(message.c_str()), message.size(),
      &error);
  return result && (error == TOX_ERR_CONFERENCE_SEND_MESSAGE_OK);
}

// 析构函数：释放 Tox 和 ToxAV 资源
ToxCoreWrapper::~ToxCoreWrapper() {
  if (av_) {
    toxav_kill(av_);
    av_ = nullptr;
  }
  if (tox_) {
    tox_kill(tox_);
    tox_ = nullptr;
    std::cout << "Destruction Successful\n";
  }
}

}  // namespace ToxCore
