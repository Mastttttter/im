#include "core/CallConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace AppConfig {
namespace {

int ClampOrDefault(int value, int lo, int hi, int fallback) {
  if (value < lo || value > hi) {
    return fallback;
  }
  return value;
}

int ValidAudioFrameDurationOrDefault(int value, int fallback) {
  switch (value) {
  case 5:
  case 10:
  case 20:
  case 40:
  case 60:
    return value;
  default:
    return fallback;
  }
}

QString FindConfigPath() {
  QString const fileName = QStringLiteral("call_config.ini");
  QString const currentPath = QDir::current().filePath(fileName);
  if (QFileInfo::exists(currentPath) && QFileInfo(currentPath).isFile()) {
    return currentPath;
  }

  QString const appPath = QDir(QCoreApplication::applicationDirPath()).filePath(fileName);
  if (QFileInfo::exists(appPath) && QFileInfo(appPath).isFile()) {
    return appPath;
  }

  return {};
}

}  // namespace

AvCallConfig LoadAvCallConfig() {
  AvCallConfig config{};
  QString const path = FindConfigPath();
  if (path.isEmpty()) {
    return config;
  }

  QSettings settings(path, QSettings::IniFormat);

  settings.beginGroup(QStringLiteral("audio"));
  config.audio.bitrateKbps = ClampOrDefault(
      settings.value(QStringLiteral("bitrate_kbps"), config.audio.bitrateKbps)
          .toInt(),
      1, 512, config.audio.bitrateKbps);
  config.audio.sampleRate = ClampOrDefault(
      settings.value(QStringLiteral("sample_rate"), config.audio.sampleRate).toInt(),
      8000, 48000, config.audio.sampleRate);
  config.audio.channels = ClampOrDefault(
      settings.value(QStringLiteral("channels"), config.audio.channels).toInt(), 1,
      2, config.audio.channels);
  config.audio.frameDurationMs = ValidAudioFrameDurationOrDefault(
      settings
          .value(QStringLiteral("frame_duration_ms"),
                 config.audio.frameDurationMs)
          .toInt(),
      config.audio.frameDurationMs);
  settings.endGroup();

  settings.beginGroup(QStringLiteral("video"));
  config.video.bitrateKbps = ClampOrDefault(
      settings.value(QStringLiteral("bitrate_kbps"), config.video.bitrateKbps)
          .toInt(),
      1, 20000, config.video.bitrateKbps);
  config.video.sendFps = ClampOrDefault(
      settings.value(QStringLiteral("send_fps"), config.video.sendFps).toInt(), 0,
      60, config.video.sendFps);
  config.video.targetWidth = ClampOrDefault(
      settings.value(QStringLiteral("target_width"), config.video.targetWidth)
          .toInt(),
      0, 4096, config.video.targetWidth);
  config.video.targetHeight = ClampOrDefault(
      settings.value(QStringLiteral("target_height"), config.video.targetHeight)
          .toInt(),
      0, 4096, config.video.targetHeight);
  config.video.keepAspect =
      settings.value(QStringLiteral("keep_aspect"), config.video.keepAspect).toBool();
  settings.endGroup();

  config.loadedFrom = path;
  return config;
}

}  // namespace AppConfig
