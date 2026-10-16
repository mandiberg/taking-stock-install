#include "ConfigLoader.h"
#include "BinSorter.h"
#include "ofMain.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

std::string ConfigLoader::trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool ConfigLoader::parseLine(const std::string& line, BinSorterConfig& config) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    std::string key = trim(line.substr(0, eq));
    std::string value = trim(line.substr(eq + 1));
    size_t hashPos = value.find('#');
    if (hashPos != std::string::npos)
        value = trim(value.substr(0, hashPos));
    if (key.empty()) return false;

    if (key == "BOX_WIDTH") { config.boxWidth = std::stoi(value); return true; }
    if (key == "BOX_HEIGHT") { config.boxHeight = std::stoi(value); return true; }
    if (key == "VIDEO_ASSET_PATH") { config.videoAssetPath = value; return true; }
    if (key == "VIDEOS_CSV_PATH") { config.videosCsvPath = value; return true; }
    if (key == "ARRANGEMENTS_PATH") { config.arrangementsPath = value; return true; }
    if (key == "CYCLE_RESET_DURATION") { config.cycleResetDuration = std::stof(value); return true; }
    if (key == "CYCLE_RESET_COUNT")    { config.cycleResetCount    = std::stoi(value); return true; }
    if (key == "VIDEO_FADE_DURATION") { config.transitionDurationFade = std::stof(value); return true; }
    if (key == "PACKING_STOP_AREA") { config.packingStopArea = std::stoi(value); return true; }
    if (key == "ITEM_BREAK_SCALE") { config.itemBreakScale = std::stof(value); return true; }
    if (key == "ITEM_BREAK_CHANCE") { config.itemBreakChance = std::stof(value); return true; }
    if (key == "BREAK_BOX_MIN_ITEMS") { config.breakBoxMinItems = std::stoi(value); return true; }
    if (key == "BREAK_BOX_MAX_ITEMS") { config.breakBoxMaxItems = std::stoi(value); return true; }
    if (key == "BREAK_BOX_FILL_ATTEMPTS") { config.breakBoxFillAttempts = std::stoi(value); return true; }
    if (key == "LAYOUT_MAX_ATTEMPTS") { config.layoutMaxAttempts = std::stoi(value); return true; }
    if (key == "LAYOUT_STALE_THRESHOLD") { config.layoutStaleThreshold = std::stoi(value); return true; }
    if (key == "LAYOUT_PHASES") { config.layoutPhases = std::stoi(value); return true; }
    if (key == "MAX_ITEMS") { config.maxItems = std::stoi(value); return true; }
    if (key == "HEAVY_LAYOUT_ITEMS") { config.heavyLayoutItems = std::stoi(value); return true; }
    if (key == "AFTER_HEAVY_MAX_ITEMS") { config.afterHeavyMaxItems = std::stoi(value); return true; }
    if (key == "PLACEMENT_AREA_EXPONENT") { config.placementAreaExponent = std::stof(value); return true; }
    if (key == "PLACEMENT_TOP_K") { config.placementTopK = std::stoi(value); return true; }
    if (key == "WEIGHT_NORMALIZATION") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        if (v == "raw") config.weightNormalization = WeightNormalization::Raw;
        else if (v == "equal") config.weightNormalization = WeightNormalization::Equal;
        else config.weightNormalization = WeightNormalization::Sqrt;
        return true;
    }
    if (key == "SELECT_MODE") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        config.selectMode = (v == "1" || v == "true" || v == "yes");
        return true;
    }
    if (key == "KEY_VIDEO_MIN_LENGTH") { config.keyVideoMinLength = std::stof(value); return true; }
    if (key == "AUDIO_PATH") { config.audioPath = value; return true; }
    if (key == "AUDIO_FADE_DURATION") { config.audioFadeDuration = std::stof(value); return true; }
    if (key == "AUDIO_SURROUND") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        config.audioSurround = (v == "1" || v == "true" || v == "yes");
        return true;
    }
    if (key == "AUDIO_CHANNEL_MAP") {
        size_t lb = value.find('[');
        size_t rb = value.find(']');
        if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
            std::istringstream iss(value.substr(lb + 1, rb - lb - 1));
            config.audioChannelMap.clear();
            int v; char comma;
            while (iss >> v) { config.audioChannelMap.push_back(v); iss >> comma; }
        }
        return true;
    }
    if (key == "AUDIO_CHANNEL_GAINS") {
        size_t lb = value.find('[');
        size_t rb = value.find(']');
        if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
            std::istringstream iss(value.substr(lb + 1, rb - lb - 1));
            config.audioChannelGains.clear();
            float v; char comma;
            while (iss >> v) { config.audioChannelGains.push_back(v); iss >> comma; }
        }
        return true;
    }
    if (key == "AUDIO_DEVICE") { config.audioDevice = value; return true; }
    if (key == "MIN_VIDEO_LENGTH") { config.minVideoLength = std::stof(value); return true; }
    if (key == "SCALE_SELECT") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        config.scaleSelectEnabled = (v == "1" || v == "true" || v == "yes");
        return true;
    }
    if (key == "RATIO_ROUND_DECIMALS") {
        int decimals = std::stoi(value);
        if (decimals < 0 || decimals > 6) {
            ofLogWarning("ConfigLoader") << "RATIO_ROUND_DECIMALS must be from 0 to 6; using 2";
            decimals = 2;
        }
        config.ratioRoundDecimals = decimals;
        return true;
    }
    if (key == "SECONDARY_WINDOW_ENABLED") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        config.secondaryWindowEnabled = (v == "1" || v == "true" || v == "yes");
        return true;
    }
    if (key == "SECONDARY_WINDOW_WIDTH")  { config.secondaryWindowWidth  = std::stoi(value); return true; }
    if (key == "SECONDARY_WINDOW_HEIGHT") { config.secondaryWindowHeight = std::stoi(value); return true; }
    if (key == "WINDOW_DECORATED") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        config.windowDecorated = (v == "1" || v == "true" || v == "yes");
        return true;
    }
    if (key == "OUTPUT_MODE") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        if (v == "syphon") {
            config.outputMode = OutputMode::Syphon;
        } else if (v == "window") {
            config.outputMode = OutputMode::Window;
        } else {
            ofLogWarning("ConfigLoader") << "Unknown OUTPUT_MODE '" << value << "', falling back to window";
            config.outputMode = OutputMode::Window;
        }
        return true;
    }
    if (key == "SYPHON_NAME") { config.syphonName = value; return true; }
    if (key == "PREVIEW_WIDTH") { config.previewWidth = std::stoi(value); return true; }
    if (key == "PREVIEW_HEIGHT") { config.previewHeight = std::stoi(value); return true; }
    if (key == "IGNORE_FINGERPRINT") {
        std::string v = value;
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        config.ignoreFingerprint = (v == "1" || v == "true" || v == "yes");
        return true;
    }

    if (key == "SELECT") {
        size_t lb = value.find('[');
        size_t rb = value.find(']');
        if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
            std::string inner = trim(value.substr(lb + 1, rb - lb - 1));
            std::string rest = trim(value.substr(rb + 1));
            SelectOption opt;
            if (inner.empty()) {
                // SELECT = [], weight  →  match videos with an empty object list
                opt.matchEmptyList = true;
            } else {
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
        return true;
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
        return true;
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
        return true;
    }
    return false;
}

bool ConfigLoader::readSettingLines(const std::string& fullPath, std::vector<std::string>& lines) {
    std::ifstream f(fullPath);
    if (!f.is_open()) return false;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        lines.push_back(line);
    }
    return true;
}

std::string ConfigLoader::lineKey(const std::string& line) {
    size_t eq = line.find('=');
    return (eq == std::string::npos) ? "" : trim(line.substr(0, eq));
}

bool ConfigLoader::load(const std::string& path, BinSorterConfig& out) {
    const std::string fullPath = ofToDataPath(path, true);
    std::vector<std::string> systemLines;
    if (!readSettingLines(fullPath, systemLines)) {
        ofLogError("ConfigLoader") << "Cannot open system config: " << fullPath;
        return false;
    }

    // Paths in system_config.txt (including WINDOW_CONFIG) are relative to its own folder
    const std::filesystem::path configDir = std::filesystem::path(fullPath).parent_path();

    std::string windowConfigValue;
    bool systemHasSelect = false, systemHasExpandRange = false;
    for (const auto& l : systemLines) {
        const std::string key = lineKey(l);
        if (key == "WINDOW_CONFIG") {
            std::string v = trim(l.substr(l.find('=') + 1));
            size_t hashPos = v.find('#');
            if (hashPos != std::string::npos) v = trim(v.substr(0, hashPos));
            windowConfigValue = v;
        }
        if (key == "SELECT") systemHasSelect = true;
        if (key == "EXPAND_RANGE") systemHasExpandRange = true;
    }
    if (windowConfigValue.empty()) {
        ofLogError("ConfigLoader") << "WINDOW_CONFIG is not set in " << fullPath;
        return false;
    }
    std::filesystem::path windowPath(windowConfigValue);
    if (windowPath.is_relative()) windowPath = (configDir / windowPath).lexically_normal();
    std::vector<std::string> windowLines;
    if (!readSettingLines(windowPath.string(), windowLines)) {
        ofLogError("ConfigLoader") << "Cannot open window config (WINDOW_CONFIG = " << windowConfigValue
            << "): " << windowPath.string();
        return false;
    }

    // Window config first, then system config, so system_config.txt wins for any setting in both.
    // SELECT / EXPAND_RANGE accumulate per line, so if system defines any, the window file's are dropped.
    auto warnUnknown = [](const std::string& line, const std::string& file) {
        static const std::set<std::string> hardCoded = {
            "WINDOW_X", "WINDOW_Y", "VIDEO_LOOP", "TRANSITION_TYPE", "TRANSITION_DURATION_FADE",
            "TRANSITION_DURATION_JUMP_TO_BLACK", "TRANSITION_TIMER_MIN", "TRANSITION_TIMER_MAX", "KEY_VIDEO",
            "SELECT_EXACT_MATCH", "GAP_FILTER_THRESHOLD", "ASPECT_EXPAND_FILTER", "NESTING_LAYERS",
            "NESTED_MIN_SPACE_THRESHOLD", "MAIN_BIN_FILL_CHANCE", "BREAK_BOX_COVERAGE_THRESHOLD",
            "MIN_SPACE_THRESHOLD"};
        const std::string key = lineKey(line);
        const std::string fileName = std::filesystem::path(file).filename().string();
        if (hardCoded.count(key))
            ofLogWarning("ConfigLoader") << "Setting '" << key << "' in " << fileName
                << " is deprecated and hard-coded (see HARD-CODED SETTINGS in configREADME.md); ignored";
        else
            ofLogWarning("ConfigLoader") << "Unknown setting '" << (key.empty() ? line : key) << "' in "
                << fileName << " (ignored; check spelling)";
    };
    for (const auto& l : windowLines) {
        const std::string key = lineKey(l);
        if (key == "WINDOW_CONFIG") {
            ofLogWarning("ConfigLoader") << "WINDOW_CONFIG is only read from system_config.txt; ignored in "
                << windowPath.filename().string();
            continue;
        }
        if (key == "SELECT" && systemHasSelect) continue;
        if (key == "EXPAND_RANGE" && systemHasExpandRange) continue;
        if (!parseLine(l, out)) warnUnknown(l, windowPath.string());
    }
    for (const auto& l : systemLines) {
        if (lineKey(l) == "WINDOW_CONFIG") continue;
        if (!parseLine(l, out)) warnUnknown(l, fullPath);
    }

    ofLogNotice("ConfigLoader") << "Loaded system config " << fullPath << " with window config " << windowPath.string();

    for (std::string* p : {&out.videoAssetPath, &out.videosCsvPath, &out.arrangementsPath, &out.audioPath}) {
        if (!p->empty() && std::filesystem::path(*p).is_relative())
            *p = (configDir / *p).lexically_normal().string();
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
