#include "ArrangementIO.h"
#include "ofMain.h"
#include "ofFileUtils.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <numeric>
#include <algorithm>

static const uint32_t MAGIC = 0x42534F52;  // "BSOR"
static const uint32_t VERSION = 1;

namespace ArrangementIO {

static std::string getAspectPrefix(int boxWidth, int boxHeight, int nestingLayers) {
    double ratio = (boxHeight != 0) ? static_cast<double>(boxWidth) / boxHeight : 0.0;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << ratio;
    return oss.str() + "_nest" + std::to_string(nestingLayers) + "_arrangements_";
}

std::string getArrangementPath(const std::string& arrangementsPath, int boxWidth, int boxHeight, int nestingLayers, int numArrangements) {
    std::string filename = getAspectPrefix(boxWidth, boxHeight, nestingLayers) + std::to_string(numArrangements);
    std::string baseDir = ofToDataPath(arrangementsPath, true);
    return ofFilePath::join(baseDir, filename);
}

std::string findArrangementPath(const std::string& arrangementsPath, int boxWidth, int boxHeight, int nestingLayers) {
    std::string prefix = getAspectPrefix(boxWidth, boxHeight, nestingLayers);
    std::string arrangementsDir = ofToDataPath(arrangementsPath, true);
    ofDirectory dir(arrangementsDir);
    if (!dir.exists()) return "";
    dir.listDir();
    for (int i = 0; i < dir.size(); ++i) {
        std::string name = dir.getName(i);
        if (name.size() > prefix.size() && name.substr(0, prefix.size()) == prefix) {
            return dir.getPath(i);
        }
    }
    return "";
}

// -- Input fingerprinting -------------------------------------------------------

static uint64_t fnv1a64(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

static uint64_t hashFileContents(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return 0;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return fnv1a64(content);
}

std::string computeInputsFingerprint(const std::string& csvPath) {
    std::string fullCsvPath = ofToDataPath(csvPath, true);
    uint64_t csvHash = hashFileContents(fullCsvPath);

    // List all mp4 files in the video folder (same folder as the CSV), sorted by name.
    // Include each file's size so additions, removals, and replacements are all detected.
    std::string videoFolder = ofFilePath::getEnclosingDirectory(fullCsvPath, false);
    ofDirectory dir(videoFolder);
    dir.allowExt("mp4");
    dir.allowExt("MP4");
    dir.listDir();

    std::vector<std::string> entries;
    entries.reserve(dir.size());
    for (int i = 0; i < dir.size(); ++i) {
        std::ifstream vf(dir.getPath(i), std::ios::binary | std::ios::ate);
        long long sz = vf.is_open() ? static_cast<long long>(vf.tellg()) : -1;
        entries.push_back(dir.getName(i) + ":" + std::to_string(sz));
    }
    std::sort(entries.begin(), entries.end());

    uint64_t videoHash = 14695981039346656037ULL;
    for (const auto& e : entries)
        videoHash = fnv1a64(std::to_string(videoHash) + e);

    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << csvHash
        << "_" << std::setw(16) << std::setfill('0') << videoHash;
    return oss.str();
}

std::string getFingerprintPath(const std::string& arrangementsPath, int boxWidth, int boxHeight, int nestingLayers) {
    std::string filename = getAspectPrefix(boxWidth, boxHeight, nestingLayers) + "inputs.fingerprint";
    return ofFilePath::join(ofToDataPath(arrangementsPath, true), filename);
}

bool saveFingerprint(const std::string& path, const std::string& fingerprint) {
    std::string dirPath = ofFilePath::getEnclosingDirectory(path, false);
    ofDirectory::createDirectory(dirPath, false, true);
    std::ofstream f(path);
    if (!f) {
        ofLogError("ArrangementIO") << "Failed to write fingerprint: " << path;
        return false;
    }
    f << fingerprint;
    return f.good();
}

std::string loadFingerprint(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::string fp;
    std::getline(f, fp);
    return fp;
}

// -- Arrangement validation ----------------------------------------------------

static bool validateNestedItemsInBounds(const std::map<std::pair<int, int>, NestedBinData>& nestedBins,
                                         const std::vector<BinItem>& parentItems) {
    for (const auto& kv : nestedBins) {
        const NestedBinData& nd = kv.second;
        int pw = nd.parentW, ph = nd.parentH;
        if (pw <= 0 || ph <= 0) return false;
        for (const auto& it : nd.items) {
            if (it.x < 0 || it.y < 0 || it.x + it.w > pw || it.y + it.h > ph)
                return false;
        }
        if (!validateNestedItemsInBounds(nd.nestedBins, nd.items))
            return false;
    }
    return true;
}

static bool validateTopLevelNestedInBounds(const std::map<std::pair<int, int>, NestedBinData>& nestedBins,
                                           const std::vector<std::vector<BinItem>>& bins) {
    for (const auto& kv : nestedBins) {
        int binIdx = kv.first.first;
        const NestedBinData& nd = kv.second;
        if (binIdx < 0 || binIdx >= (int)bins.size()) return false;
        int pw = nd.parentW, ph = nd.parentH;
        if (pw <= 0 || ph <= 0) return false;
        for (const auto& it : nd.items) {
            if (it.x < 0 || it.y < 0 || it.x + it.w > pw || it.y + it.h > ph)
                return false;
        }
        if (!validateNestedItemsInBounds(nd.nestedBins, nd.items))
            return false;
    }
    return true;
}

bool isValidArrangement(const Arrangement& arr, int boxWidth, int boxHeight) {
    if (boxWidth <= 0 || boxHeight <= 0) return false;
    for (const auto& bin : arr.bins) {
        for (const auto& it : bin) {
            if (it.x < 0 || it.y < 0 || it.w <= 0 || it.h <= 0 ||
                it.x + it.w > boxWidth || it.y + it.h > boxHeight)
                return false;
        }
    }
    return validateTopLevelNestedInBounds(arr.nestedBins, arr.bins);
}

static bool writeBinItem(std::ostream& os, const BinItem& item) {
    os.write(reinterpret_cast<const char*>(&item.x), sizeof(int));
    os.write(reinterpret_cast<const char*>(&item.y), sizeof(int));
    os.write(reinterpret_cast<const char*>(&item.w), sizeof(int));
    os.write(reinterpret_cast<const char*>(&item.h), sizeof(int));
    os.write(reinterpret_cast<const char*>(&item.itemIdx), sizeof(int));
    return os.good();
}

static bool readBinItem(std::istream& is, BinItem& item) {
    is.read(reinterpret_cast<char*>(&item.x), sizeof(int));
    is.read(reinterpret_cast<char*>(&item.y), sizeof(int));
    is.read(reinterpret_cast<char*>(&item.w), sizeof(int));
    is.read(reinterpret_cast<char*>(&item.h), sizeof(int));
    is.read(reinterpret_cast<char*>(&item.itemIdx), sizeof(int));
    return is.good();
}

static bool writeNestedBinData(std::ostream& os, const NestedBinData& nd) {
    int32_t count = static_cast<int32_t>(nd.items.size());
    os.write(reinterpret_cast<const char*>(&count), sizeof(int32_t));
    for (const auto& it : nd.items)
        if (!writeBinItem(os, it)) return false;

    os.write(reinterpret_cast<const char*>(&nd.parentX), sizeof(int));
    os.write(reinterpret_cast<const char*>(&nd.parentY), sizeof(int));
    os.write(reinterpret_cast<const char*>(&nd.parentW), sizeof(int));
    os.write(reinterpret_cast<const char*>(&nd.parentH), sizeof(int));

    count = static_cast<int32_t>(nd.nestedBins.size());
    os.write(reinterpret_cast<const char*>(&count), sizeof(int32_t));
    for (const auto& kv : nd.nestedBins) {
        os.write(reinterpret_cast<const char*>(&kv.first.first), sizeof(int));
        os.write(reinterpret_cast<const char*>(&kv.first.second), sizeof(int));
        if (!writeNestedBinData(os, kv.second)) return false;
    }
    return os.good();
}

static bool readNestedBinData(std::istream& is, NestedBinData& nd) {
    int32_t count;
    is.read(reinterpret_cast<char*>(&count), sizeof(int32_t));
    if (!is.good() || count < 0) return false;
    nd.items.resize(count);
    for (int32_t i = 0; i < count; ++i)
        if (!readBinItem(is, nd.items[i])) return false;

    is.read(reinterpret_cast<char*>(&nd.parentX), sizeof(int));
    is.read(reinterpret_cast<char*>(&nd.parentY), sizeof(int));
    is.read(reinterpret_cast<char*>(&nd.parentW), sizeof(int));
    is.read(reinterpret_cast<char*>(&nd.parentH), sizeof(int));
    if (!is.good()) return false;

    is.read(reinterpret_cast<char*>(&count), sizeof(int32_t));
    if (!is.good() || count < 0) return false;
    for (int32_t i = 0; i < count; ++i) {
        int k1, k2;
        is.read(reinterpret_cast<char*>(&k1), sizeof(int));
        is.read(reinterpret_cast<char*>(&k2), sizeof(int));
        if (!is.good()) return false;
        NestedBinData child;
        if (!readNestedBinData(is, child)) return false;
        nd.nestedBins[{k1, k2}] = std::move(child);
    }
    return is.good();
}

bool save(const std::string& path, const std::vector<Arrangement>& arrangements) {
    std::string dirPath = ofFilePath::getEnclosingDirectory(path, false);
    if (!ofDirectory::createDirectory(dirPath, false, true)) {
        ofLogError("ArrangementIO") << "Failed to create directory: " << dirPath;
        return false;
    }

    std::ofstream os(path, std::ios::binary);
    if (!os) {
        ofLogError("ArrangementIO") << "Failed to open for write: " << path;
        return false;
    }

    os.write(reinterpret_cast<const char*>(&MAGIC), sizeof(uint32_t));
    os.write(reinterpret_cast<const char*>(&VERSION), sizeof(uint32_t));
    int32_t count = static_cast<int32_t>(arrangements.size());
    os.write(reinterpret_cast<const char*>(&count), sizeof(int32_t));

    for (const auto& arr : arrangements) {
        int32_t numBins = static_cast<int32_t>(arr.bins.size());
        os.write(reinterpret_cast<const char*>(&numBins), sizeof(int32_t));
        for (const auto& bin : arr.bins) {
            int32_t numItems = static_cast<int32_t>(bin.size());
            os.write(reinterpret_cast<const char*>(&numItems), sizeof(int32_t));
            for (const auto& item : bin)
                if (!writeBinItem(os, item)) { os.close(); return false; }
        }
        int32_t numNested = static_cast<int32_t>(arr.nestedBins.size());
        os.write(reinterpret_cast<const char*>(&numNested), sizeof(int32_t));
        for (const auto& kv : arr.nestedBins) {
            os.write(reinterpret_cast<const char*>(&kv.first.first), sizeof(int));
            os.write(reinterpret_cast<const char*>(&kv.first.second), sizeof(int));
            if (!writeNestedBinData(os, kv.second)) { os.close(); return false; }
        }
    }

    if (!os.good()) {
        ofLogError("ArrangementIO") << "Write failed: " << path;
        return false;
    }
    ofLogNotice("ArrangementIO") << "Saved " << arrangements.size() << " arrangements to " << path;
    return true;
}

bool load(const std::string& path, std::vector<Arrangement>& out) {
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;

    uint32_t magic, version;
    is.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
    is.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
    if (!is.good() || magic != MAGIC || version != VERSION) return false;

    int32_t count;
    is.read(reinterpret_cast<char*>(&count), sizeof(int32_t));
    if (!is.good() || count < 0) return false;

    out.clear();
    out.reserve(count);

    for (int32_t a = 0; a < count; ++a) {
        Arrangement arr;
        int32_t numBins;
        is.read(reinterpret_cast<char*>(&numBins), sizeof(int32_t));
        if (!is.good() || numBins < 0) return false;
        arr.bins.resize(numBins);
        for (int32_t b = 0; b < numBins; ++b) {
            int32_t numItems;
            is.read(reinterpret_cast<char*>(&numItems), sizeof(int32_t));
            if (!is.good() || numItems < 0) return false;
            arr.bins[b].resize(numItems);
            for (int32_t i = 0; i < numItems; ++i)
                if (!readBinItem(is, arr.bins[b][i])) return false;
        }
        int32_t numNested;
        is.read(reinterpret_cast<char*>(&numNested), sizeof(int32_t));
        if (!is.good() || numNested < 0) return false;
        for (int32_t n = 0; n < numNested; ++n) {
            int k1, k2;
            is.read(reinterpret_cast<char*>(&k1), sizeof(int));
            is.read(reinterpret_cast<char*>(&k2), sizeof(int));
            if (!is.good()) return false;
            NestedBinData nd;
            if (!readNestedBinData(is, nd)) return false;
            arr.nestedBins[{k1, k2}] = std::move(nd);
        }
        out.push_back(std::move(arr));
    }

    ofLogNotice("ArrangementIO") << "Loaded " << out.size() << " arrangements from " << path;
    return true;
}

}
