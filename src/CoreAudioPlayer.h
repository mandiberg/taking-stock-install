#pragma once
#include <string>
#include <vector>

struct CoreAudioPlayerDiagnostics {
    bool engineLive = false;
    unsigned long long loadCalls = 0;
    unsigned long long enginesCreated = 0;
    unsigned long long enginesReleased = 0;
    unsigned long long sourceBufferBytes = 0;
    unsigned long long retainedBufferBytes = 0;
};

class CoreAudioPlayer {
public:
    CoreAudioPlayer();
    ~CoreAudioPlayer();

    // Configuration — call these before load(); values persist across load() calls.
    void setSurroundEnabled(bool surround);                      // true = device channel count, no stereo downmix; false = native file format
    void setChannelMap(const std::vector<int>& map);             // source channel index for each output channel
    void setChannelGains(const std::vector<float>& gains);       // per-output-channel gain multiplier (0.0–1.0)
    void setOutputDeviceName(const std::string& name);           // "" = macOS system default

    bool load(const std::string& path);
    void play();
    void stop();
    void setLoop(bool loop);
    void setVolume(float volume);  // 0.0 - 1.0
    bool isPlaying();
    CoreAudioPlayerDiagnostics getDiagnostics() const;

private:
    void releaseResources();
    struct Impl;
    Impl* impl;
};
