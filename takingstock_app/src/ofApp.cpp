#include "ofApp.h"
#include "ArrangementIO.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <random>

std::string ofApp::getCurrentKeyVideoClusterNo() const {
    int keyIdx = renderer.getKeyVideoSlotIndex(config.keyVideoMinLength);
    if (keyIdx < 0) return "";
    return renderer.getSlots()[keyIdx].clusterNo;
}

std::string ofApp::findAudioFile(const std::string& clusterNo) const {
    if (config.audioPath.empty() || clusterNo.empty()) return "";
    std::string resolvedPath = ofToDataPath(config.audioPath, true);
    ofDirectory dir(resolvedPath);
    dir.listDir();
    for (const auto& file : dir.getFiles()) {
        if (file.getFileName().find(clusterNo) != std::string::npos) {
            return file.getAbsolutePath();
        }
    }
    return "";
}

void ofApp::startAudioForArrangement(float initialVolume) {
    audioPlayer.stop();
    currentAudioFileName = "";
    if (config.audioPath.empty()) return;

    int keyIdx = config.keyVideo ? renderer.getKeyVideoSlotIndex(config.keyVideoMinLength) : -1;
    if (keyIdx < 0) return;

    const std::string& clusterNo = renderer.getSlots()[keyIdx].clusterNo;
    std::string audioPath = findAudioFile(clusterNo);
    if (audioPath.empty()) {
        ofLogWarning("ofApp") << "Audio: no file found for cluster_no=" << clusterNo << " in " << config.audioPath;
        return;
    }

    audioVolume = initialVolume;
    currentAudioFileName = ofFile(audioPath).getFileName();
    audioPlayer.load(audioPath);
    audioPlayer.setLoop(true);
    audioPlayer.setVolume(audioVolume);
    audioPlayer.play();
}

void ofApp::beginAudioFade(float targetVolume) {
    audioFadeStartTime = ofGetElapsedTimef();
    audioFadeStartVolume = audioVolume;
    audioFadeTargetVolume = targetVolume;
}

void ofApp::updateAudioFade() {
    if (audioFadeStartTime < 0.f) return;
    float elapsed = ofGetElapsedTimef() - audioFadeStartTime;
    float t = (config.audioFadeDuration > 0.f)
        ? std::min(1.f, elapsed / config.audioFadeDuration)
        : 1.f;
    audioVolume = audioFadeStartVolume + (audioFadeTargetVolume - audioFadeStartVolume) * t;
    audioPlayer.setVolume(audioVolume);
    if (t >= 1.f) audioFadeStartTime = -1.f;
}

void ofApp::setup() {
    ofSetBackgroundColor(0, 0, 0);
    if (!ConfigLoader::load("config.txt", config)) {
        ofLogError("ofApp") << "Failed to load config.txt, using defaults";
    }

    videoPool.minDuration = config.minVideoLength;
    if (!videoPool.loadFromCsv(config.videosCsvPath)) {
        ofLogWarning("ofApp") << "No video assets found (check VIDEOS_CSV_PATH), will use colored rects";
    }

    if (!videoPool.hasObjectColumn() && config.selectMode) {
        ofLogWarning("ofApp") << "SELECT_MODE disabled: no 'object' column found in CSV";
        config.selectMode = false;
    }

    if (config.selectMode) {
        ofLogNotice("ofApp") << "SELECT_MODE enabled (match=" << (config.selectExactMatch ? "exact" : "any") << ")";
        for (size_t i = 0; i < config.selectOptions.size(); ++i) {
            const auto& opt = config.selectOptions[i];
            std::string label;
            if (opt.matchEmptyList) {
                label = "[]";
            } else {
                label = "[";
                for (const auto& o : opt.objects) label += (label.size() > 1 ? ", " : "") + o;
                label += "]";
            }
            ofLogNotice("ofApp") << "  Option " << (i + 1) << ": objects=" << label << " weight=" << opt.weight;
        }
    } else {
        ofLogNotice("ofApp") << "SELECT_MODE disabled";
    }

    // Check whether the CSV or video files have changed since the last run.
    // If so, delete any cached arrangement file so it gets regenerated with
    // the updated ratios and weightings.
    std::string currentFingerprint;
    std::string fingerprintPath;
    std::string savedFingerprint;
    if (config.ignoreFingerprint) {
        ofLogNotice("ofApp") << "IGNORE_FINGERPRINT enabled: skipping fingerprint check, reusing any matching arrangement file";
    } else {
        ofLogNotice("ofApp") << "IGNORE_FINGERPRINT disabled: verifying arrangement cache against input fingerprint";
        currentFingerprint = ArrangementIO::computeInputsFingerprint(config.videosCsvPath);
        fingerprintPath = ArrangementIO::getFingerprintPath(config.arrangementsPath, config.boxWidth, config.boxHeight, config.nestingLayers);
        savedFingerprint = ArrangementIO::loadFingerprint(fingerprintPath);
        if (!savedFingerprint.empty() && savedFingerprint != currentFingerprint) {
            ofLogNotice("ofApp") << "Input files have changed (videos or CSV), clearing cached arrangements";
            std::string oldPath = ArrangementIO::findArrangementPath(config.arrangementsPath, config.boxWidth, config.boxHeight, config.nestingLayers);
            if (!oldPath.empty()) {
                ofFile(oldPath).remove(false);
                ofLogNotice("ofApp") << "Deleted stale arrangement cache: " << oldPath;
            }
        }
    }

    // Derive size ratios from the CSV: weight each ratio by its video count (normalized per config)
    auto ratioCounts = videoPool.getRatioCounts();
    int totalVideos = 0;
    for (auto& [ratio, count] : ratioCounts) totalVideos += count;
    ofLogNotice("ofApp") << "Videos loaded: " << totalVideos << " total, " << ratioCounts.size() << " ratio(s)";
    for (auto& [ratio, count] : ratioCounts) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3) << ratio;
        ofLogNotice("ofApp") << "  ratio " << oss.str() << " -> " << count << " videos";
        float w_norm;
        switch (config.weightNormalization) {
            case WeightNormalization::Raw:   w_norm = (float)count; break;
            case WeightNormalization::Equal: w_norm = 1.0f; break;
            case WeightNormalization::Sqrt:
            default:                         w_norm = std::sqrt((float)count); break;
        }
        int w = (int)std::round(ratio * 1000.f);

        const auto& fb = config.expandFallback;
        float eTop = fb[0], eRight = fb[1], eBot = fb[2], eLeft = fb[3];
        int matchCount = 0;
        for (const auto& er : config.expandRanges) {
            if (ratio >= er.minRatio && ratio <= er.maxRatio) {
                if (matchCount == 0) { eTop = er.top; eRight = er.right; eBot = er.bottom; eLeft = er.left; }
                matchCount++;
            }
        }
        if (matchCount > 1) {
            ofLogWarning("ofApp") << "ratio " << oss.str() << " matched " << matchCount
                << " EXPAND_RANGEs; using first match — consider fixing overlapping ranges";
        }

        config.sizeRatios.push_back(SizeRatio(w, 1000, w_norm, eTop, eRight, eBot, eLeft));
    }
    if (config.sizeRatios.empty()) {
        ofLogWarning("ofApp") << "No ratios found in CSV, falling back to 1:1";
        const auto& fb = config.expandFallback;
        config.sizeRatios.push_back(SizeRatio(1000, 1000, 1.0f, fb[0], fb[1], fb[2], fb[3]));
    }

    binSorter = std::make_unique<BinSorter>(config.boxWidth, config.boxHeight, config.sizeRatios,
                         config.packingStopArea, config.nestingLayers,
                         config.nestedMinSpaceThreshold, config.mainBinFillChance,
                         config.itemBreakScale, config.itemBreakChance,
                         config.breakBoxMinItems, config.breakBoxMaxItems,
                         config.breakBoxFillAttempts, config.breakBoxCoverageThreshold,
                         config.placementAreaExponent, config.placementTopK);

    std::string arrangementPath = ArrangementIO::findArrangementPath(config.arrangementsPath, config.boxWidth, config.boxHeight, config.nestingLayers);

    if (!arrangementPath.empty() && ArrangementIO::load(arrangementPath, arrangements)) {
        auto it = std::remove_if(arrangements.begin(), arrangements.end(),
            [this](const Arrangement& a) {
                if (!ArrangementIO::isValidArrangement(a, config.boxWidth, config.boxHeight))
                    return true;
                if (config.gapFilterThreshold >= 0) {
                    binSorter->loadArrangement(a.bins, a.nestedBins);
                    int maxGap = binSorter->getLargestFittableAreaInLayout();
                    int threshold = (config.gapFilterThreshold == 0) ? 1 : config.gapFilterThreshold;
                    if (maxGap >= threshold) return true;
                }
                if (config.aspectExpandFilter &&
                    !binSorter->arrangementAspectWithinExpandTolerance(a.bins, a.nestedBins))
                    return true;
                if (binSorter->hasMutualEdgeOverflow(a.bins, a.nestedBins)) {
                    ofLogNotice("ofApp") << "Discarding cached arrangement: mutual edge overflow at shared boundary";
                    return true;
                }
                return false;
            });
        arrangements.erase(it, arrangements.end());
        if (!arrangements.empty()) {
            std::string tail;
            if (config.gapFilterThreshold >= 0 && config.aspectExpandFilter)
                tail = " (filtered by gap + aspect-expand)";
            else if (config.gapFilterThreshold >= 0)
                tail = " (filtered by gap threshold)";
            else if (config.aspectExpandFilter)
                tail = " (filtered by aspect-expand)";
            ofLogNotice("ofApp") << "Loaded " << arrangements.size() << " arrangements from disk" << tail;
            // First run with fingerprinting: adopt current fingerprint so future
            // runs only regenerate when inputs actually change.
            if (!config.ignoreFingerprint && savedFingerprint.empty())
                ArrangementIO::saveFingerprint(fingerprintPath, currentFingerprint);
        }
        if (arrangements.empty()) {
            ofLogWarning("ofApp") << "All loaded arrangements were invalid or exceeded gap threshold, generating new";
        }
    }
    if (arrangementPath.empty() || arrangements.empty()) {
        // Precompute arrangements with multiple phases (different seeds explore different regions)
        arrangements.clear();
        std::set<std::string> seenSignatures;

        for (int phase = 0; phase < config.layoutPhases; ++phase) {
            ofSetRandomSeed((unsigned long)(phase + 1));  // different seed per phase
            int phaseStartCount = (int)arrangements.size();
            int attempts = 0;
            int staleCount = 0;
            int mutualOverlapDiscarded = 0;

            while (attempts < config.layoutMaxAttempts) {
                binSorter->sort(-1);
                std::string sig = binSorter->getLayoutSignature();
                if (seenSignatures.insert(sig).second) {
                    Arrangement arr;
                    arr.bins = binSorter->getBins();
                    arr.nestedBins = binSorter->getNestedBins();
                    if (!ArrangementIO::isValidArrangement(arr, config.boxWidth, config.boxHeight)) {
                        staleCount++;
                        if (staleCount >= config.layoutStaleThreshold) break;
                    } else if (config.gapFilterThreshold >= 0) {
                        int maxGap = binSorter->getLargestFittableAreaInLayout();
                        int threshold = (config.gapFilterThreshold == 0) ? 1 : config.gapFilterThreshold;
                        if (maxGap >= threshold) {
                            staleCount++;
                            if (staleCount >= config.layoutStaleThreshold) break;
                        } else if (config.aspectExpandFilter &&
                            !binSorter->arrangementAspectWithinExpandTolerance(arr.bins, arr.nestedBins)) {
                            staleCount++;
                            if (staleCount >= config.layoutStaleThreshold) break;
                        } else if (binSorter->hasMutualEdgeOverflow(arr.bins, arr.nestedBins)) {
                            mutualOverlapDiscarded++;
                            staleCount++;
                            if (staleCount >= config.layoutStaleThreshold) break;
                        } else {
                            arrangements.push_back(arr);
                            staleCount = 0;
                            if (arrangements.size() % 10 == 0) {
                                ofLogNotice("ofApp") << "Arrangements: " << arrangements.size();
                            }
                        }
                    } else if (config.aspectExpandFilter &&
                        !binSorter->arrangementAspectWithinExpandTolerance(arr.bins, arr.nestedBins)) {
                        staleCount++;
                        if (staleCount >= config.layoutStaleThreshold) break;
                    } else if (binSorter->hasMutualEdgeOverflow(arr.bins, arr.nestedBins)) {
                        mutualOverlapDiscarded++;
                        staleCount++;
                        if (staleCount >= config.layoutStaleThreshold) break;
                    } else {
                        arrangements.push_back(arr);
                        staleCount = 0;
                        if (arrangements.size() % 10 == 0) {
                            ofLogNotice("ofApp") << "Arrangements: " << arrangements.size();
                        }
                    }
                } else {
                    staleCount++;
                    if (staleCount >= config.layoutStaleThreshold) break;
                }
                attempts++;
            }

            int phaseNew = (int)arrangements.size() - phaseStartCount;
            ofLogNotice("ofApp") << "Phase " << (phase + 1) << ": found " << phaseNew << " new (total "
                << arrangements.size() << ")"
                << (mutualOverlapDiscarded > 0 ? ", discarded " + std::to_string(mutualOverlapDiscarded) + " for mutual edge overlap" : "");
        }

        ofLogNotice("ofApp") << "Finished: " << arrangements.size() << " unique arrangements";

        if (!arrangements.empty()) {
            std::string savePath = ArrangementIO::getArrangementPath(config.arrangementsPath, config.boxWidth, config.boxHeight, config.nestingLayers, (int)arrangements.size());
            ArrangementIO::save(savePath, arrangements);
            if (!config.ignoreFingerprint)
                ArrangementIO::saveFingerprint(fingerprintPath, currentFingerprint);
        }
    }

    if (arrangements.empty()) {
        ofLogError("ofApp") << "No arrangements found, using single sort result";
        binSorter->sort(-1);
    }

    pickQueue.clear();
    for (size_t i = 0; i < arrangements.size(); ++i) pickQueue.push_back(i);
    std::shuffle(pickQueue.begin(), pickQueue.end(), std::mt19937(std::random_device{}()));

    pickSelectAndApplyFilter();
    currentSelectFilterLabel = nextSelectFilterLabel;
    size_t initialIdx = 0;
    if (!arrangements.empty()) {
        initialIdx = pickNextArrangementIndex();
        binSorter->loadArrangement(arrangements[initialIdx].bins, arrangements[initialIdx].nestedBins);
    }
    renderer.setup(binSorter.get(), &videoPool, config.videoLoop, config.keyVideo, config.keyVideoMinLength);
    startAudioForArrangement(1.f);

    ofSetWindowShape(config.boxWidth, config.boxHeight);
    exportFbo.allocate(config.boxWidth, config.boxHeight, GL_RGB);

    if (arrangements.size() > 1) {
        pickSelectAndApplyFilter();
        size_t maxAttempts = arrangements.size();
        for (size_t attempt = 0; attempt < maxAttempts; ++attempt) {
            nextLayoutIdx = pickNextArrangementIndex();
            videoPool.resetUsed();
            bool isLastAttempt = (attempt + 1 == maxAttempts);
            if (renderer.preloadFromArrangement(arrangements[nextLayoutIdx], isLastAttempt))
                break;
            ofLogNotice("ofApp") << "Layout " << (nextLayoutIdx + 1)
                << " skipped: would repeat a video; trying next";
        }
    }
    if (!arrangements.empty()) {
        ofLogNotice("ofApp") << "---";
        logArrangementInfo(initialIdx);
    }
    scheduleNextTransition();
}

void ofApp::logArrangementInfo(size_t idx) {
    if (arrangements.empty() || idx >= arrangements.size()) return;
    std::string timerStr = "";
    if (config.keyVideo) {
        int ki = renderer.getKeyVideoSlotIndex(config.keyVideoMinLength);
        if (ki >= 0) {
            float dur = renderer.getSlots()[ki].duration;
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << dur;
            timerStr = " | next_transition_timer=" + oss.str() + "s";
        }
    }
    ofLogNotice("ofApp") << "Picked arrangement " << (idx + 1) << " of " << arrangements.size() << timerStr;
    if (config.selectMode && !currentSelectFilterLabel.empty())
        ofLogNotice("ofApp") << "Select filter: " << currentSelectFilterLabel;
    const auto& slots = renderer.getSlots();
    bool hasBreakBox = !arrangements[idx].nestedBins.empty();
    ofLogNotice("ofApp") << "Window: " << config.boxWidth << " x " << config.boxHeight
        << " | nestingLayers=" << config.nestingLayers
        << " | breakBox=" << (hasBreakBox ? "yes" : "no");
    if (hasBreakBox) {
        const auto& bins = arrangements[idx].bins;
        const auto& nestedBins = arrangements[idx].nestedBins;
        int bbNum = 0;
        for (const auto& kv : nestedBins) {
            int bi = kv.first.first;
            int parentIdx = kv.first.second;
            int nx = kv.second.parentX, ny = kv.second.parentY;
            int nw = kv.second.parentW, nh = kv.second.parentH;
            if (bi >= 0 && bi < (int)bins.size()) {
                for (const auto& pit : bins[bi]) {
                    if (pit.itemIdx == parentIdx) {
                        nx = pit.x; ny = pit.y; nw = pit.w; nh = pit.h;
                        break;
                    }
                }
            }
            int nItems = (int)kv.second.items.size();
            ofLogNotice("ofApp") << "  breakBox " << (++bbNum) << ": " << nItems << " items"
                << " at (" << nx << "," << ny << ") size " << nw << "x" << nh;
        }
    }
    int keyVideoIdx = config.keyVideo ? renderer.getKeyVideoSlotIndex(config.keyVideoMinLength) : -1;
    for (size_t i = 0; i < slots.size(); ++i) {
        const auto& s = slots[i];
        bool outOfBounds = (s.x + s.w > config.boxWidth || s.y + s.h > config.boxHeight ||
                            s.x < 0 || s.y < 0);
        bool isKeyVideo = (keyVideoIdx >= 0 && (int)i == keyVideoIdx);
        float slotRatio = (s.ratioH > 0) ? (float)s.ratioW / s.ratioH : 0.f;
        float calcRatio = (s.h > 0) ? (float)s.w / s.h : 0.f;
        ofLogNotice("ofApp") << "  Slot " << (i + 1) << ": x=" << s.x << " y=" << s.y
            << " w=" << s.w << " h=" << s.h
            << " slot_ratio=" << std::fixed << std::setprecision(3) << slotRatio
            << " calc_ratio=" << calcRatio << std::defaultfloat << std::setprecision(6)
            << " right=" << (s.x + s.w)
            << " bottom=" << (s.y + s.h)
            << " duration=" << (int)s.duration << "s"
            << (outOfBounds ? " [OUT OF BOUNDS]" : "")
            << (isKeyVideo ? " [KEY VIDEO]" : "");
    }
}

void ofApp::pickSelectAndApplyFilter() {
    SelectOption wildcardOpt;  // empty objects, matchEmptyList=false → passes all videos
    if (!config.selectMode || config.selectOptions.empty()) {
        videoPool.setObjectFilter(wildcardOpt, false);
        nextSelectFilterLabel = "all objects";
        ofLogNotice("ofApp") << "Select filter: all objects";
        return;
    }
    float totalWeight = 0.f;
    for (const auto& opt : config.selectOptions)
        totalWeight += opt.weight;
    if (totalWeight <= 0.f) {
        videoPool.setObjectFilter(wildcardOpt, false);
        nextSelectFilterLabel = "all objects";
        ofLogNotice("ofApp") << "Select filter: all objects";
        return;
    }

    auto applyAndLog = [&](const SelectOption& opt) {
        videoPool.setObjectFilter(opt, config.selectExactMatch);
        bool isWildcard = !opt.matchEmptyList && (opt.objects.empty() ||
            std::find(opt.objects.begin(), opt.objects.end(), "*") != opt.objects.end());
        if (isWildcard) {
            nextSelectFilterLabel = "all objects";
            ofLogNotice("ofApp") << "Select filter: all objects";
        } else if (opt.matchEmptyList) {
            nextSelectFilterLabel = "objects=[]";
            ofLogNotice("ofApp") << "Select filter: objects=[] (empty list)";
        } else {
            std::string objs;
            for (const auto& o : opt.objects) objs += (objs.empty() ? "" : ", ") + o;
            std::string mode = config.selectExactMatch ? " (exact)" : " (any)";
            nextSelectFilterLabel = "objects=[" + objs + "]";
            ofLogNotice("ofApp") << "Select filter: objects=[" << objs << "]" << mode;
        }
    };

    float r = ofRandom(0.f, totalWeight);
    for (const auto& opt : config.selectOptions) {
        r -= opt.weight;
        if (r < 0.f) {
            applyAndLog(opt);
            return;
        }
    }
    applyAndLog(config.selectOptions.back());
}

void ofApp::swapToPreloadedAndLog(size_t idx, bool deferPlay) {
    if (arrangements.empty() || idx >= arrangements.size()) return;
    if (!renderer.hasPreloadedLayout()) {
        pickAndLoadArrangement(idx);
        return;
    }
    currentSelectFilterLabel = nextSelectFilterLabel;
    renderer.swapToPreloaded(arrangements[idx]);
    if (!deferPlay) renderer.startPlaying();
    logArrangementInfo(idx);
}

void ofApp::preloadNextLayout() {
    if (arrangements.size() <= 1) return;
    pickSelectAndApplyFilter();
    // Retry through all arrangements to find one with no duplicate videos in a scene.
    // On the final attempt, allow duplicates as a fallback so preload always succeeds.
    size_t maxAttempts = arrangements.size();
    for (size_t attempt = 0; attempt < maxAttempts; ++attempt) {
        nextLayoutIdx = pickNextArrangementIndex();
        videoPool.resetUsed();
        bool isLastAttempt = (attempt + 1 == maxAttempts);
        if (renderer.preloadFromArrangement(arrangements[nextLayoutIdx], isLastAttempt))
            return;
        ofLogNotice("ofApp") << "Layout " << (nextLayoutIdx + 1)
            << " skipped: would repeat a video; trying next";
    }
}

void ofApp::pickAndLoadArrangement(size_t idx) {
    if (arrangements.empty() || idx >= arrangements.size()) return;
    ofLogNotice("ofApp") << "---";
    pickSelectAndApplyFilter();
    currentSelectFilterLabel = nextSelectFilterLabel;
    binSorter->loadArrangement(arrangements[idx].bins, arrangements[idx].nestedBins);
    renderer.regenerate();
    std::string timerStr = "";
    if (config.keyVideo) {
        int ki = renderer.getKeyVideoSlotIndex(config.keyVideoMinLength);
        if (ki >= 0) {
            float dur = renderer.getSlots()[ki].duration;
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << dur;
            timerStr = " | next_transition_timer=" + oss.str() + "s";
        }
    }
    ofLogNotice("ofApp") << "Picked arrangement " << (idx + 1) << " of " << arrangements.size() << timerStr;
    if (config.selectMode && !currentSelectFilterLabel.empty())
        ofLogNotice("ofApp") << "Select filter: " << currentSelectFilterLabel;
    const auto& slots = renderer.getSlots();
    bool hasBreakBox = !arrangements[idx].nestedBins.empty();
    ofLogNotice("ofApp") << "Window: " << config.boxWidth << " x " << config.boxHeight
        << " | nestingLayers=" << config.nestingLayers
        << " | breakBox=" << (hasBreakBox ? "yes" : "no");
    if (hasBreakBox) {
        const auto& bins = arrangements[idx].bins;
        const auto& nestedBins = arrangements[idx].nestedBins;
        int bbNum = 0;
        for (const auto& kv : nestedBins) {
            int bi = kv.first.first;
            int parentIdx = kv.first.second;
            int nx = kv.second.parentX, ny = kv.second.parentY;
            int nw = kv.second.parentW, nh = kv.second.parentH;
            if (bi >= 0 && bi < (int)bins.size()) {
                for (const auto& pit : bins[bi]) {
                    if (pit.itemIdx == parentIdx) {
                        nx = pit.x; ny = pit.y; nw = pit.w; nh = pit.h;
                        break;
                    }
                }
            }
            int nItems = (int)kv.second.items.size();
            ofLogNotice("ofApp") << "  breakBox " << (++bbNum) << ": " << nItems << " items"
                << " at (" << nx << "," << ny << ") size " << nw << "x" << nh;
        }
    }
    int keyVideoIdx = config.keyVideo ? renderer.getKeyVideoSlotIndex(config.keyVideoMinLength) : -1;
    for (size_t i = 0; i < slots.size(); ++i) {
        const auto& s = slots[i];
        bool outOfBounds = (s.x + s.w > config.boxWidth || s.y + s.h > config.boxHeight ||
                            s.x < 0 || s.y < 0);
        bool isKeyVideo = (keyVideoIdx >= 0 && (int)i == keyVideoIdx);
        float slotRatio = (s.ratioH > 0) ? (float)s.ratioW / s.ratioH : 0.f;
        float calcRatio = (s.h > 0) ? (float)s.w / s.h : 0.f;
        ofLogNotice("ofApp") << "  Slot " << (i + 1) << ": x=" << s.x << " y=" << s.y
            << " w=" << s.w << " h=" << s.h
            << " slot_ratio=" << std::fixed << std::setprecision(3) << slotRatio
            << " calc_ratio=" << calcRatio << std::defaultfloat << std::setprecision(6)
            << " right=" << (s.x + s.w)
            << " bottom=" << (s.y + s.h)
            << " duration=" << (int)s.duration << "s"
            << (outOfBounds ? " [OUT OF BOUNDS]" : "")
            << (isKeyVideo ? " [KEY VIDEO]" : "");
    }
}

bool ofApp::arrangementCompatibleWithFilter(const Arrangement& arr) const {
    for (size_t bi = 0; bi < arr.bins.size(); ++bi) {
        for (const auto& it : arr.bins[bi]) {
            auto nested = arr.nestedBins.find({(int)bi, it.itemIdx});
            if (nested != arr.nestedBins.end()) {
                for (const auto& nit : nested->second.items) {
                    int wr = 0, hr = 0;
                    binSorter->getItemRatio(nit.w, nit.h, wr, hr);
                    if (!videoPool.hasVideosFor(wr, hr)) return false;
                }
            } else {
                int wr = 0, hr = 0;
                binSorter->getItemRatio(it.w, it.h, wr, hr);
                if (!videoPool.hasVideosFor(wr, hr)) return false;
            }
        }
    }
    return true;
}

float ofApp::scheduleNextTransition() {
    if (config.keyVideo) {
        int keyIdx = renderer.getKeyVideoSlotIndex(config.keyVideoMinLength);
        if (keyIdx >= 0) {
            const auto& keySlot = renderer.getSlots()[keyIdx];
            float keyDur = keySlot.duration;
            nextTransitionTime = ofGetElapsedTimef() + keyDur;
            ofLogNotice("ofApp") << "Key video: slot " << (keyIdx + 1)
                << " | duration=" << std::fixed << std::setprecision(1) << keyDur << "s"
                << " | cluster_no=" << keySlot.clusterNo
                << " | " << ofFile(keySlot.path).getFileName();
            if (!currentAudioFileName.empty())
                ofLogNotice("ofApp") << "Audio: playing " << currentAudioFileName;
            ofLogNotice("ofApp") << "---";
            return keyDur;
        }
        ofLogWarning("ofApp") << "KEY_VIDEO enabled but no qualifying video found "
            << "(minLength=" << config.keyVideoMinLength << "s), falling back to random timer";
    }
    float minT = config.transitionTimerMin;
    float maxT = config.transitionTimerMax;
    if (maxT < minT) maxT = minT;
    float delay = ofRandom(minT, maxT);
    nextTransitionTime = ofGetElapsedTimef() + delay;
    ofLogNotice("ofApp") << "---";
    return delay;
}

size_t ofApp::pickNextArrangementIndex() {
    if (arrangements.empty()) return 0;
    if (pickQueue.empty()) {
        for (size_t i = 0; i < arrangements.size(); ++i) pickQueue.push_back(i);
        std::shuffle(pickQueue.begin(), pickQueue.end(), std::mt19937(std::random_device{}()));
        if (config.cycleResetDuration > 0.f)
            endOfCycleResetPending = true;
    }
    if (config.selectMode) {
        for (auto it = pickQueue.begin(); it != pickQueue.end(); ++it) {
            if (arrangementCompatibleWithFilter(arrangements[*it])) {
                size_t idx = *it;
                pickQueue.erase(it);
                return idx;
            }
        }
        ofLogWarning("ofApp") << "SELECT_MODE: no arrangement compatible with current filter, using any";
    }
    size_t idx = pickQueue.back();
    pickQueue.pop_back();
    return idx;
}

void ofApp::update() {
    float now = ofGetElapsedTimef();
    ofSoundUpdate();
    updateAudioFade();

    if (transitionState == TransitionState::Idle) {
        bool trigger = arrangementPickRequested || (now >= nextTransitionTime);
        bool preloadReady = arrangements.size() <= 1 || renderer.isPreloadComplete();

        if (trigger && !arrangements.empty()) {
            if (!preloadReady) {
                if (preloadWaitStartTime < 0.f) preloadWaitStartTime = now;
                float timeout = renderer.getSlots().size() * 0.5f + 15.f;
                if (now - preloadWaitStartTime > timeout) {
                    ofLogWarning("ofApp") << "Preload stalled for " << (now - preloadWaitStartTime)
                        << "s (timeout=" << timeout << "s), forcing transition";
                    preloadReady = true;
                }
            }
            if (preloadReady) {
                preloadWaitStartTime = -1.f;
                arrangementPickRequested = false;

                if (config.transitionType == TransitionType::Jumpcut) {
                    ofLogNotice("ofApp") << "---";
                    swapToPreloadedAndLog(nextLayoutIdx);
                    startAudioForArrangement(1.f);
                    preloadNextLayout();
                    scheduleNextTransition();
                    if (endOfCycleResetPending) {
                        endOfCycleResetPending = false;
                        transitionState = TransitionState::CycleReset;
                        transitionStartTime = now;
                        ofLogNotice("ofApp") << "XXXXXXXXXXX";
                        ofLogNotice("ofApp") << "Cycle reset: all " << arrangements.size() << " arrangements shown, holding black for " << config.cycleResetDuration << "s";
                        ofLogNotice("ofApp") << "XXXXXXXXXXX";
                    }
                } else if (config.transitionType == TransitionType::Fade) {
                    transitionState = TransitionState::FadeDown;
                    transitionStartTime = now;
                    beginAudioFade(0.f);
                } else {
                    transitionState = TransitionState::HoldBlack;
                    transitionStartTime = now;
                }
            }
        } else if (!trigger) {
            preloadWaitStartTime = -1.f;
        }
    } else if (transitionState == TransitionState::FadeDown) {
        float dur = std::max(0.016f, config.transitionDurationFade);
        if (now - transitionStartTime >= dur) {
            transitionState = TransitionState::FadeHoldBlack;
            transitionStartTime = now;
            fadeHoldBlackSwapDone = false;
        }
    } else if (transitionState == TransitionState::FadeHoldBlack) {
        if (!fadeHoldBlackSwapDone) {
            ofLogNotice("ofApp") << "---";
            swapToPreloadedAndLog(nextLayoutIdx, true);
            startAudioForArrangement(0.f);
            beginAudioFade(1.f);
            fadeHoldBlackSwapDone = true;
        } else {
            renderer.startPlaying();
            transitionState = TransitionState::FadeUp;
            transitionStartTime = now;
        }
    } else if (transitionState == TransitionState::HoldBlack) {
        float dur = std::max(0.016f, config.transitionDurationJumpToBlack);
        if (now - transitionStartTime >= dur) {
            ofLogNotice("ofApp") << "---";
            swapToPreloadedAndLog(nextLayoutIdx);
            startAudioForArrangement(1.f);
            preloadNextLayout();
            scheduleNextTransition();
            if (endOfCycleResetPending) {
                endOfCycleResetPending = false;
                transitionState = TransitionState::CycleReset;
                transitionStartTime = now;
                ofLogNotice("ofApp") << "XXXXXXXXXXX";
                ofLogNotice("ofApp") << "Cycle reset: all " << arrangements.size() << " arrangements shown, holding black for " << config.cycleResetDuration << "s";
                ofLogNotice("ofApp") << "XXXXXXXXXXX";
            } else {
                transitionState = TransitionState::Idle;
            }
        }
    } else if (transitionState == TransitionState::FadeUp) {
        float dur = std::max(0.016f, config.transitionDurationFade);
        if (now - transitionStartTime >= dur) {
            scheduleNextTransition();
            preloadNextLayout();
            if (endOfCycleResetPending) {
                endOfCycleResetPending = false;
                transitionState = TransitionState::CycleReset;
                transitionStartTime = now;
                ofLogNotice("ofApp") << "XXXXXXXXXXX";
                ofLogNotice("ofApp") << "Cycle reset: all " << arrangements.size() << " arrangements shown, holding black for " << config.cycleResetDuration << "s";
                ofLogNotice("ofApp") << "XXXXXXXXXXX";
            } else {
                transitionState = TransitionState::Idle;
            }
        }
    } else if (transitionState == TransitionState::CycleReset) {
        if (now - transitionStartTime >= config.cycleResetDuration) {
            scheduleNextTransition();
            transitionState = TransitionState::Idle;
            ofLogNotice("ofApp") << "XXXXXXXXXXX";
            ofLogNotice("ofApp") << "Cycle reset complete (" << config.cycleResetDuration << "s), resuming";
            ofLogNotice("ofApp") << "XXXXXXXXXXX";
        }
    }

    renderer.update();
}

void ofApp::draw() {
    ofBackground(0);

    if (transitionState == TransitionState::HoldBlack || transitionState == TransitionState::FadeHoldBlack || transitionState == TransitionState::CycleReset) {
        ofFill();
        ofSetColor(0);
        ofDrawRectangle(0, 0, ofGetWindowWidth(), ofGetWindowHeight());
    } else if (transitionState == TransitionState::FadeUp) {
        renderer.draw(0, 0);
        float dur = std::max(0.016f, config.transitionDurationFade);
        float elapsed = ofGetElapsedTimef() - transitionStartTime;
        float t = std::min(1.f, elapsed / dur);
        ofEnableAlphaBlending();
        ofFill();
        ofSetColor(0, 0, 0, (int)((1.f - t) * 255));
        ofDrawRectangle(0, 0, ofGetWindowWidth(), ofGetWindowHeight());
        ofDisableAlphaBlending();
    } else {
        renderer.draw(0, 0);

        if (transitionState == TransitionState::FadeDown) {
            float dur = std::max(0.016f, config.transitionDurationFade);
            float elapsed = ofGetElapsedTimef() - transitionStartTime;
            float t = std::min(1.f, elapsed / dur);
            ofEnableAlphaBlending();
            ofFill();
            ofSetColor(0, 0, 0, (int)(t * 255));
            ofDrawRectangle(0, 0, ofGetWindowWidth(), ofGetWindowHeight());
            ofDisableAlphaBlending();
        }
    }

    if (exportRequested) {
        renderer.drawToFbo(exportFbo);
        ofPixels pixels;
        exportFbo.readToPixels(pixels);
        ofImage img;
        img.setFromPixels(pixels);
        img.save("bin_sorter_export.png");
        ofLogNotice("ofApp") << "Exported to bin_sorter_export.png";
        exportRequested = false;
    }
}

void ofApp::keyPressed(int key) {
    if (key == 's' || key == 'S') {
        exportRequested = true;
    } else if (key == 'r' || key == 'R') {
        arrangementPickRequested = true;
    }
}
