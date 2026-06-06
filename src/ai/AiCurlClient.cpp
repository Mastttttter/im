#include "ai/AiCurlClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

#include <curl/curl.h>

#include <mutex>

namespace {
void EnsureCurlInitialized() {
  static std::once_flag flag;
  std::call_once(flag, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}
}  // namespace

// ==================== 构造 / 析构 ====================

AiCurlClient::AiCurlClient(QObject *parent) : QObject(parent) {
  EnsureCurlInitialized();
}

AiCurlClient::~AiCurlClient() {
  if (requestThread_) {
    requestThread_->requestInterruption();
    requestThread_->wait();
  }
}

// ==================== 公开接口 ====================

void AiCurlClient::SetBaseUrl(QString const &url) {
  QString const trimmed = url.trimmed();
  baseUrl_ = trimmed.endsWith(QChar('/')) ? trimmed.chopped(1) : trimmed;
}

void AiCurlClient::SetModelName(QString const &model) {
  modelName_ = model.trimmed();
}

void AiCurlClient::SetApiKey(QString const &key) {
  apiKey_ = key.trimmed();
}

void AiCurlClient::SetProvider(QString const &provider) {
  provider_ = provider.trimmed();
}

void AiCurlClient::SetTemperature(double temp) {
  temperature_ = temp;
}

void AiCurlClient::SetMaxTokens(int tokens) {
  maxTokens_ = tokens;
}

void AiCurlClient::SendAiMessage(QString const &userText,
                                  QVector<AiChatMessage> const &history) {
  if (busy_) {
    return;
  }
  busy_ = true;

  QByteArray const jsonBody = BuildRequestJson_(userText, history);
  QString const url = baseUrl_ + QStringLiteral("/chat/completions");
  QString const key = apiKey_;
  QPointer<AiCurlClient> self(this);

  auto *thread = QThread::create([self, jsonBody, url, key]() {
    auto [ok, text] = PerformRequest_(jsonBody, url, key);
    if (!self) {
      return;
    }
    QMetaObject::invokeMethod(
        self,
        [self, ok, text]() {
          if (!self) {
            return;
          }
          self->busy_ = false;
          self->requestThread_ = nullptr;
          if (ok) {
            emit self->ReplyReady(text);
          } else {
            emit self->ErrorOccurred(text);
          }
        },
        Qt::QueuedConnection);
  });
  requestThread_ = thread;
  connect(thread, &QThread::finished, thread, &QThread::deleteLater);
  thread->start();
}

// ==================== libcurl 回调 ====================

size_t AiCurlClient::CurlWriteCallback_(char *ptr, size_t size, size_t nmemb,
                                        void *userdata) {
  auto *buffer = static_cast<QByteArray *>(userdata);
  buffer->append(ptr, static_cast<qsizetype>(size * nmemb));
  return size * nmemb;
}

// ==================== 请求构建 ====================

QByteArray AiCurlClient::BuildRequestJson_(QString const &userText,
                                             QVector<AiChatMessage> const &history) const {
  QJsonObject sysMsg;
  sysMsg[QStringLiteral("role")] = QStringLiteral("system");
  sysMsg[QStringLiteral("content")] = QStringLiteral("You are a helpful assistant.");

  QJsonArray messages;
  messages.append(sysMsg);

  for (AiChatMessage const &item: history) {
    QString const role = item.role.trimmed();
    QString const content = item.content.trimmed();
    if (content.isEmpty() ||
        (role != QStringLiteral("user") && role != QStringLiteral("assistant"))) {
      continue;
    }
    QJsonObject msg;
    msg[QStringLiteral("role")] = role;
    msg[QStringLiteral("content")] = content;
    messages.append(msg);
  }

  QJsonObject userMsg;
  userMsg[QStringLiteral("role")] = QStringLiteral("user");
  userMsg[QStringLiteral("content")] = userText;
  messages.append(userMsg);

  QJsonObject root;
  root[QStringLiteral("model")] = modelName_;
  root[QStringLiteral("messages")] = messages;
  root[QStringLiteral("stream")] = false;
  root[QStringLiteral("temperature")] = temperature_;
  root[QStringLiteral("max_tokens")] = maxTokens_;

  bool const usesTokenponyExtensions =
      baseUrl_.contains(QStringLiteral("tokenpony"), Qt::CaseInsensitive);
  if (usesTokenponyExtensions && !provider_.isEmpty()) {
    root[QStringLiteral("provider")] = provider_;
  }

  if (usesTokenponyExtensions) {
    QJsonObject chatTemplateKwargs;
    chatTemplateKwargs[QStringLiteral("enable_thinking")] = false;
    root[QStringLiteral("chat_template_kwargs")] = chatTemplateKwargs;
  }

  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// ==================== 网络请求（在后台线程执行） ====================

QPair<bool, QString> AiCurlClient::PerformRequest_(QByteArray const &jsonBody,
                                                   QString const &apiUrl,
                                                   QString const &apiKey) {
  CURL *curl = curl_easy_init();
  if (!curl) {
    return {false, QStringLiteral("无法初始化网络请求")};
  }

  QByteArray responseBuffer;
  QByteArray const urlBytes = apiUrl.toUtf8();

  curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  QByteArray const authHeader = "Authorization: Bearer " + apiKey.toUtf8();
  headers = curl_slist_append(headers, authHeader.constData());

  curl_easy_setopt(curl, CURLOPT_URL, urlBytes.constData());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.constData());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(jsonBody.size()));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback_);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

  CURLcode const res = curl_easy_perform(curl);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    if (res == CURLE_OPERATION_TIMEDOUT) {
      return {false, QStringLiteral("AI 请求超时，请稍后再试")};
    }
    return {false, QStringLiteral("网络请求失败: %1")
                       .arg(QString::fromUtf8(curl_easy_strerror(res)))};
  }

  QString const aiText = ExtractAiText_(responseBuffer);
  if (aiText.isEmpty()) {
    QString const preview = QString::fromUtf8(responseBuffer).left(300);
    return {false, QStringLiteral("AI 返回内容为空或解析失败: %1").arg(preview)};
  }

  if (aiText.startsWith(QStringLiteral("[API 错误]"))) {
    return {false, aiText};
  }

  return {true, aiText};
}

// ==================== JSON 解析 ====================

QString AiCurlClient::ExtractAiText_(QByteArray const &responseJson) {
  QJsonParseError err;
  auto const doc = QJsonDocument::fromJson(responseJson, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return {};
  }

  auto const root = doc.object();

  if (root.contains(QStringLiteral("error"))) {
    auto const errVal = root.value(QStringLiteral("error"));
    QString errDetail;
    if (errVal.isObject()) {
      auto const errObj = errVal.toObject();
      errDetail = errObj.value(QStringLiteral("message")).toString();
      if (errDetail.isEmpty()) {
        errDetail = errObj.value(QStringLiteral("msg")).toString();
      }
      if (errDetail.isEmpty()) {
        errDetail = QString::fromUtf8(
            QJsonDocument(errObj).toJson(QJsonDocument::Compact));
      }
    } else if (errVal.isString()) {
      errDetail = errVal.toString();
    } else {
      errDetail = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
    return QStringLiteral("[API 错误] %1").arg(TranslateApiError_(errDetail));
  }

  auto const choices = root.value(QStringLiteral("choices")).toArray();
  if (choices.isEmpty()) {
    return {};
  }

  auto const msg = choices.first().toObject().value(QStringLiteral("message")).toObject();
  QString const content = msg.value(QStringLiteral("content")).toString().trimmed();
  QString const reasoning = msg.value(QStringLiteral("reasoning_content")).toString().trimmed();

  if (!reasoning.isEmpty() && !content.isEmpty()) {
    return QStringLiteral("**[思考过程]**\n%1\n\n**[回答]**\n%2")
        .arg(reasoning, content);
  }
  if (!reasoning.isEmpty()) {
    return QStringLiteral("**[思考过程]**\n%1").arg(reasoning);
  }
  return content;
}

// ==================== 错误信息中文化 ====================

QString AiCurlClient::TranslateApiError_(QString const &rawError) {
  if (rawError.contains(QStringLiteral("insufficient balance"), Qt::CaseInsensitive)) {
    return QStringLiteral("账户余额不足，请前往充值");
  }

  if (rawError.contains(QStringLiteral("invalid api key"), Qt::CaseInsensitive) ||
      rawError.contains(QStringLiteral("authentication"), Qt::CaseInsensitive) ||
      rawError.contains(QStringLiteral("unauthorized"), Qt::CaseInsensitive)) {
    return QStringLiteral("API Key 无效或已过期，请检查后重新设置");
  }

  if (rawError.contains(QStringLiteral("rate limit"), Qt::CaseInsensitive)) {
    return QStringLiteral("请求过于频繁，请稍后再试");
  }

  if (rawError.contains(QStringLiteral("model"), Qt::CaseInsensitive) &&
      rawError.contains(QStringLiteral("not found"), Qt::CaseInsensitive)) {
    return QStringLiteral("所选模型不存在或暂不可用，请确认模型名称");
  }

  if (rawError.contains(QStringLiteral("context length"), Qt::CaseInsensitive) ||
      rawError.contains(QStringLiteral("too long"), Qt::CaseInsensitive)) {
    return QStringLiteral("消息内容过长，超出模型上下文限制");
  }

  if (rawError.contains(QStringLiteral("internal"), Qt::CaseInsensitive) ||
      (rawError.contains(QStringLiteral("server"), Qt::CaseInsensitive) &&
       rawError.contains(QStringLiteral("error"), Qt::CaseInsensitive))) {
    return QStringLiteral("服务器内部错误，请稍后再试");
  }

  return rawError;
}
