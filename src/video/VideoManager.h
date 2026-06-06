#pragma once

#include <QObject>
#include <QPointer>
#include <QtMultimedia/QCamera>
#include <QtMultimedia/QMediaCaptureSession>
#include <QtMultimedia/QVideoFrame>
#include <QtMultimedia/QVideoSink>
#include <cstdint>
#include <memory>
#include <vector>

class VideoManager final : public QObject {
  Q_OBJECT

  public:
  explicit VideoManager(QObject *parent = nullptr);
  ~VideoManager() override;

  bool StartCamera();
  void StopCamera();
  bool GetFrame(uint16_t &width, uint16_t &height, uint8_t *&y, uint8_t *&u,
                uint8_t *&v);
  void SetLocalPreviewSink(QObject *sinkObject);
  void SetRemoteVideoSink(QObject *sinkObject);
  void SetSendTargetSize(int width, int height, bool keepAspect = true);
  void RenderRemoteFrame(uint16_t width, uint16_t height, uint8_t const *y,
                         uint8_t const *u, uint8_t const *v);

  private slots:
  void OnVideoFrameChanged(QVideoFrame const &frame);

  private:
  std::unique_ptr<QCamera> camera_;
  std::unique_ptr<QMediaCaptureSession> captureSession_;
  QPointer<QVideoSink> localSink_;
  QPointer<QVideoSink> remoteSink_;
  QMetaObject::Connection localSinkConnection_;
  QVideoFrame currentFrame_;

  int targetWidth_{0};
  int targetHeight_{0};
  bool keepAspect_{true};

  std::vector<uint8_t> yBuffer_;
  std::vector<uint8_t> uBuffer_;
  std::vector<uint8_t> vBuffer_;
};
