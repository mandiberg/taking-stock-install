#pragma once

#include "BinSorter.h"
#include "ofVideoPlayer.h"
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <utility>

struct VideoSlot {
    ofVideoPlayer player;      // currently displaying
    ofVideoPlayer nextPlayer;  // preloaded for seamless swap (only used when !videoLoop)
    std::string path;
    std::string nextPath;      // path of preloaded next video
    int x, y, w, h;
    int ratioW, ratioH;
    bool hasVideo = false;
    std::string clusterNo;     // from CSV cluster_no column; used for audio file lookup
    float duration = 0.f;      // from CSV duration column (seconds); used for key video timing
};

class BinSorterRenderer {
public:
    void setup(BinSorter* sorter, class VideoAssetPool* pool, bool videoLoop = false,
               bool keyVideo = false, float keyVideoMinLength = 0.f);
    void update();
    void draw(float offsetX = 0, float offsetY = 0);
    void drawToFbo(ofFbo& fbo);
    void regenerate();
    void preloadFromArrangement(const Arrangement& arr);
    void swapToPreloaded(const Arrangement& arr);
    void startPlaying();
    std::vector<VideoSlot>& getSlots() { return slots; }
    const std::vector<VideoSlot>& getSlots() const { return slots; }
    bool hasPreloadedLayout() const { return !nextSlots.empty(); }
    bool isPreloadComplete() const { return nextSlots.empty() || nextSlotsLoadQueue.empty(); }
    float getKeyVideoDuration(float minLength) const;  // returns longest slot duration >= minLength, or -1 if none
    int getKeyVideoSlotIndex(float minLength) const;   // returns index of key video slot, or -1 if none
private:
    void buildSlots();
    void buildSlotsFromArrangement(const std::vector<std::vector<BinItem>>& bins,
        const std::map<std::pair<int, int>, NestedBinData>& nestedBins,
        std::vector<VideoSlot>& out);

    BinSorter* binSorter = nullptr;
    VideoAssetPool* videoPool = nullptr;
    bool videoLoop = false;
    bool keyVideo = false;
    float keyVideoMinLength = 0.f;
    std::vector<VideoSlot> slots;
    std::vector<VideoSlot> nextSlots;
    std::deque<std::pair<size_t, bool>> nextSlotsLoadQueue;  // (slotIndex, isNextPlayer) - load one every 0.5s
    float lastNextSlotLoadTime = 0.f;
    std::vector<VideoSlot> slotsPendingDeletion;  // defer destruction to avoid Bus error when freeing GPU resources
    bool didSwapThisFrame = false;  // skip clearing pending in same frame as swap
    std::set<std::string> loggedNotReadyKeys;
    std::vector<size_t> cachedDrawOrder;
    bool drawOrderDirty = true;  // recompute draw order only when slots change
};
