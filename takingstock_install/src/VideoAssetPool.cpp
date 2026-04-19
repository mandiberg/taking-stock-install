#include "VideoAssetPool.h"
#include "ofMain.h"
#include "ofFileUtils.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

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
    // Parse header to find column indices (supports video_id,filename,ratio,object OR video_id,filename,object,ratio)
    std::vector<std::string> header = splitCsvLine(line);
    int idxVideoId = -1, idxFilename = -1, idxRatio = -1, idxObject = -1;
    for (size_t i = 0; i < header.size(); ++i) {
        std::string h = header[i];
        std::transform(h.begin(), h.end(), h.begin(), ::tolower);
        if (h.find("video_id") != std::string::npos) idxVideoId = (int)i;
        else if (h.find("filename") != std::string::npos) idxFilename = (int)i;
        else if (h.find("ratio") != std::string::npos) idxRatio = (int)i;
        else if (h.find("object") != std::string::npos) idxObject = (int)i;
    }
    if (idxVideoId < 0 || idxFilename < 0 || idxRatio < 0 || idxObject < 0) {
        ofLogWarning("VideoAssetPool") << "videos.csv header must have video_id, filename, ratio, object columns";
        return false;
    }

    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::vector<std::string> fields = splitCsvLine(line);
        int maxIdx = std::max(std::max(idxVideoId, idxFilename), std::max(idxRatio, idxObject));
        if (fields.size() < (size_t)maxIdx + 1) continue;

        VideoEntry entry;
        entry.videoId = fields[idxVideoId];
        entry.filename = fields[idxFilename];
        entry.object = fields[idxObject];
        try {
            entry.ratio = std::stof(fields[idxRatio]);
        } catch (...) {
            ofLogWarning("VideoAssetPool") << "Invalid ratio in row: " << line;
            continue;
        }
        entry.fullPath = ofFilePath::join(videoFolder, entry.filename);

        videos.push_back(entry);
    }

    resetUsed();
    ofLogNotice("VideoAssetPool") << "Loaded " << videos.size() << " videos from " << fullPath
        << " (video folder: " << videoFolder << ")";
    return !videos.empty();
}

void VideoAssetPool::resetUsed() {
    availableByRatioKey.clear();
}

void VideoAssetPool::setObjectFilter(const std::vector<std::string>& allowedObjects) {
    objectFilter.clear();
    if (allowedObjects.empty()) return;
    for (const auto& o : allowedObjects) {
        if (o == "*") return;  // [*] or [x, *] = no filter
    }
    for (const auto& o : allowedObjects) {
        if (!o.empty()) objectFilter.insert(o);
    }
}

bool VideoAssetPool::passesObjectFilter(const VideoEntry& entry) const {
    if (objectFilter.empty()) return true;
    if (objectFilter.find("*") != objectFilter.end()) return true;
    return objectFilter.find(entry.object) != objectFilter.end();
}

std::string VideoAssetPool::getVideoPath(int wr, int hr) {
    if (videos.empty()) return "";
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
        // Diagnostic: log target aspect and sample of ratios in CSV to help troubleshoot
        std::string ratioSample;
        int n = 0;
        for (const auto& v : videos) {
            if (n++ < 5) ratioSample += (ratioSample.empty() ? "" : ", ") + std::to_string(v.ratio);
        }
        if (videos.size() > 5) ratioSample += ", ...";
        ofLogWarning("VideoAssetPool") << "No video for ratio " << key
            << " (target aspect " << targetAspect << ", tolerance " << RATIO_TOLERANCE
            << "). CSV ratios sample: [" << ratioSample << "]";
        return "";
    }

    size_t idx = (size_t)(ofRandom(0.0f, (float)available.size()));
    if (idx >= available.size()) idx = available.size() - 1;
    size_t vidIdx = available[idx];
    available[idx] = available.back();
    available.pop_back();
    return videos[vidIdx].fullPath;
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
