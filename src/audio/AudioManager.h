#pragma once

#include <QObject>
#include <QtMultimedia/QAudioDevice>
#include <QtMultimedia/QAudioFormat>
#include <QtMultimedia/QAudioSink>
#include <QtMultimedia/QAudioSource>
#include <QtMultimedia/QMediaDevices>
#include <cstdint>
#include <memory>
#include <vector>

class AudioManager final : public QObject {
  Q_OBJECT

  public:
  explicit AudioManager(QObject *parent = nullptr, uint32_t sampleRate = 48000,
                        uint8_t channels = 1);
  ~AudioManager() override;

  void Start();
  void Stop();
  qsizetype ReadCapture(uint8_t *buf, qsizetype maxBytes);
  void Play(uint8_t const *data, qsizetype len);

  private slots:
  void OnAudioInputsChanged();
  void OnAudioOutputsChanged();

  private:
  void InitDevices();
  void SafeStopDevices();
  bool CheckAndRecoverInput();
  bool CheckAndRecoverOutput();

  QAudioFormat fmt_;
  std::unique_ptr<QAudioSource> mic_;
  std::unique_ptr<QAudioSink> spk_;
  std::unique_ptr<QMediaDevices> mediaDevices_;
  QIODevice *micDev_{nullptr};
  QIODevice *spkDev_{nullptr};
  QAudioDevice currentInputDevice_;
  QAudioDevice currentOutputDevice_;
  bool devicesStarted_{false};
  std::vector<uint8_t> playbackBacklog_;
};
