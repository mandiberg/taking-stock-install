#pragma once

#include <array>
#include <string>
#include <vector>
#include "BinSorter.h"

enum class TransitionType { Jumpcut, Fade, JumpcutToBlack };
enum class WeightNormalization { Raw, Sqrt, Equal };

struct SelectOption {
    std::vector<std::string> objects;  // empty or ["*"] = any object
    float weight = 1.0f;
};

struct ExpandRange {
    float minRatio, maxRatio;
    float top, right, bottom, left;
};

struct BinSorterConfig {
    int boxWidth = 1920;
    int boxHeight = 1080;
    std::string videoAssetPath = "videos";
    std::string videosCsvPath = "videos/videos.csv";  // path to videos.csv (replaces folder-based loading)
    std::string arrangementsPath = "arrangements";
    bool videoLoop = false;  // when false, swap to new video when finished; when true, loop
    TransitionType transitionType = TransitionType::Jumpcut;
    float transitionDurationFade = 0.5f;
    float transitionDurationJumpToBlack = 0.5f;
    float transitionTimerMin = 30.f;
    float transitionTimerMax = 90.f;
    std::vector<SizeRatio> sizeRatios;
    int gapFilterThreshold = 1000;   // reject layouts where largest empty rect >= this (px²); 0 = only perfect fill
    bool aspectExpandFilter = true;  // reject layouts where |calc_ratio - slot_ratio| exceeds expand slack (see SIZE_RATIO expandX/Y)
    int packingStopArea = 1000;     // stop placing when largest placeable item would be < this (px²); prevents infinite tiny items
    int nestingLayers = 1;
    int nestedMinSpaceThreshold = 0;
    float mainBinFillChance = 0.05f;
    float itemBreakScale = 0.45f;
    float itemBreakChance = 0.95f;
    int breakBoxMinItems = 1;
    int breakBoxMaxItems = 4;
    int breakBoxFillAttempts = 5;
    float breakBoxCoverageThreshold = 0.99f;
    int layoutMaxAttempts = 50000;      // max sort() calls per phase before giving up
    int layoutStaleThreshold = 1500;   // stop phase after this many consecutive duplicates
    int layoutPhases = 5;               // number of reseeded phases to explore different regions
    std::vector<ExpandRange> expandRanges;                        // per-ratio-range directional expand rules (first match wins)
    std::array<float, 4> expandFallback = {0.1f, 0.1f, 0.1f, 0.1f};  // [top, right, bottom, left] used when no range matches
    float placementAreaExponent = 1.2f;  // score = area^exp * weight; >1 favors larger items
    int placementTopK = 3;              // randomly pick from top K candidates for variation (1=always best)
    WeightNormalization weightNormalization = WeightNormalization::Sqrt;  // how to normalize per-ratio video counts into placement weights
    bool selectMode = false;             // when true, filter videos by CSV object column per SELECT lines
    std::vector<SelectOption> selectOptions;
    bool keyVideo = false;               // when true, transition fires when the longest qualifying video ends
    float keyVideoMinLength = 0.f;       // minimum seconds for a video to qualify as the key video
    std::string audioPath = "";          // path to audio directory (files matched by cluster_no substring)
    float audioFadeDuration = 1.f;       // seconds for audio fade in/out (0 = instant cut)
    float minVideoLength = 0.f;          // discard videos shorter than this many seconds (0 = keep all)
    bool secondaryWindowEnabled = false; // when true, open a secondary info window
    int  secondaryWindowWidth   = 400;   // width of secondary window in pixels
    int  secondaryWindowHeight  = 300;   // height of secondary window in pixels
};

class ConfigLoader {
public:
    static bool load(const std::string& path, BinSorterConfig& out);
private:
    static std::string trim(const std::string& s);
    static void parseLine(const std::string& line, BinSorterConfig& config);
};
