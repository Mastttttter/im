#include "video/VideoManager.h"

#include <QImage>
#include <QtMultimedia/QCameraDevice>
#include <QtMultimedia/QMediaDevices>
#include <algorithm>

namespace {

uint8_t ClampColor(int value) {
  return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

void YuvToRgb(uint8_t y, uint8_t u, uint8_t v, uint8_t &r, uint8_t &g,
              uint8_t &b) {
  int const c = static_cast<int>(y) - 16;
  int const d = static_cast<int>(u) - 128;
  int const e = static_cast<int>(v) - 128;
  r = ClampColor((298 * c + 409 * e + 128) >> 8);
  g = ClampColor((298 * c - 100 * d - 208 * e + 128) >> 8);
  b = ClampColor((298 * c + 516 * d + 128) >> 8);
}

}  // namespace

VideoManager::VideoManager(QObject *parent) : QObject(parent) {
  captureSession_ = std::make_unique<QMediaCaptureSession>();

  QList<QCameraDevice> const cameras = QMediaDevices::videoInputs();
  if (cameras.isEmpty()) {
    return;
  }

  camera_ = std::make_unique<QCamera>(cameras.first());
  captureSession_->setCamera(camera_.get());
}

VideoManager::~VideoManager() { StopCamera(); }

bool VideoManager::StartCamera() {
  if (!camera_) {
    return false;
  }
  if (camera_->isActive()) {
    return true;
  }
  camera_->start();
  return true;
}

void VideoManager::StopCamera() {
  if (camera_ && camera_->isActive()) {
    camera_->stop();
  }
  currentFrame_ = {};
}

void VideoManager::SetLocalPreviewSink(QObject *sinkObject) {
  if (localSinkConnection_) {
    QObject::disconnect(localSinkConnection_);
    localSinkConnection_ = {};
  }

  auto *sink = qobject_cast<QVideoSink *>(sinkObject);
  localSink_ = sink;
  if (!captureSession_) {
    return;
  }

  captureSession_->setVideoSink(sink);
  if (!sink) {
    currentFrame_ = {};
    return;
  }

  localSinkConnection_ = connect(sink, &QVideoSink::videoFrameChanged, this,
                                 &VideoManager::OnVideoFrameChanged);
}

void VideoManager::SetRemoteVideoSink(QObject *sinkObject) {
  remoteSink_ = qobject_cast<QVideoSink *>(sinkObject);
}

void VideoManager::SetSendTargetSize(int width, int height, bool keepAspect) {
  targetWidth_ = width < 0 ? 0 : width;
  targetHeight_ = height < 0 ? 0 : height;
  keepAspect_ = keepAspect;
}

void VideoManager::OnVideoFrameChanged(QVideoFrame const &frame) {
  if (frame.isValid()) {
    currentFrame_ = frame;
  }
}

bool VideoManager::GetFrame(uint16_t &width, uint16_t &height, uint8_t *&y,
                            uint8_t *&u, uint8_t *&v) {
  if (!currentFrame_.isValid()) {
    return false;
  }

  QVideoFrame frame = currentFrame_;
  if (!frame.map(QVideoFrame::ReadOnly)) {
    return false;
  }

  QImage image = frame.toImage();
  frame.unmap();
  if (image.isNull()) {
    return false;
  }

  image = image.convertToFormat(QImage::Format_RGB888);
  if (targetWidth_ > 0 && targetHeight_ > 0) {
    image = image.scaled(targetWidth_, targetHeight_,
                         keepAspect_ ? Qt::KeepAspectRatio
                                     : Qt::IgnoreAspectRatio,
                         Qt::SmoothTransformation);
  }

  int w = image.width();
  int h = image.height();
  if (w <= 0 || h <= 0) {
    return false;
  }
  if (w % 2 != 0) {
    --w;
  }
  if (h % 2 != 0) {
    --h;
  }
  if (w <= 0 || h <= 0) {
    return false;
  }
  if (w != image.width() || h != image.height()) {
    image = image.copy(0, 0, w, h);
  }

  width = static_cast<uint16_t>(w);
  height = static_cast<uint16_t>(h);
  size_t const ySize = static_cast<size_t>(width) * height;
  size_t const uvSize = static_cast<size_t>(width / 2) * (height / 2);
  yBuffer_.resize(ySize);
  uBuffer_.resize(uvSize);
  vBuffer_.resize(uvSize);

  uchar const *rgb = image.constBits();
  int const stride = image.bytesPerLine();
  for (int row = 0; row < height; ++row) {
    for (int col = 0; col < width; ++col) {
      int const rgbIndex = row * stride + col * 3;
      int const r = rgb[rgbIndex];
      int const g = rgb[rgbIndex + 1];
      int const b = rgb[rgbIndex + 2];

      int const yValue = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
      yBuffer_[static_cast<size_t>(row) * width + col] = ClampColor(yValue);

      if (row % 2 == 0 && col % 2 == 0) {
        int const uValue = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
        int const vValue = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
        size_t const uvIndex = static_cast<size_t>(row / 2) * (width / 2) +
                               static_cast<size_t>(col / 2);
        uBuffer_[uvIndex] = ClampColor(uValue);
        vBuffer_[uvIndex] = ClampColor(vValue);
      }
    }
  }

  y = yBuffer_.data();
  u = uBuffer_.data();
  v = vBuffer_.data();
  return true;
}

void VideoManager::RenderRemoteFrame(uint16_t width, uint16_t height,
                                     uint8_t const *y, uint8_t const *u,
                                     uint8_t const *v) {
  if (!remoteSink_ || !y || !u || !v || width == 0 || height == 0) {
    return;
  }

  QImage image(width, height, QImage::Format_RGB888);
  if (image.isNull()) {
    return;
  }

  for (int row = 0; row < height; ++row) {
    auto *line = image.scanLine(row);
    for (int col = 0; col < width; ++col) {
      size_t const yIndex = static_cast<size_t>(row) * width + col;
      size_t const uvIndex = static_cast<size_t>(row / 2) * (width / 2) +
                             static_cast<size_t>(col / 2);
      uint8_t r = 0;
      uint8_t g = 0;
      uint8_t b = 0;
      YuvToRgb(y[yIndex], u[uvIndex], v[uvIndex], r, g, b);
      int const rgbIndex = col * 3;
      line[rgbIndex] = r;
      line[rgbIndex + 1] = g;
      line[rgbIndex + 2] = b;
    }
  }

  remoteSink_->setVideoFrame(QVideoFrame(image));
}
