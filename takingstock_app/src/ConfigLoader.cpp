#include "ConfigLoader.h"
#include "BinSorter.h"
#include "ofMain.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

std::string ConfigLoader::trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

void ConfigLoader::parseLine(const std::string& line, BinSorterConfig& config) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) return;
    std::string key = trim(line.substr(0, eq));
    std::string value = trim(line.substr(eq + 1));
    size_t hashPos = value.find('#');
    if (hashPos != std::string::npos)
        value = trim(value.substr(0, hashPos));
    if (key.empty()) return;

    if (key == "BOX_WIDTH") { config.boxWidth = std::stoi(value); return; }
    if (key == "BOX_HEIGHT") { config.boxHeight = std::stoi(value); return; }
    if (key == "VIDEO_ASSET_PATH") { config.videoAssetPath = value; return; }
    if (key == "VIDEOS_CSV_PATH") { config.videosCsvPath = value; return; }
    if (key == "ARRANGEMENTS_PATH") { config.arrangementsPath = value; return; }
    if (key == "VIDEO_LOOP") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        config.videoLoop = (v == "1" || v == "true" || v == "yes");
        return;
    }
    if (key == "TRANSITION_TYPE") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        if (v == "fade") config.transitionType = TransitionType::Fade;
        else if (v == "jumpcut_to_black") config.transitionType = TransitionType::JumpcutToBlack;
        else config.transitionType = TransitionType::Jumpcut;
        return;
    }
    if (key == "TRANSITION_DURATION_FADE") { config.transitionDurationFade = std::stof(value); return; }
    if (key == "TRANSITION_DURATION_JUMP_TO_BLACK") { config.transitionDurationJumpToBlack = std::stof(value); return; }
    if (key == "TRANSITION_TIMER_MIN") { config.transitionTimerMin = std::stof(value); return; }
    if (key == "TRANSITION_TIMER_MAX") { config.transitionTimerMax = std::stof(value); return; }
    if (key == "MIN_SPACE_THRESHOLD") {
        int v = std::stoi(value);
        config.gapFilterThreshold = v;
        config.packingStopArea = v;
        return;
    }
    if (key == "GAP_FILTER_THRESHOLD") { config.gapFilterThreshold = std::stoi(value); return; }
    if (key == "ASPECT_EXPAND_FILTER") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        config.aspectExpandFilter = (v == "1" || v == "true" || v == "yes");
        return;
    }
    if (key == "PACKING_STOP_AREA") { config.packingStopArea = std::stoi(value); return; }
    if (key == "NESTING_LAYERS") { config.nestingLayers = std::stoi(value); return; }
    if (key == "NESTED_MIN_SPACE_THRESHOLD") { config.nestedMinSpaceThreshold = std::stoi(value); return; }
    if (key == "MAIN_BIN_FILL_CHANCE") { config.mainBinFillChance = std::stof(value); return; }
    if (key == "ITEM_BREAK_SCALE") { config.itemBreakScale = std::stof(value); return; }
    if (key == "ITEM_BREAK_CHANCE") { config.itemBreakChance = std::stof(value); return; }
    if (key == "BREAK_BOX_MIN_ITEMS") { config.breakBoxMinItems = std::stoi(value); return; }
    if (key == "BREAK_BOX_MAX_ITEMS") { config.breakBoxMaxItems = std::stoi(value); return; }
    if (key == "BREAK_BOX_FILL_ATTEMPTS") { config.breakBoxFillAttempts = std::stoi(value); return; }
    if (key == "BREAK_BOX_COVERAGE_THRESHOLD") { config.breakBoxCoverageThreshold = std::stof(value); return; }
    if (key == "LAYOUT_MAX_ATTEMPTS") { config.layoutMaxAttempts = std::stoi(value); return; }
    if (key == "LAYOUT_STALE_THRESHOLD") { config.layoutStaleThreshold = std::stoi(value); return; }
    if (key == "LAYOUT_PHASES") { config.layoutPhases = std::stoi(value); return; }
    if (key == "PLACEMENT_AREA_EXPONENT") { config.placementAreaExponent = std::stof(value); return; }
    if (key == "PLACEMENT_TOP_K") { config.placementTopK = std::stoi(value); return; }
    if (key == "WEIGHT_NORMALIZATION") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        if (v == "raw") config.weightNormalization = WeightNormalization::Raw;
        else if (v == "equal") config.weightNormalization = WeightNormalization::Equal;
        else config.weightNormalization = WeightNormalization::Sqrt;
        return;
    }
    if (key == "SELECT_MODE") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        config.selectMode = (v == "1" || v == "true" || v == "yes");
        return;
    }
    if (key == "KEY_VIDEO") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        config.keyVideo = (v == "1" || v == "true" || v == "yes");
        return;
    }
    if (key == "KEY_VIDEO_MIN_LENGTH") { config.keyVideoMinLength = std::stof(value); return; }

    if (key == "SELECT") {
        size_t lb = value.find('[');
        size_t rb = value.find(']');
        if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
            std::string inner = trim(value.substr(lb + 1, rb - lb - 1));
            std::string rest = trim(value.substr(rb + 1));
            SelectOption opt;
            if (!inner.empty()) {
                size_t pos = 0;
                while (pos < inner.size()) {
                    size_t comma = inner.find(',', pos);
                    std::string obj = trim(inner.substr(pos, (comma == std::string::npos ? inner.size() : comma) - pos));
                    if (!obj.empty()) opt.objects.push_back(obj);
                    pos = (comma == std::string::npos) ? inner.size() : comma + 1;
                }
            }
            float weight = 1.0f;
            if (!rest.empty() && rest[0] == ',') {
                std::istringstream iss(trim(rest.substr(1)));
                if (iss >> weight) { /* ok */ }
            } else {
                std::istringstream iss(rest);
                if (iss >> weight) { /* ok */ }
            }
            opt.weight = weight;
            config.selectOptions.push_back(opt);
        }
        return;
    }

    if (key == "EXPAND_RANGE") {
        size_t lb = value.find('[');
        size_t rb = value.find(']');
        if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
            std::istringstream iss(value.substr(lb + 1, rb - lb - 1));
            float vals[6] = {};
            char comma;
            int n = 0;
            while (n < 6 && (iss >> vals[n])) { n++; iss >> comma; }
            if (n == 6) {
                ExpandRange er;
                er.minRatio = vals[0]; er.maxRatio = vals[1];
                er.top = vals[2]; er.right = vals[3]; er.bottom = vals[4]; er.left = vals[5];
                config.expandRanges.push_back(er);
            } else {
                ofLogWarning("ConfigLoader") << "EXPAND_RANGE needs 6 values [minRatio, maxRatio, top, right, bottom, left]; got " << n;
            }
        }
        return;
    }
    if (key == "EXPAND_FALLBACK") {
        size_t lb = value.find('[');
        size_t rb = value.find(']');
        if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
            std::istringstream iss(value.substr(lb + 1, rb - lb - 1));
            float vals[4] = {};
            char comma;
            int n = 0;
            while (n < 4 && (iss >> vals[n])) { n++; iss >> comma; }
            if (n == 4) {
                config.expandFallback = {vals[0], vals[1], vals[2], vals[3]};
            } else {
                ofLogWarning("ConfigLoader") << "EXPAND_FALLBACK needs 4 values [top, right, bottom, left]; got " << n;
            }
        }
        return;
    }
}

bool ConfigLoader::load(const std::string& path, BinSorterConfig& out) {
    std::string fullPath = ofToDataPath(path, true);
    std::ifstream f(fullPath);
    if (!f.is_open()) {
        ofLogError("ConfigLoader") << "Cannot open config: " << fullPath;
        return false;
    }
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        parseLine(line, out);
    }

    // Warn about overlapping EXPAND_RANGEs (first match wins, but overlap is likely a mistake)
    const auto& ranges = out.expandRanges;
    for (size_t i = 0; i < ranges.size(); ++i) {
        for (size_t j = i + 1; j < ranges.size(); ++j) {
            if (ranges[i].minRatio < ranges[j].maxRatio && ranges[j].minRatio < ranges[i].maxRatio) {
                ofLogWarning("ConfigLoader")
                    << "EXPAND_RANGE overlap: entry " << i << " [" << ranges[i].minRatio << ", " << ranges[i].maxRatio << "]"
                    << " overlaps entry " << j << " [" << ranges[j].minRatio << ", " << ranges[j].maxRatio << "]"
                    << " — first match wins, consider fixing your ranges";
            }
        }
    }

    return true;
}
