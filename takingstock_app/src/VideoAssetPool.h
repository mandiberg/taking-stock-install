#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <cmath>

struct VideoEntry {
    std::string videoId;
    std::string filename;
    float ratio;
    float duration = 0.f;  // duration in seconds from CSV duration column
    std::string object;
    std::string clusterNo;
    std::string poseNo;
    std::string fullPath;  // resolved path to video file
};

class VideoAssetPool {
public:
    bool loadFromCsv(const std::string& csvPath);
    void resetUsed();  // call when starting a new layout - makes all videos available again
    void setObjectFilter(const std::vector<std::string>& allowedObjects);  // empty or ["*"] = no filter
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
    std::set<std::string> objectFilter;  // when non-empty and without "*", filter by object column
};
