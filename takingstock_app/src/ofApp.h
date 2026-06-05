#pragma once

#include "ofMain.h"
#include "BinSorter.h"
#include "VideoAssetPool.h"
#include "BinSorterRenderer.h"
#include "ConfigLoader.h"
#include <memory>

enum class TransitionState { Idle, FadeDown, FadeHoldBlack, HoldBlack, FadeUp, CycleReset };

class ofApp : public ofBaseApp {
public:
    void setup();
    void update();
    void draw();
    void keyPressed(int key);

    std::string getCurrentKeyVideoClusterNo() const;
    bool isKeyVideoEnabled() const { return config.keyVideo; }

private:
    void pickSelectAndApplyFilter();
    void pickSelectAndApplyFilterWithFallback();  // re-rolls filter if no arrangement is compatible
    void pickAndLoadArrangement(size_t idx);
    void swapToPreloadedAndLog(size_t idx, bool deferPlay = false);
    void logArrangementInfo(size_t idx);
    void preloadNextLayout();
    float scheduleNextTransition();
    size_t pickNextArrangementIndex();
    bool arrangementCompatibleWithFilter(const Arrangement& arr) const;

    std::string findAudioFile(const std::string& clusterNo) const;
    void startAudioForArrangement(float initialVolume = 1.f);
    void updateAudioFade();
    void beginAudioFade(float targetVolume);

    BinSorterConfig config;
    std::unique_ptr<BinSorter> binSorter;
    VideoAssetPool videoPool;
    BinSorterRenderer renderer;
    ofFbo exportFbo;
    bool exportRequested = false;
    std::vector<Arrangement> arrangements;
    bool arrangementPickRequested = false;
    int arrangementsShownCount = 0;  // counts arrangements displayed; triggers cycle reset at cycleResetCount

    TransitionState transitionState = TransitionState::Idle;
    float transitionStartTime = 0.f;
    float nextTransitionTime = 0.f;
    size_t nextLayoutIdx = 0;  // preloaded layout index
    bool fadeHoldBlackSwapDone = false;
    float preloadWaitStartTime = -1.f;  // time when triggered transition started waiting for preload
    bool endOfCycleResetPending = false;  // set when arrangementsShownCount reaches cycleResetCount

    ofSoundPlayer audioPlayer;
    float audioVolume = 1.f;
    float audioFadeStartTime = -1.f;
    float audioFadeStartVolume = 0.f;
    float audioFadeTargetVolume = 1.f;
    std::string currentAudioFileName;
    std::string currentSelectFilterLabel;
    std::string nextSelectFilterLabel;
};
