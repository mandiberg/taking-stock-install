#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_set>
#include <cmath>

struct SelectOption;

struct VideoEntry {
    std::string videoId;
    std::string filename;
    float ratio;
    float duration = 0.f;          // duration in seconds from CSV duration column
    std::string object;            // raw object column string from CSV, e.g. "[67, 95]"
    std::vector<std::string> objectList;  // parsed list of object IDs, e.g. {"67", "95"}; empty = no objects
    std::string clusterNo;
    std::string poseNo;
    std::string fullPath;          // resolved path to video file
};

class VideoAssetPool {
public:
    float minDuration = 0.f;  // set before loadFromCsv; entries with known duration < this are discarded

    bool loadFromCsv(const std::string& csvPath);
    void resetUsed();  // call when starting a new layout - makes all videos available again
    void setObjectFilter(const SelectOption& opt, bool exactMatch);  // configure active filter
    VideoEntry getVideoEntry(int wr, int hr);  // picks from unused; returns full entry incl. clusterNo
    VideoEntry getVideoEntryWithMinDuration(int wr, int hr, float minDuration);  // like getVideoEntry but only picks videos with duration >= minDuration; returns empty if none available (pool unchanged)
    std::string getVideoPath(int wr, int hr);  // picks from unused; reuses only when none left
    bool hasVideosFor(int wr, int hr) const;
    // Returns a map of ratio (rounded to nearest 0.001) -> count of videos with that ratio
    std::map<float, int> getRatioCounts() const;
    bool hasObjectColumn() const { return objectColumnFound; }
private:
    bool passesObjectFilter(const VideoEntry& entry) const;

    static constexpr float RATIO_TOLERANCE = 0.01f;  // forgiving for "1" vs "1.000", 0.667 vs 0.6667, etc.

    bool objectColumnFound = false;

    std::vector<VideoEntry> videos;
    std::map<std::string, std::vector<size_t>> availableByRatioKey;  // key "wr_hr" -> indices into videos
    std::string videoFolder;  // directory containing videos (parent of csv)

    // Filter state (set via setObjectFilter)
    std::unordered_set<std::string> objectFilter;  // specific IDs to match; empty = no filter (wildcard)
    bool filterToEmptyList = false;  // when true, only pass videos whose objectList is empty
    bool selectExactMatch = false;   // when true, objectList must equal objectFilter exactly
};
