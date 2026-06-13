#pragma once
#include <string>

class CoreAudioPlayer {
public:
    CoreAudioPlayer();
    ~CoreAudioPlayer();
    
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