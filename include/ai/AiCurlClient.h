#pragma once

#include <QByteArray>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QThread>
#include <QVector>

/**
 * @brief AI 大模型客户端（基于 libcurl，兼容 OpenAI API 格式）
 *
 * 可通过 SetBaseUrl / SetModelName / SetApiKey 配置不同厂商的 API。
 * 默认指向小马算力（tokenpony），模型为 qwen3-8b。
 *
 * 使用方式：
 *   1. 按需配置 BaseUrl / ModelName / ApiKey
 *   2. SendAiMessage() 发送用户消息
 *   3. 连接 ReplyReady / ErrorOccurred 信号接收结果
 */
struct AiChatMessage {
  QString role;
  QString content;
};

class AiCurlClient : public QObject {
  Q_OBJECT

  public:
  explicit AiCurlClient(QObject *parent = nullptr);
  ~AiCurlClient() override;

  void SetBaseUrl(QString const &url);
  void SetModelName(QString const &model);
  void SetApiKey(QString const &key);
  void SetProvider(QString const &provider);
  void SetTemperature(double temp);
  void SetMaxTokens(int tokens);

  QString BaseUrl() const { return baseUrl_; }
  QString ModelName() const { return modelName_; }
  QString ApiKey() const { return apiKey_; }
  QString Provider() const { return provider_; }
  double Temperature() const { return temperature_; }
  int MaxTokens() const { return maxTokens_; }
  bool IsBusy() const { return busy_; }

  void SendAiMessage(QString const &userText,
                     QVector<AiChatMessage> const &history = {});

  signals:
  void ReplyReady(QString const &aiText);
  void ErrorOccurred(QString const &errorMsg);

  private:
  QByteArray BuildRequestJson_(QString const &userText,
                               QVector<AiChatMessage> const &history) const;
  static QPair<bool, QString> PerformRequest_(QByteArray const &jsonBody,
                                              QString const &apiUrl,
                                              QString const &apiKey);
  static QString ExtractAiText_(QByteArray const &responseJson);
  static QString TranslateApiError_(QString const &rawError);
  static size_t CurlWriteCallback_(char *ptr, size_t size, size_t nmemb,
                                   void *userdata);

  QString baseUrl_{QStringLiteral("https://api.tokenpony.cn/v1")};
  QString modelName_{QStringLiteral("qwen3-8b")};
  QString apiKey_;
  QString provider_{QStringLiteral("tokenpony")};
  double temperature_{0.0};
  int maxTokens_{4096};
  bool busy_{false};
  QPointer<QThread> requestThread_;
};
