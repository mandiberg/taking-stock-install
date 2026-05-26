#include "BinSorterRenderer.h"
#include "VideoAssetPool.h"
#include "ofMain.h"
#include <algorithm>
#include <iterator>
#include <queue>
#include <set>

static void buildSlotsImpl(BinSorter* binSorter, VideoAssetPool* videoPool, bool videoLoop,
    const std::vector<std::vector<BinItem>>& bins,
    const std::map<std::pair<int, int>, NestedBinData>& nestedBins,
    std::vector<VideoSlot>& out, bool quiet, bool deferPlay = false,
    bool deferLoad = false, const char* arrangementContext = "current arrangement",
    bool keyVideo = false, float keyVideoMinLength = 0.f) {
    out.clear();
    if (!binSorter) return;

    int boxW = binSorter->getBoxWidth();
    const int padding = 20;

    // Pass 1: assign paths from CSV pool data — no file loading yet
    for (size_t bi = 0; bi < bins.size(); ++bi) {
        float baseX = bi * (boxW + padding);
        float baseY = 0;
        for (const auto& it : bins[bi]) {
            auto itNested = nestedBins.find({(int)bi, it.itemIdx});
            if (itNested != nestedBins.end()) {
                for (const auto& nit : itNested->second.items) {
                    int wr = 0, hr = 0;
                    binSorter->getItemRatio(nit.w, nit.h, wr, hr);
                    VideoEntry entry = videoPool ? videoPool->getVideoEntry(wr, hr) : VideoEntry{};
                    VideoSlot slot;
                    slot.x = (int)(baseX + it.x + nit.x);
                    slot.y = (int)(baseY + it.y + nit.y);
                    slot.w = nit.w;
                    slot.h = nit.h;
                    slot.ratioW = wr;
                    slot.ratioH = hr;
                    slot.path = entry.fullPath;
                    slot.nextPath = (!videoLoop && videoPool && !entry.fullPath.empty()) ? videoPool->getVideoPath(wr, hr) : "";
                    slot.clusterNo = entry.clusterNo;
                    slot.duration = entry.duration;
                    slot.hasVideo = !slot.path.empty();
                    out.push_back(std::move(slot));
                }
            } else {
                int wr = 0, hr = 0;
                binSorter->getItemRatio(it.w, it.h, wr, hr);
                VideoEntry entry = videoPool ? videoPool->getVideoEntry(wr, hr) : VideoEntry{};
                VideoSlot slot;
                slot.x = (int)(baseX + it.x);
                slot.y = (int)(baseY + it.y);
                slot.w = it.w;
                slot.h = it.h;
                slot.ratioW = wr;
                slot.ratioH = hr;
                slot.path = entry.fullPath;
                slot.nextPath = (!videoLoop && videoPool && !entry.fullPath.empty()) ? videoPool->getVideoPath(wr, hr) : "";
                slot.clusterNo = entry.clusterNo;
                slot.duration = entry.duration;
                slot.hasVideo = !slot.path.empty();
                out.push_back(std::move(slot));
            }
        }
    }

    // Pass 2: key video check and replacement (CSV only, no file I/O)
    // If no assigned video meets the minimum duration, try swapping one slot's video
    // with a qualifying one from the pool. Stops at the first successful replacement.
    if (keyVideo && keyVideoMinLength > 0.f && videoPool) {
        bool hasQualifying = false;
        for (const auto& slot : out) {
            if (slot.hasVideo && slot.duration >= keyVideoMinLength) { hasQualifying = true; break; }
        }
        if (!hasQualifying) {
            for (size_t i = 0; i < out.size(); ++i) {
                auto& slot = out[i];
                if (!slot.hasVideo) continue;
                VideoEntry qual = videoPool->getVideoEntryWithMinDuration(slot.ratioW, slot.ratioH, keyVideoMinLength);
                if (!qual.fullPath.empty()) {
                    ofLogNotice("BinSorterRenderer") << "KEY_VIDEO: slot " << (i + 1)
                        << " swapped to qualifying video (duration=" << qual.duration << "s)"
                        << " [" << arrangementContext << "]";
                    slot.path = qual.fullPath;
                    slot.duration = qual.duration;
                    slot.clusterNo = qual.clusterNo;
                    hasQualifying = true;
                    break;
                }
            }
            if (!hasQualifying) {
                ofLogWarning("BinSorterRenderer") << "KEY_VIDEO: no video with duration>="
                    << keyVideoMinLength << "s available for any slot ratio"
                    << " [" << arrangementContext << "]";
            }
        }
    }

    // Pass 3: load players (skipped when deferLoad — staggered load handles it later)
    if (!deferLoad) {
        for (size_t i = 0; i < out.size(); ++i) {
            auto& slot = out[i];
            if (slot.hasVideo) {
                if (slot.player.load(slot.path)) {
                    ofLogNotice("BinSorterRenderer") << "Video load [" << arrangementContext << "] " << slot.path;
                    slot.player.setLoopState(videoLoop ? OF_LOOP_NORMAL : OF_LOOP_NONE);
                    if (!deferPlay) slot.player.play();
                    if (!videoLoop && !slot.nextPath.empty() && slot.nextPlayer.load(slot.nextPath)) {
                        ofLogNotice("BinSorterRenderer") << "Video load [" << arrangementContext << "] " << slot.nextPath;
                        slot.nextPlayer.setLoopState(OF_LOOP_NONE);
                        slot.nextPlayer.setPosition(0);
                    }
                } else {
                    slot.hasVideo = false;
                    if (!quiet) ofLogWarning("BinSorterRenderer") << "load failed for ratio "
                        << slot.ratioW << "_" << slot.ratioH << ": " << slot.path;
                }
            }
            if (!quiet) {
                float slotAspect = (slot.h > 0) ? (float)slot.w / slot.h : 0;
                ofLogNotice("BinSorterRenderer") << "Slot " << (i + 1) << ": "
                    << slot.w << "x" << slot.h << " aspect=" << slotAspect
                    << " -> ratio " << slot.ratioW << ":" << slot.ratioH
                    << (slot.path.empty() ? "" : " ") << slot.path
                    << " [" << arrangementContext << "]";
            }
        }
    }
}

void BinSorterRenderer::setup(BinSorter* sorter, VideoAssetPool* pool, bool videoLoop_,
                              bool keyVideo_, float keyVideoMinLength_) {
    binSorter = sorter;
    videoPool = pool;
    videoLoop = videoLoop_;
    keyVideo = keyVideo_;
    keyVideoMinLength = keyVideoMinLength_;
    if (videoPool) videoPool->resetUsed();
    buildSlots();
    drawOrderDirty = true;
}

void BinSorterRenderer::buildSlots() {
    if (!binSorter) return;
    buildSlotsFromArrangement(binSorter->getBins(), binSorter->getNestedBins(), slots);
}

void BinSorterRenderer::buildSlotsFromArrangement(const std::vector<std::vector<BinItem>>& bins,
    const std::map<std::pair<int, int>, NestedBinData>& nestedBins,
    std::vector<VideoSlot>& out) {
    buildSlotsImpl(binSorter, videoPool, videoLoop, bins, nestedBins, out, false, false, false,
                   "current arrangement", keyVideo, keyVideoMinLength);
}

void BinSorterRenderer::preloadFromArrangement(const Arrangement& arr) {
    if (!binSorter) return;
    nextSlots.clear();
    nextSlotsLoadQueue.clear();
    buildSlotsImpl(binSorter, videoPool, videoLoop, arr.bins, arr.nestedBins, nextSlots, true, true, true,
                   "next arrangement", keyVideo, keyVideoMinLength);
    for (size_t i = 0; i < nextSlots.size(); ++i) {
        if (!nextSlots[i].path.empty()) {
            nextSlotsLoadQueue.push_back({i, false});
            if (!videoLoop && !nextSlots[i].nextPath.empty())
                nextSlotsLoadQueue.push_back({i, true});
        }
    }
    lastNextSlotLoadTime = ofGetElapsedTimef();
}

void BinSorterRenderer::swapToPreloaded(const Arrangement& arr) {
    if (nextSlots.empty()) return;
    binSorter->loadArrangement(arr.bins, arr.nestedBins);
    std::swap(slots, nextSlots);
    // Defer destruction to next frame: avoids Bus error 10 when freeing GPU resources
    // that may still be referenced by pending GL commands from the previous draw.
    slotsPendingDeletion.insert(slotsPendingDeletion.end(),
        std::make_move_iterator(nextSlots.begin()),
        std::make_move_iterator(nextSlots.end()));
    nextSlots.clear();
    loggedNotReadyKeys.clear();
    didSwapThisFrame = true;
    drawOrderDirty = true;
}

void BinSorterRenderer::startPlaying() {
    for (auto& slot : slots) {
        if (slot.hasVideo && slot.player.isLoaded())
            slot.player.play();
    }
}

void BinSorterRenderer::update() {
    // Clear deferred slots from previous frame's swap (not same frame - avoids Bus error 10)
    if (!didSwapThisFrame)
        slotsPendingDeletion.clear();
    didSwapThisFrame = false;

    // Staggered preload: load one video for next arrangement every 0.5s
    if (!nextSlotsLoadQueue.empty()) {
        float now = ofGetElapsedTimef();
        if (now - lastNextSlotLoadTime >= 0.5f) {
            auto p = nextSlotsLoadQueue.front();
            nextSlotsLoadQueue.pop_front();
            size_t slotIdx = p.first;
            bool isNextPlayer = p.second;
            if (slotIdx < nextSlots.size()) {
                auto& slot = nextSlots[slotIdx];
                if (isNextPlayer) {
                    if (!slot.nextPath.empty() && slot.nextPlayer.load(slot.nextPath)) {
                        ofLogNotice("BinSorterRenderer") << "Video load [next arrangement] " << slot.nextPath;
                        slot.nextPlayer.setLoopState(OF_LOOP_NONE);
                        slot.nextPlayer.setPosition(0);
                    }
                } else {
                    if (!slot.path.empty() && slot.player.load(slot.path)) {
                        ofLogNotice("BinSorterRenderer") << "Video load [next arrangement] " << slot.path;
                        slot.player.setLoopState(videoLoop ? OF_LOOP_NORMAL : OF_LOOP_NONE);
                    }
                }
            }
            lastNextSlotLoadTime = now;
        }
    }

    for (auto& slot : nextSlots) {
        if (slot.hasVideo) {
            slot.player.update();
            if (!videoLoop && slot.nextPlayer.isLoaded())
                slot.nextPlayer.update();
        }
    }
    for (size_t i = 0; i < slots.size(); ++i) {
        auto& slot = slots[i];
        if (slot.hasVideo) {
            slot.player.update();
            if (!videoLoop) {
                if (slot.nextPlayer.isLoaded())
                    slot.nextPlayer.update();  // keep preloaded video decoded for seamless swap
                if (slot.player.getIsMovieDone()) {
                    if (slot.nextPlayer.isLoaded()) {
                        slot.nextPlayer.play();
                        slot.player.close();
                        std::string newPath = videoPool ? videoPool->getVideoPath(slot.ratioW, slot.ratioH) : "";
                        if (newPath.empty()) {
                            slot.hasVideo = false;
                            ofLogWarning("BinSorterRenderer") << "Slot " << (i + 1) << ": no replacement video for ratio "
                                << slot.ratioW << ":" << slot.ratioH << ", falling back to placeholder";
                        } else {
                            if (slot.player.load(newPath)) {
                                ofLogNotice("BinSorterRenderer") << "Video load [current arrangement] " << newPath;
                                slot.player.setLoopState(OF_LOOP_NONE);
                                slot.player.setPosition(0);
                                slot.path = slot.nextPath;
                                slot.nextPath = newPath;
                                std::swap(slot.player, slot.nextPlayer);
                                float slotAspect = (slot.h > 0) ? (float)slot.w / slot.h : 0;
                                ofLogNotice("BinSorterRenderer") << "Slot " << (i + 1) << ": "
                                    << slot.w << "x" << slot.h << " aspect=" << slotAspect
                                    << " -> ratio " << slot.ratioW << ":" << slot.ratioH << " " << slot.path;
                            } else {
                                slot.hasVideo = false;
                                ofLogWarning("BinSorterRenderer") << "Slot " << (i + 1) << ": load failed for ratio "
                                    << slot.ratioW << "_" << slot.ratioH << ": " << newPath;
                            }
                        }
                    } else {
                        slot.player.close();
                        std::string path = videoPool ? videoPool->getVideoPath(slot.ratioW, slot.ratioH) : "";
                        if (path.empty()) {
                            slot.hasVideo = false;
                            ofLogWarning("BinSorterRenderer") << "Slot " << (i + 1) << ": no replacement video for ratio "
                                << slot.ratioW << ":" << slot.ratioH << ", falling back to placeholder";
                        } else {
                            if (slot.player.load(path)) {
                                ofLogNotice("BinSorterRenderer") << "Video load [current arrangement] " << path;
                                slot.path = path;
                                slot.player.setLoopState(OF_LOOP_NONE);
                                slot.player.play();
                                float slotAspect = (slot.h > 0) ? (float)slot.w / slot.h : 0;
                                ofLogNotice("BinSorterRenderer") << "Slot " << (i + 1) << ": "
                                    << slot.w << "x" << slot.h << " aspect=" << slotAspect
                                    << " -> ratio " << slot.ratioW << ":" << slot.ratioH << " " << path;
                            } else {
                                slot.hasVideo = false;
                                ofLogWarning("BinSorterRenderer") << "Slot " << (i + 1) << ": load failed for ratio "
                                    << slot.ratioW << "_" << slot.ratioH << ": " << path;
                            }
                        }
                    }
                }
            }
        }
    }
}

void BinSorterRenderer::draw(float offsetX, float offsetY) {
    if (!binSorter) return;
    int boxW = binSorter->getBoxWidth();
    int boxH = binSorter->getBoxHeight();
    auto& bins = binSorter->getBins();
    const int padding = 20;

    // Fill area outside content with black (when window is larger than render, e.g. fullscreen)
    int winW = ofGetWindowWidth(), winH = ofGetWindowHeight();
    int contentW = (int)(bins.size() * (boxW + padding) - (bins.empty() ? 0 : padding));
    if (boxH < winH || contentW < winW) {
        ofPushView();
        ofViewport(0, 0, winW, winH, false);
        ofFill();
        ofSetColor(0);
        if (boxH < winH)
            ofDrawRectangle(0, 0, winW, winH - boxH);
        if (contentW < winW)
            ofDrawRectangle(contentW, 0, winW - contentW, winH);
        ofPopView();
    }

    for (size_t bi = 0; bi < bins.size(); ++bi) {
        float offX = offsetX + bi * (boxW + padding);
        float offY = offsetY;
        ofNoFill();
        ofSetColor(0);
        ofDrawRectangle(offX, offY, boxW, boxH);
    }

    // Draw order: slots whose videos overflow into edge-adjacent neighbors are drawn
    // first so the neighbor's video covers the aspect-fill bleed.
    // Overflow direction is determined by comparing the target video aspect ratio
    // (ratioW/ratioH) against the actual slot aspect ratio (w/h):
    //   vidAspect > slotAspect → scales to full height → overflows left/right
    //   vidAspect < slotAspect → scales to full width  → overflows top/bottom
    // A directed "draw before" graph is built from these relationships, resolved
    // via topological sort (Kahn's). Mutual conflicts and ties fall back to
    // smallest-area-first, preserving the original overlap behavior.
    // The result is cached and only recomputed when slots change (drawOrderDirty).
    if (drawOrderDirty) {
        cachedDrawOrder.clear();
        const int edgeTol = 1;
        int N = (int)slots.size();
        auto area = [&](int k) { return slots[k].w * slots[k].h; };

        std::vector<bool> ovLR(N), ovTB(N);
        for (int i = 0; i < N; ++i) {
            const auto& s = slots[i];
            float vid = (s.ratioH > 0) ? (float)s.ratioW / s.ratioH : 1.f;
            float slt = (s.h > 0)      ? (float)s.w / s.h            : 1.f;
            ovLR[i] = vid > slt + 0.01f;
            ovTB[i] = vid < slt - 0.01f;
        }

        // Collect candidate "draw i before j" edges where i's overflow bleeds into j.
        std::vector<std::pair<int,int>> candidates;
        for (int i = 0; i < N; ++i) {
            if (!ovLR[i] && !ovTB[i]) continue;
            const auto& sa = slots[i];
            for (int j = 0; j < N; ++j) {
                if (i == j) continue;
                const auto& sb = slots[j];
                bool bleeds = false;
                if (ovLR[i]) {
                    bool adjV   = std::abs(sa.x + sa.w - sb.x) <= edgeTol ||
                                  std::abs(sb.x + sb.w - sa.x) <= edgeTol;
                    bool vertOv = !(sa.y + sa.h <= sb.y || sb.y + sb.h <= sa.y);
                    bleeds = adjV && vertOv;
                } else {
                    bool adjH   = std::abs(sa.y + sa.h - sb.y) <= edgeTol ||
                                  std::abs(sb.y + sb.h - sa.y) <= edgeTol;
                    bool horzOv = !(sa.x + sa.w <= sb.x || sb.x + sb.w <= sa.x);
                    bleeds = adjH && horzOv;
                }
                if (bleeds) candidates.push_back({i, j});
            }
        }

        // Resolve mutual conflicts (i→j and j→i both exist) using geometry:
        // for a shared horizontal edge draw the upper slot (smaller y) first so
        // the lower slot covers its downward bleed; for a shared vertical edge
        // draw the left slot (smaller x) first so the right slot covers its bleed.
        // This matches the natural direction of aspect-fill overflow.
        std::set<std::pair<int,int>> edgeSet;
        for (auto [i, j] : candidates) {
            if (edgeSet.count({j, i})) {
                const auto& sa = slots[i];
                const auto& sb = slots[j];
                bool sharedH = std::abs(sa.y + sa.h - sb.y) <= edgeTol ||
                               std::abs(sb.y + sb.h - sa.y) <= edgeTol;
                // keepIJ = true → i should be drawn before j (replace j→i with i→j)
                bool keepIJ = sharedH ? (sa.y < sb.y) : (sa.x < sb.x);
                if (keepIJ) { edgeSet.erase({j, i}); edgeSet.insert({i, j}); }
                // else: existing {j, i} is already the correct direction
            } else {
                edgeSet.insert({i, j});
            }
        }

        std::vector<std::vector<int>> outEdges(N);
        std::vector<int> inDeg(N, 0);
        for (auto [i, j] : edgeSet) { outEdges[i].push_back(j); inDeg[j]++; }

        // Kahn's topological sort, tie-breaking by smallest area first.
        using P = std::pair<int,int>;
        std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
        for (int i = 0; i < N; ++i)
            if (inDeg[i] == 0) pq.push({area(i), i});

        cachedDrawOrder.reserve(N);
        while (!pq.empty()) {
            auto [a, i] = pq.top(); pq.pop();
            cachedDrawOrder.push_back((size_t)i);
            for (int j : outEdges[i])
                if (--inDeg[j] == 0) pq.push({area(j), j});
        }
        // Fallback: any nodes left in a cycle are appended in area order.
        std::vector<P> rem;
        for (int i = 0; i < N; ++i)
            if (inDeg[i] > 0) rem.push_back({area(i), i});
        std::sort(rem.begin(), rem.end());
        for (auto [a, i] : rem) cachedDrawOrder.push_back((size_t)i);

        drawOrderDirty = false;
    }

    // Scissor all slot drawing to the content region so aspect-fill overflow
    // never bleeds into the black bars when the window is larger than the layout.
    // ofGetCurrentViewport() gives the correct height for both window and FBO targets.
    int viewH = (int)ofGetCurrentViewport().height;
    glEnable(GL_SCISSOR_TEST);
    glScissor((GLint)(offsetX), (GLint)(viewH - (offsetY + boxH)), (GLsizei)contentW, (GLsizei)boxH);

    for (size_t idx : cachedDrawOrder) {
        const auto& slot = slots[idx];
        float dx = offsetX + slot.x;
        float dy = offsetY + slot.y;
        if (slot.hasVideo && slot.player.isLoaded()) {
            ofSetColor(255);  // white - needed so video texture isn't tinted black
            float vw = slot.player.getWidth();
            float vh = slot.player.getHeight();
            // Prefer actual texture dimensions - player metadata can differ for non-square pixels
            const ofTexture& tex = slot.player.getTexture();
            if (tex.isAllocated()) {
                float tw = tex.getWidth(), th = tex.getHeight();
                if (tw > 0 && th > 0) { vw = tw; vh = th; }
            }
            if (vw > 0 && vh > 0) {
                // Aspect-fill (cover): scale video to completely fill the slot, center and crop overflow.
                // This removes white lines when slots have been expanded by stretchToEdges.
                float slotAspect = (float)slot.w / slot.h;
                float vidAspect = vw / vh;
                float drawW, drawH, drawX, drawY;
                if (vidAspect > slotAspect) {
                    drawH = slot.h;
                    drawW = slot.h * vidAspect;
                    drawX = dx + (slot.w - drawW) * 0.5f;
                    drawY = dy;
                } else {
                    drawW = slot.w;
                    drawH = slot.w / vidAspect;
                    drawX = dx;
                    drawY = dy + (slot.h - drawH) * 0.5f;
                }
                slot.player.draw(drawX, drawY, drawW, drawH);
            } else {
                float ol = 0.5f;
                slot.player.draw(dx - ol, dy - ol, slot.w + 2*ol, slot.h + 2*ol);
            }
        } else {
            if (slot.hasVideo) {
                std::string key = slot.path + "_" + std::to_string(slot.x) + "_" + std::to_string(slot.y);
                if (loggedNotReadyKeys.find(key) == loggedNotReadyKeys.end()) {
                    loggedNotReadyKeys.insert(key);
                    ofLogWarning("BinSorterRenderer") << "video not ready yet (isLoaded=false): "
                        << slot.path << " at (" << slot.x << "," << slot.y << ")";
                }
            }
            ofFill();
            ofSetColor(100, 150, 200);
            ofDrawRectangle(dx, dy, slot.w, slot.h);
        }
    }

    glDisable(GL_SCISSOR_TEST);
}

void BinSorterRenderer::drawToFbo(ofFbo& fbo) {
    fbo.begin();
    ofClear(255, 255);
    draw(0, 0);
    fbo.end();
}

void BinSorterRenderer::regenerate() {
    if (binSorter) {
        if (videoPool) videoPool->resetUsed();
        loggedNotReadyKeys.clear();
        slots.clear();
        buildSlots();
        drawOrderDirty = true;
    }
}

int BinSorterRenderer::getKeyVideoSlotIndex(float minLength) const {
    float best = -1.f;
    int bestIdx = -1;
    for (int i = 0; i < (int)slots.size(); ++i) {
        if (!slots[i].hasVideo) continue;
        float dur = slots[i].duration;
        if (dur >= minLength && dur > best) {
            best = dur;
            bestIdx = i;
        }
    }
    return bestIdx;
}

float BinSorterRenderer::getKeyVideoDuration(float minLength) const {
    int idx = getKeyVideoSlotIndex(minLength);
    return (idx >= 0) ? slots[idx].duration : -1.f;
}
