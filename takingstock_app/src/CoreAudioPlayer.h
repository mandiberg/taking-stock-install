#pragma once
#include <string>
#include <vector>

class CoreAudioPlayer {
public:
    CoreAudioPlayer();
    ~CoreAudioPlayer();

    // Configuration — call these before load(); values persist across load() calls.
    void setSurroundEnabled(bool surround);                      // true = 5.1 output; false = stereo
    void setChannelMap(const std::vector<int>& map);             // source channel index for each output channel
    void setChannelGains(const std::vector<float>& gains);       // per-output-channel gain multiplier (0.0–1.0)
    void setOutputDeviceName(const std::string& name);           // "" = macOS system default

    bool load(const std::string& path);
    void play();
    void stop();
    void setLoop(bool loop);
    void setVolume(float volume);  // 0.0 - 1.0
    bool isPlaying();

private:
    struct Impl;
    Impl* impl;
};
