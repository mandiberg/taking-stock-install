#pragma once

#include <array>
#include <string>
#include <vector>
#include "BinSorter.h"

inline constexpr const char* kConfigPath = "../../config/config.txt";  // relative to bin/data

enum class TransitionType { Jumpcut, Fade, JumpcutToBlack };
enum class WeightNormalization { Raw, Sqrt, Equal };
enum class OutputMode { Window, Syphon };

struct SelectOption {
    std::vector<std::string> objects;  // ["*"] = any object; empty with matchEmptyList=false = wildcard
    bool matchEmptyList = false;       // when true, select only videos whose object list is []
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
    // Hard-coded settings (no longer read from config.txt; see config/configREADME.md "HARD-CODED SETTINGS")
    static constexpr bool videoLoop = true;                       // videos loop instead of swapping when finished
    static constexpr TransitionType transitionType = TransitionType::Fade;
    static constexpr float transitionDurationJumpToBlack = 0.5f;  // unused while transitionType is Fade
    static constexpr float transitionTimerMin = 5.f;              // fallback hold range when no key video qualifies
    static constexpr float transitionTimerMax = 13.f;
    static constexpr bool keyVideo = true;                        // transition fires when the longest qualifying video ends
    static constexpr bool selectExactMatch = false;               // any overlap between CSV objects and SELECT list passes
    static constexpr int gapFilterThreshold = 0;                  // only perfect-fill layouts are accepted
    static constexpr bool aspectExpandFilter = true;              // reject layouts whose slots exceed expand tolerances
    static constexpr int nestingLayers = 0;                       // nesting disabled
    static constexpr int nestedMinSpaceThreshold = 0;
    static constexpr float mainBinFillChance = 0.05f;             // chance the first item may fill the whole canvas
    static constexpr float breakBoxCoverageThreshold = 0.99f;     // fraction of a broken slot the sub-items must cover
    static constexpr int windowX = 0;                             // main window opens at the primary display's top-left
    static constexpr int windowY = 0;

    float cycleResetDuration = 5.f;  // seconds to hold black when a cycle reset fires (0 = no hold even if count reached)
    int cycleResetCount = 0;         // trigger a cycle reset every N arrangements shown (0 = disabled)
    float transitionDurationFade = 1.f;  // VIDEO_FADE_DURATION: seconds for each half of the visual fade
    std::vector<SizeRatio> sizeRatios;
    int packingStopArea = 1000;     // stop placing when largest placeable item would be < this (px²); prevents infinite tiny items
    float itemBreakScale = 0.45f;
    float itemBreakChance = 0.95f;
    int breakBoxMinItems = 1;
    int breakBoxMaxItems = 4;
    int breakBoxFillAttempts = 5;
    int layoutMaxAttempts = 50000;      // max sort() calls per phase before giving up
    int layoutStaleThreshold = 1500;   // stop phase after this many consecutive duplicates
    int layoutPhases = 5;               // number of reseeded phases to explore different regions
    int maxItems = 0;                   // reject layouts with more total items than this (0 = no limit)
    int heavyLayoutItems = 6;           // a layout with more items than this is "heavy" (0 = rule disabled)
    int afterHeavyMaxItems = 5;         // after a heavy layout, the next must have at most this many items (0 = rule disabled)
    std::vector<ExpandRange> expandRanges;                        // per-ratio-range directional expand rules (first match wins)
    std::array<float, 4> expandFallback = {0.1f, 0.1f, 0.1f, 0.1f};  // [top, right, bottom, left] used when no range matches
    float placementAreaExponent = 1.2f;  // score = area^exp * weight; >1 favors larger items
    int placementTopK = 3;              // randomly pick from top K candidates for variation (1=always best)
    WeightNormalization weightNormalization = WeightNormalization::Sqrt;  // how to normalize per-ratio video counts into placement weights
    bool selectMode = false;             // when true, filter videos by CSV object column per SELECT lines
    std::vector<SelectOption> selectOptions;
    float keyVideoMinLength = 0.f;       // minimum seconds for a video to qualify as the key video
    std::string audioPath = "";          // path to audio directory (files matched by cluster_no substring)
    float audioFadeDuration = 1.f;       // seconds for audio fade in/out (0 = instant cut)
    bool audioSurround = false;          // when true, route audio to quad output (4-channel)
    std::vector<int> audioChannelMap;    // source channel index for each output channel; empty = pass-through
    std::vector<float> audioChannelGains; // per-output-channel gain multiplier; empty = all 1.0
    std::string audioDevice = "";        // target output device name (empty = macOS system default)
    float minVideoLength = 0.f;          // discard videos shorter than this many seconds (0 = keep all)
    bool scaleSelectEnabled = false;     // when true, pick the smallest scale variant that covers the slot dimensions
    bool secondaryWindowEnabled = false; // when true, open a secondary info window
    int  secondaryWindowWidth   = 400;   // width of secondary window in pixels
    int  secondaryWindowHeight  = 300;   // height of secondary window in pixels
    bool ignoreFingerprint = false;      // when true, skip fingerprint check and reuse any matching arrangement file
    bool windowDecorated = false;        // when false, window has no title bar or borders (recommended for installation)
    OutputMode outputMode = OutputMode::Window;  // window = span displays; syphon = FBO + Syphon + preview
    std::string syphonName = "Taking Stock";     // Syphon server name visible to QLab
    int  previewWidth = 400;             // OF preview window width when outputMode is Syphon
    int  previewHeight = 400;            // OF preview window height when outputMode is Syphon
};

class ConfigLoader {
public:
    static bool load(const std::string& path, BinSorterConfig& out);
private:
    static std::string trim(const std::string& s);
    static void parseLine(const std::string& line, BinSorterConfig& config);
};
