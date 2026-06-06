#include "audio/AudioManager.h"

#include <QIODevice>
#include <QStringList>
#include <QtMultimedia/QAudio>
#include <algorithm>
#include <cstddef>

namespace {

QAudioDevice SelectBestInputDevice() {
  QList<QAudioDevice> const devices = QMediaDevices::audioInputs();
  QStringList const headphoneKeywords{QStringLiteral("headphone"),
                                      QStringLiteral("headset"),
                                      QStringLiteral("earphone"),
                                      QStringLiteral("耳机"),
                                      QStringLiteral("耳麦")};

  for (QAudioDevice const &device: devices) {
    QString const description = device.description().toLower();
    for (QString const &keyword: headphoneKeywords) {
      if (description.contains(keyword, Qt::CaseInsensitive)) {
        return device;
      }
    }
  }

  return QMediaDevices::defaultAudioInput();
}

QAudioDevice SelectBestOutputDevice() {
  QList<QAudioDevice> const devices = QMediaDevices::audioOutputs();
  QStringList const headphoneKeywords{QStringLiteral("headphone"),
                                      QStringLiteral("headset"),
                                      QStringLiteral("earphone"),
                                      QStringLiteral("耳机"),
                                      QStringLiteral("耳麦")};

  for (QAudioDevice const &device: devices) {
    QString const description = device.description().toLower();
    for (QString const &keyword: headphoneKeywords) {
      if (description.contains(keyword, Qt::CaseInsensitive)) {
        return device;
      }
    }
  }

  return QMediaDevices::defaultAudioOutput();
}

}  // namespace

AudioManager::AudioManager(QObject *parent, uint32_t sampleRate, uint8_t channels)
    : QObject(parent) {
  int const sr = (sampleRate >= 8000 && sampleRate <= 48000)
                     ? static_cast<int>(sampleRate)
                     : 48000;
  int const ch = (channels == 1 || channels == 2) ? static_cast<int>(channels) : 1;

  fmt_.setSampleRate(sr);
  fmt_.setChannelCount(ch);
  fmt_.setSampleFormat(QAudioFormat::Int16);

  mediaDevices_ = std::make_unique<QMediaDevices>();
  connect(mediaDevices_.get(), &QMediaDevices::audioInputsChanged, this,
          &AudioManager::OnAudioInputsChanged);
  connect(mediaDevices_.get(), &QMediaDevices::audioOutputsChanged, this,
          &AudioManager::OnAudioOutputsChanged);
}

AudioManager::~AudioManager() { Stop(); }

void AudioManager::Start() {
  devicesStarted_ = true;
  InitDevices();
}

void AudioManager::Stop() {
  devicesStarted_ = false;
  SafeStopDevices();
}

void AudioManager::InitDevices() {
  SafeStopDevices();
  currentInputDevice_ = SelectBestInputDevice();
  currentOutputDevice_ = SelectBestOutputDevice();

  try {
    if (!currentInputDevice_.isNull()) {
      mic_ = std::make_unique<QAudioSource>(currentInputDevice_, fmt_);
      micDev_ = mic_->start();
    }
    if (!currentOutputDevice_.isNull()) {
      spk_ = std::make_unique<QAudioSink>(currentOutputDevice_, fmt_);
      spkDev_ = spk_->start();
    }
  } catch (...) {
    micDev_ = nullptr;
    spkDev_ = nullptr;
  }
}

void AudioManager::OnAudioInputsChanged() {
  if (!devicesStarted_) {
    return;
  }
  QAudioDevice newDevice = SelectBestInputDevice();
  if (newDevice.id() == currentInputDevice_.id()) {
    return;
  }

  currentInputDevice_ = newDevice;
  try {
    if (mic_) {
      mic_->stop();
    }
    micDev_ = nullptr;
    if (!currentInputDevice_.isNull()) {
      mic_ = std::make_unique<QAudioSource>(currentInputDevice_, fmt_);
      micDev_ = mic_->start();
    }
  } catch (...) {
    micDev_ = nullptr;
  }
}

void AudioManager::OnAudioOutputsChanged() {
  if (!devicesStarted_) {
    return;
  }
  QAudioDevice newDevice = SelectBestOutputDevice();
  if (newDevice.id() == currentOutputDevice_.id()) {
    return;
  }

  currentOutputDevice_ = newDevice;
  try {
    if (spk_) {
      spk_->stop();
    }
    spkDev_ = nullptr;
    if (!currentOutputDevice_.isNull()) {
      spk_ = std::make_unique<QAudioSink>(currentOutputDevice_, fmt_);
      spkDev_ = spk_->start();
    }
  } catch (...) {
    spkDev_ = nullptr;
  }
}

bool AudioManager::CheckAndRecoverInput() {
  if (!devicesStarted_) {
    return false;
  }
  try {
    if (!mic_) {
      QAudioDevice newDevice = SelectBestInputDevice();
      if (newDevice.isNull()) {
        return false;
      }
      currentInputDevice_ = newDevice;
      mic_ = std::make_unique<QAudioSource>(currentInputDevice_, fmt_);
      micDev_ = mic_->start();
      return micDev_ != nullptr;
    }

    if (mic_->state() == QAudio::StoppedState && mic_->error() != QAudio::NoError) {
      mic_->stop();
      micDev_ = mic_->start();
      if (!micDev_ || mic_->error() != QAudio::NoError) {
        QAudioDevice newDevice = SelectBestInputDevice();
        if (!newDevice.isNull() && newDevice.id() != currentInputDevice_.id()) {
          currentInputDevice_ = newDevice;
          mic_ = std::make_unique<QAudioSource>(currentInputDevice_, fmt_);
          micDev_ = mic_->start();
        }
      }
      return micDev_ != nullptr && mic_->error() == QAudio::NoError;
    }

    return true;
  } catch (...) {
    return false;
  }
}

bool AudioManager::CheckAndRecoverOutput() {
  if (!devicesStarted_) {
    return false;
  }
  try {
    if (!spk_) {
      QAudioDevice newDevice = SelectBestOutputDevice();
      if (newDevice.isNull()) {
        return false;
      }
      currentOutputDevice_ = newDevice;
      spk_ = std::make_unique<QAudioSink>(currentOutputDevice_, fmt_);
      spkDev_ = spk_->start();
      return spkDev_ != nullptr;
    }

    if (spk_->state() == QAudio::StoppedState && spk_->error() != QAudio::NoError) {
      spk_->stop();
      spkDev_ = spk_->start();
      if (!spkDev_ || spk_->error() != QAudio::NoError) {
        QAudioDevice newDevice = SelectBestOutputDevice();
        if (!newDevice.isNull() && newDevice.id() != currentOutputDevice_.id()) {
          currentOutputDevice_ = newDevice;
          spk_ = std::make_unique<QAudioSink>(currentOutputDevice_, fmt_);
          spkDev_ = spk_->start();
        }
      }
      return spkDev_ != nullptr && spk_->error() == QAudio::NoError;
    }

    return true;
  } catch (...) {
    return false;
  }
}

void AudioManager::SafeStopDevices() {
  if (mic_) {
    mic_->stop();
  }
  if (spk_) {
    spk_->stop();
  }
  micDev_ = nullptr;
  spkDev_ = nullptr;
  playbackBacklog_.clear();
}

qsizetype AudioManager::ReadCapture(uint8_t *buf, qsizetype maxBytes) {
  if (!buf || maxBytes <= 0) {
    return 0;
  }
  if (!micDev_) {
    CheckAndRecoverInput();
    if (!micDev_) {
      return 0;
    }
  }

  qsizetype const bytesRead = micDev_->read(reinterpret_cast<char *>(buf), maxBytes);
  if (bytesRead < 0) {
    CheckAndRecoverInput();
    return 0;
  }
  return bytesRead;
}

void AudioManager::Play(uint8_t const *data, qsizetype len) {
  if (!data || len <= 0) {
    return;
  }
  if (!spkDev_) {
    CheckAndRecoverOutput();
    if (!spkDev_) {
      return;
    }
  }

  playbackBacklog_.insert(playbackBacklog_.end(), data, data + len);
  constexpr size_t kMaxBacklogBytes = 384000;
  if (playbackBacklog_.size() > kMaxBacklogBytes) {
    playbackBacklog_.erase(playbackBacklog_.begin(),
                           playbackBacklog_.begin() +
                               static_cast<std::ptrdiff_t>(playbackBacklog_.size() -
                                                           kMaxBacklogBytes));
  }

  while (!playbackBacklog_.empty()) {
    qsizetype const bytesWritten = spkDev_->write(
        reinterpret_cast<char const *>(playbackBacklog_.data()),
        static_cast<qsizetype>(playbackBacklog_.size()));
    if (bytesWritten < 0) {
      CheckAndRecoverOutput();
      return;
    }
    if (bytesWritten == 0) {
      return;
    }
    playbackBacklog_.erase(playbackBacklog_.begin(),
                           playbackBacklog_.begin() + bytesWritten);
  }
}
