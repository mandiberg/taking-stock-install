#include "VideoAssetPool.h"
#include "ConfigLoader.h"
#include "ofMain.h"
#include "ofFileUtils.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if ((c == ',' && !inQuotes) || c == '\r') {
            fields.push_back(trim(field));
            field.clear();
        } else if (c != '\r') {
            field += c;
        }
    }
    fields.push_back(trim(field));
    return fields;
}

// Extract the base video ID from a filename by stripping "_scale_<digits>" before the extension.
// e.g. "foo_scale_1080.mp4" → "foo.mp4", "foo_scale_2160.mp4" → "foo.mp4", "bar.mp4" → "bar.mp4"
static std::string computeBaseVideoId(const std::string& filename) {
    // Find last occurrence of "_scale_" followed by digits and then "."
    size_t scalePos = std::string::npos;
    size_t searchFrom = 0;
    while (true) {
        size_t pos = filename.find("_scale_", searchFrom);
        if (pos == std::string::npos) break;
        // Check that everything after "_scale_" up to the next "." is digits
        size_t digitStart = pos + 7;  // len("_scale_") == 7
        size_t dotPos = filename.find('.', digitStart);
        if (dotPos == std::string::npos) { searchFrom = pos + 1; continue; }
        bool allDigits = (dotPos > digitStart);
        for (size_t i = digitStart; i < dotPos && allDigits; ++i)
            if (!std::isdigit((unsigned char)filename[i])) allDigits = false;
        if (allDigits) { scalePos = pos; break; }
        searchFrom = pos + 1;
    }
    if (scalePos == std::string::npos) return filename;
    // Replace "_scale_<digits>" with nothing: keep everything before scalePos + extension
    size_t extPos = filename.rfind('.');
    std::string base = filename.substr(0, scalePos);
    if (extPos != std::string::npos && extPos > scalePos)
        base += filename.substr(extPos);
    return base;
}

// Parse a CSV object column value like "[67, 95]" or "[]" into a list of trimmed ID strings.
static std::vector<std::string> parseObjectList(const std::string& raw) {
    std::vector<std::string> result;
    size_t lb = raw.find('[');
    size_t rb = raw.find(']');
    if (lb == std::string::npos || rb == std::string::npos || rb <= lb) return result;
    std::string inner = trim(raw.substr(lb + 1, rb - lb - 1));
    if (inner.empty()) return result;
    size_t pos = 0;
    while (pos < inner.size()) {
        size_t comma = inner.find(',', pos);
        std::string item = trim(inner.substr(pos, (comma == std::string::npos ? inner.size() : comma) - pos));
        if (!item.empty()) result.push_back(item);
        pos = (comma == std::string::npos) ? inner.size() : comma + 1;
    }
    return result;
}

bool VideoAssetPool::loadFromCsv(const std::string& csvPath) {
    videos.clear();
    availableByRatioKey.clear();

    std::string fullPath = ofToDataPath(csvPath, true);
    std::ifstream f(fullPath);
    if (!f.is_open()) {
        ofLogWarning("VideoAssetPool") << "Cannot open videos.csv: " << fullPath;
        return false;
    }

    // Video folder = parent directory of csv (videos live alongside videos.csv)
    videoFolder = ofFilePath::getEnclosingDirectory(fullPath, false);

    std::string line;
    if (!std::getline(f, line)) {
        ofLogWarning("VideoAssetPool") << "videos.csv is empty";
        return false;
    }
    // Parse header — required: file_name, ratio; optional: object, cluster_no, pose_no, width, height
    // Legacy 'filename' column is accepted with a deprecation warning.
    std::vector<std::string> header = splitCsvLine(line);
    int idxFilename = -1, idxRatio = -1, idxObject = -1, idxClusterNo = -1, idxPoseNo = -1, idxDuration = -1;
    int idxWidth = -1, idxHeight = -1;
    for (size_t i = 0; i < header.size(); ++i) {
        std::string h = header[i];
        std::transform(h.begin(), h.end(), h.begin(), ::tolower);
        if (h == "file_name") {
            idxFilename = (int)i;
        } else if (h == "filename" && idxFilename < 0) {
            idxFilename = (int)i;
            ofLogWarning("VideoAssetPool") << "CSV column 'filename' is deprecated; rename it to 'file_name'";
        } else if (h == "ratio") {
            idxRatio = (int)i;
        } else if (h.find("object") != std::string::npos) {
            idxObject = (int)i;
        } else if (h == "cluster_no") {
            idxClusterNo = (int)i;
        } else if (h == "pose_no") {
            idxPoseNo = (int)i;
        } else if (h == "duration") {
            idxDuration = (int)i;
        } else if (h == "width") {
            idxWidth = (int)i;
        } else if (h == "height") {
            idxHeight = (int)i;
        }
    }
    if (idxFilename < 0 || idxRatio < 0) {
        ofLogWarning("VideoAssetPool") << "installation.csv must have 'file_name' and 'ratio' columns";
        return false;
    }
    objectColumnFound = (idxObject >= 0);
    if (!objectColumnFound) {
        ofLogWarning("VideoAssetPool") << "installation.csv has no 'object' column; "
            "SELECT_MODE cannot work and will be disabled";
    }

    int rowNum = 1;
    int discardedCount = 0;
    while (std::getline(f, line)) {
        ++rowNum;
        if (line.empty()) continue;
        std::vector<std::string> fields = splitCsvLine(line);
        int maxRequired = std::max(idxFilename, idxRatio);
        if ((int)fields.size() < maxRequired + 1) continue;

        VideoEntry entry;
        entry.filename  = fields[idxFilename];
        entry.videoId   = entry.filename;
        entry.object    = (idxObject    >= 0 && idxObject    < (int)fields.size()) ? fields[idxObject]    : "";
        entry.objectList = parseObjectList(entry.object);
        entry.clusterNo = (idxClusterNo >= 0 && idxClusterNo < (int)fields.size()) ? fields[idxClusterNo] : "";
        entry.poseNo    = (idxPoseNo    >= 0 && idxPoseNo    < (int)fields.size()) ? fields[idxPoseNo]    : "";
        try {
            entry.ratio = std::stof(fields[idxRatio]);
        } catch (...) {
            ofLogWarning("VideoAssetPool") << "Row " << rowNum << ": invalid ratio value, skipping";
            continue;
        }
        if (idxDuration >= 0 && idxDuration < (int)fields.size() && !fields[idxDuration].empty()) {
            try {
                entry.duration = std::stof(fields[idxDuration]);
            } catch (...) {
                ofLogWarning("VideoAssetPool") << "Row " << rowNum << ": invalid duration value, defaulting to 0";
            }
        }
        if (idxWidth >= 0 && idxWidth < (int)fields.size() && !fields[idxWidth].empty()) {
            try { entry.videoWidth  = std::stoi(fields[idxWidth]);  } catch (...) {}
        }
        if (idxHeight >= 0 && idxHeight < (int)fields.size() && !fields[idxHeight].empty()) {
            try { entry.videoHeight = std::stoi(fields[idxHeight]); } catch (...) {}
        }
        entry.baseVideoId = computeBaseVideoId(entry.filename);

        if (minDuration > 0.f && entry.duration < minDuration) {
            discardedCount++;
            continue;
        }

        entry.fullPath = ofFilePath::join(videoFolder, entry.filename);

        if (idxClusterNo >= 0 && entry.clusterNo.empty())
            ofLogWarning("VideoAssetPool") << "Row " << rowNum << " (" << entry.filename << "): cluster_no is empty";
        if (idxPoseNo >= 0 && entry.poseNo.empty())
            ofLogWarning("VideoAssetPool") << "Row " << rowNum << " (" << entry.filename << "): pose_no is empty";

        videos.push_back(entry);
    }

    if (discardedCount > 0)
        ofLogNotice("VideoAssetPool") << "Discarded " << discardedCount
            << " videos shorter than " << minDuration << "s (MIN_VIDEO_LENGTH)";

    // Build scale-variant lookup: baseVideoId -> all indices in videos[]
    scaleVariantsByBase.clear();
    for (size_t i = 0; i < videos.size(); ++i)
        scaleVariantsByBase[videos[i].baseVideoId].push_back(i);

    if (scaleSelectEnabled) {
        int multiCount = 0;
        for (const auto& kv : scaleVariantsByBase)
            if (kv.second.size() > 1) ++multiCount;
        ofLogNotice("VideoAssetPool") << "SCALE_SELECT enabled: "
            << multiCount << " logical video(s) with multiple scale variants";
    }

    resetUsed();
    ofLogNotice("VideoAssetPool") << "Loaded " << videos.size() << " videos from " << fullPath
        << " (video folder: " << videoFolder << ")";
    return !videos.empty();
}

void VideoAssetPool::resetUsed() {
    availableByRatioKey.clear();
}

// Given the index of a picked video, find the optimal scale variant for slotH.
// Returns the index of the variant with the smallest videoHeight that is >= slotH.
// Falls back to the variant with the largest videoHeight if none are large enough.
// Returns vidIdx unchanged if there are no multi-scale variants or no dimension data.
size_t VideoAssetPool::pickBestScaleVariant(size_t vidIdx, int slotH) const {
    const std::string& baseId = videos[vidIdx].baseVideoId;
    auto it = scaleVariantsByBase.find(baseId);
    if (it == scaleVariantsByBase.end()) return vidIdx;
    const std::vector<size_t>& variants = it->second;
    if (variants.size() <= 1) return vidIdx;

    // Find the smallest videoHeight that covers the slot
    size_t bestIdx = variants[0];
    bool foundCover = false;
    int bestHeight = 0;

    for (size_t vi : variants) {
        int h = videos[vi].videoHeight;
        if (h <= 0) continue;  // no dimension data for this entry; skip
        if (!foundCover) {
            // Haven't found a covering scale yet — track the largest as fallback
            if (h > bestHeight) { bestHeight = h; bestIdx = vi; }
            if (h >= slotH) { foundCover = true; bestHeight = h; bestIdx = vi; }
        } else {
            // Already have a covering scale; prefer a smaller one that still covers
            if (h >= slotH && h < bestHeight) { bestHeight = h; bestIdx = vi; }
        }
    }
    return bestIdx;
}

// Remove all indices listed in variantIndices from the available vector (swap-erase idiom).
void VideoAssetPool::removeVariantsFromAvailable(std::vector<size_t>& available,
                                                  const std::vector<size_t>& variantIndices) {
    if (variantIndices.empty()) return;
    // Build a set for O(1) lookup
    std::unordered_set<size_t> toRemove(variantIndices.begin(), variantIndices.end());
    size_t write = 0;
    for (size_t read = 0; read < available.size(); ++read) {
        if (!toRemove.count(available[read]))
            available[write++] = available[read];
    }
    available.resize(write);
}

void VideoAssetPool::setObjectFilter(const SelectOption& opt, bool exactMatch) {
    objectFilter.clear();
    filterToEmptyList = false;
    selectExactMatch = exactMatch;

    // [*] wildcard — no filter, all videos pass
    for (const auto& o : opt.objects) {
        if (o == "*") return;
    }

    if (opt.matchEmptyList) {
        filterToEmptyList = true;
        return;
    }

    for (const auto& o : opt.objects) {
        if (!o.empty()) objectFilter.insert(o);
    }
}

bool VideoAssetPool::passesObjectFilter(const VideoEntry& entry) const {
    // No active filter — all videos pass (wildcard or select mode off)
    if (objectFilter.empty() && !filterToEmptyList) return true;

    if (filterToEmptyList) return entry.objectList.empty();

    if (selectExactMatch) {
        // CSV objectList must be exactly equal to objectFilter (same elements, any order)
        if (entry.objectList.size() != objectFilter.size()) return false;
        for (const auto& o : entry.objectList) {
            if (!objectFilter.count(o)) return false;
        }
        return true;
    } else {
        // ANY mode — at least one item in objectList must be in objectFilter
        for (const auto& o : entry.objectList) {
            if (objectFilter.count(o)) return true;
        }
        return false;
    }
}

VideoEntry VideoAssetPool::getVideoEntry(int wr, int hr, int slotW, int slotH) {
    if (videos.empty()) return {};
    float targetAspect = (hr > 0) ? (float)wr / hr : 0.f;

    std::string key = std::to_string(wr) + "_" + std::to_string(hr);
    std::vector<size_t>& available = availableByRatioKey[key];

    if (available.empty()) {
        for (size_t i = 0; i < videos.size(); ++i) {
            if (std::abs(videos[i].ratio - targetAspect) < RATIO_TOLERANCE && passesObjectFilter(videos[i]))
                available.push_back(i);
        }
    }

    if (available.empty()) {
        std::string ratioSample;
        int n = 0;
        for (const auto& v : videos) {
            if (n++ < 5) ratioSample += (ratioSample.empty() ? "" : ", ") + std::to_string(v.ratio);
        }
        if (videos.size() > 5) ratioSample += ", ...";
        ofLogWarning("VideoAssetPool") << "No video for ratio " << key
            << " (target aspect " << targetAspect << ", tolerance " << RATIO_TOLERANCE
            << "). CSV ratios sample: [" << ratioSample << "]";
        return {};
    }

    size_t idx = (size_t)(ofRandom(0.0f, (float)available.size()));
    if (idx >= available.size()) idx = available.size() - 1;
    size_t vidIdx = available[idx];

    if (scaleSelectEnabled && slotH > 0) {
        // Scale-aware path: pick the optimal scale variant, then remove all variants from the pool.
        size_t bestIdx = pickBestScaleVariant(vidIdx, slotH);
        const std::string& baseId = videos[vidIdx].baseVideoId;
        auto varIt = scaleVariantsByBase.find(baseId);
        if (varIt != scaleVariantsByBase.end())
            removeVariantsFromAvailable(available, varIt->second);
        else {
            available[idx] = available.back();
            available.pop_back();
        }
        return videos[bestIdx];
    }

    available[idx] = available.back();
    available.pop_back();
    return videos[vidIdx];
}

VideoEntry VideoAssetPool::getVideoEntryWithMinDuration(int wr, int hr, float minDuration, int slotW, int slotH) {
    if (videos.empty()) return {};
    float targetAspect = (hr > 0) ? (float)wr / hr : 0.f;
    std::string key = std::to_string(wr) + "_" + std::to_string(hr);
    std::vector<size_t>& available = availableByRatioKey[key];

    if (available.empty()) {
        for (size_t i = 0; i < videos.size(); ++i) {
            if (std::abs(videos[i].ratio - targetAspect) < RATIO_TOLERANCE && passesObjectFilter(videos[i]))
                available.push_back(i);
        }
    }

    // Collect positions within available[] whose video meets the duration threshold
    std::vector<size_t> qualifying;
    for (size_t pos = 0; pos < available.size(); ++pos) {
        if (videos[available[pos]].duration >= minDuration)
            qualifying.push_back(pos);
    }
    if (qualifying.empty()) return {};  // nothing qualifies; pool is unchanged

    size_t qIdx = (size_t)(ofRandom(0.0f, (float)qualifying.size()));
    if (qIdx >= qualifying.size()) qIdx = qualifying.size() - 1;
    size_t pos = qualifying[qIdx];
    size_t vidIdx = available[pos];

    if (scaleSelectEnabled && slotH > 0) {
        size_t bestIdx = pickBestScaleVariant(vidIdx, slotH);
        const std::string& baseId = videos[vidIdx].baseVideoId;
        auto varIt = scaleVariantsByBase.find(baseId);
        if (varIt != scaleVariantsByBase.end())
            removeVariantsFromAvailable(available, varIt->second);
        else {
            available[pos] = available.back();
            available.pop_back();
        }
        return videos[bestIdx];
    }

    available[pos] = available.back();
    available.pop_back();
    return videos[vidIdx];
}

std::string VideoAssetPool::getVideoPath(int wr, int hr, int slotW, int slotH) {
    return getVideoEntry(wr, hr, slotW, slotH).fullPath;
}

bool VideoAssetPool::hasVideosFor(int wr, int hr) const {
    if (videos.empty()) return false;
    float targetAspect = (hr > 0) ? (float)wr / hr : 0.f;
    for (const auto& v : videos) {
        if (std::abs(v.ratio - targetAspect) < RATIO_TOLERANCE && passesObjectFilter(v))
            return true;
    }
    return false;
}

std::map<float, int> VideoAssetPool::getRatioCounts() const {
    std::map<float, int> counts;
    for (const auto& v : videos) {
        float rounded = std::round(v.ratio * 1000.f) / 1000.f;
        counts[rounded]++;
    }
    return counts;
}
