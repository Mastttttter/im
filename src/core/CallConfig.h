#pragma once

#include <QString>

namespace AppConfig {

struct AvCallConfig {
  struct Audio {
    int bitrateKbps{48};
    int sampleRate{48000};
    int channels{1};
    int frameDurationMs{20};
  };

  struct Video {
    int bitrateKbps{5000};
    int sendFps{15};
    int targetWidth{0};
    int targetHeight{0};
    bool keepAspect{true};
  };

  Audio audio;
  Video video;
  QString loadedFrom;
};

AvCallConfig LoadAvCallConfig();

}  // namespace AppConfig
