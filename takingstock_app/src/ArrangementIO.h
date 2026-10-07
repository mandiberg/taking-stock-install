#pragma once

#include "BinSorter.h"
#include <string>
#include <vector>

namespace ArrangementIO {
    // settingsHash identifies the generation settings (see hashSettings); a cached file only
    // matches when the aspect ratio and settingsHash are both equal.
    std::string hashSettings(const std::string& settingsKey);
    std::string getArrangementPath(const std::string& arrangementsPath, int boxWidth, int boxHeight,
                                   const std::string& settingsHash, int numArrangements);
    std::string findArrangementPath(const std::string& arrangementsPath, int boxWidth, int boxHeight,
                                    const std::string& settingsHash);
    // Delete every arrangement file for this aspect ratio except keepPath
    // (any generation settings, including old-style names). Fingerprint files are kept.
    void deleteOtherArrangementFiles(const std::string& arrangementsPath, int boxWidth, int boxHeight,
                                     const std::string& keepPath);
    bool isValidArrangement(const Arrangement& arr, int boxWidth, int boxHeight);
    bool load(const std::string& path, std::vector<Arrangement>& out);
    bool save(const std::string& path, const std::vector<Arrangement>& arrangements);

    // Input fingerprinting: detects changes to the CSV and video files so cached
    // arrangements are automatically regenerated when inputs change.
    std::string computeInputsFingerprint(const std::string& csvPath);
    std::string getFingerprintPath(const std::string& arrangementsPath, int boxWidth, int boxHeight);
    bool saveFingerprint(const std::string& path, const std::string& fingerprint);
    std::string loadFingerprint(const std::string& path);
}
