#pragma once

// Long-run stability diagnostics (header-only, main-thread only).
//
// Every REPORT_INTERVAL_SEC seconds one row is appended to
//   bin/data/diagnostics/diag_<start time>.csv   (flushed immediately, survives a crash)
// and a one-line "[Diag]" summary is logged. Every SNAPSHOT_INTERVAL_SEC seconds a
// background shell job also dumps footprint / vmmap / GPU memory / thermal state to
//   bin/data/diagnostics/snap_<start time>_<n>.txt
//
// What it answers:
//   * Are ofVideoPlayer / VideoSlot objects accumulating?  (live_* vs expected_*)
//   * Is the main loop slow, or only the video decoders?   (fps vs vid_fps_per_slot)
//   * Which phase is slow: update, load, destroy, draw, Syphon publish, GPU?
//   * Is any process resource growing: footprint, threads, mach ports, fds?

#include "ofMain.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <libproc.h>
#include <mach/mach.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <unistd.h>

namespace Diag {

constexpr double REPORT_INTERVAL_SEC = 60.0;
constexpr double SNAPSHOT_INTERVAL_SEC = 1800.0;
constexpr double FIRST_SNAPSHOT_SEC = 120.0;
constexpr double HITCH_LOG_MS = 250.0;
constexpr int MAX_HITCH_LOGS_PER_INTERVAL = 20;
constexpr double SLOW_LOAD_LOG_MS = 1000.0;

using Clock = std::chrono::steady_clock;
inline double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Counts live instances of T-tagged objects, including copies and moved-from objects
// (which still run a destructor).
template <class Tag>
struct LiveCounter {
    static inline std::atomic<long long> live{0};
    static inline std::atomic<long long> created{0};
    static inline std::atomic<long long> destroyed{0};
    LiveCounter() noexcept { ++live; ++created; }
    LiveCounter(const LiveCounter&) noexcept { ++live; ++created; }
    LiveCounter(LiveCounter&&) noexcept { ++live; ++created; }
    LiveCounter& operator=(const LiveCounter&) noexcept { return *this; }
    LiveCounter& operator=(LiveCounter&&) noexcept { return *this; }
    ~LiveCounter() { --live; ++destroyed; }
};

struct PlayerTag {};
struct SlotTag {};

// Drop-in ofVideoPlayer that is counted.
class TrackedVideoPlayer : public ofVideoPlayer {
    LiveCounter<PlayerTag> counter_;
};

struct ProcStats {
    double footprintMB = 0, residentMB = 0, virtualMB = 0, internalMB = 0, compressedMB = 0;
    int threads = -1, machPorts = -1, fds = -1;
    double cpuSec = 0;
    long long inblock = 0, oublock = 0, majflt = 0, minflt = 0;
    double sysWiredMB = 0, sysCompressorMB = 0, sysFreeMB = 0, swapUsedMB = 0;
    long long swapouts = 0, pageouts = 0;
    int pressureLevel = -1;
};

inline ProcStats readProcStats() {
    ProcStats s;
    const double MB = 1024.0 * 1024.0;

    task_vm_info_data_t vi;
    mach_msg_type_number_t viCount = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vi, &viCount) == KERN_SUCCESS) {
        s.footprintMB = vi.phys_footprint / MB;
        s.residentMB = vi.resident_size / MB;
        s.virtualMB = vi.virtual_size / MB;
        s.internalMB = vi.internal / MB;
        s.compressedMB = vi.compressed / MB;
    }

    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t nThreads = 0;
    if (task_threads(mach_task_self(), &threads, &nThreads) == KERN_SUCCESS) {
        s.threads = (int)nThreads;
        for (mach_msg_type_number_t i = 0; i < nThreads; ++i)
            mach_port_deallocate(mach_task_self(), threads[i]);
        vm_deallocate(mach_task_self(), (vm_address_t)threads, nThreads * sizeof(thread_t));
    }

    mach_port_name_array_t names = nullptr;
    mach_port_type_array_t types = nullptr;
    mach_msg_type_number_t nNames = 0, nTypes = 0;
    if (mach_port_names(mach_task_self(), &names, &nNames, &types, &nTypes) == KERN_SUCCESS) {
        s.machPorts = (int)nNames;
        vm_deallocate(mach_task_self(), (vm_address_t)names, nNames * sizeof(*names));
        vm_deallocate(mach_task_self(), (vm_address_t)types, nTypes * sizeof(*types));
    }

    int fdBytes = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, nullptr, 0);
    if (fdBytes > 0) s.fds = fdBytes / (int)sizeof(proc_fdinfo);

    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        s.cpuSec = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec * 1e-6
                 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec * 1e-6;
        s.inblock = ru.ru_inblock;
        s.oublock = ru.ru_oublock;
        s.majflt = ru.ru_majflt;
        s.minflt = ru.ru_minflt;
    }

    vm_statistics64_data_t vs;
    mach_msg_type_number_t vsCount = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vs, &vsCount) == KERN_SUCCESS) {
        const double page = (double)vm_page_size;
        s.sysWiredMB = vs.wire_count * page / MB;
        s.sysCompressorMB = vs.compressor_page_count * page / MB;
        s.sysFreeMB = vs.free_count * page / MB;
        s.swapouts = (long long)vs.swapouts;
        s.pageouts = (long long)vs.pageouts;
    }

    xsw_usage swap;
    size_t swapLen = sizeof(swap);
    if (sysctlbyname("vm.swapusage", &swap, &swapLen, nullptr, 0) == 0)
        s.swapUsedMB = swap.xsu_used / MB;

    int level = 0;
    size_t levelLen = sizeof(level);
    if (sysctlbyname("kern.memorystatus_vm_pressure_level", &level, &levelLen, nullptr, 0) == 0)
        s.pressureLevel = level;

    return s;
}

// Time spent in the current frame, by phase (ms).
struct FrameTimes {
    double update = 0, renderer = 0, load = 0, destroy = 0, audio = 0;
    double draw = 0, publish = 0, gpuFinish = 0;
};

struct Stat {
    double sum = 0, max = 0;
    long long n = 0;
    void add(double v) { sum += v; if (v > max) max = v; ++n; }
    double avg() const { return n ? sum / n : 0.0; }
};

// Counts sampled from the renderer at report time.
struct RendererCounts {
    int slots = 0, nextSlots = 0, pendingSlots = 0;
    int loadedCurrent = 0, loadedNext = 0, loadedPending = 0;
};

struct AudioCounts {
    int liveEngines = 0;
    long long loadCalls = 0;
    long long enginesCreated = 0;
    long long enginesReleased = 0;
    long long sourceBufferBytes = 0;
    long long retainedBufferBytes = 0;
};

class Monitor {
public:
    // ---- per-frame hooks -------------------------------------------------
    void beginUpdate() {
        auto now = Clock::now();
        if (started_) {
            double dt = std::chrono::duration<double, std::milli>(now - lastFrameStart_).count();
            finishFrame(dt);
        } else {
            started_ = true;
            startTime_ = now;
            lastReport_ = now;
            lastSnapshot_ = now;
            openFiles();
            prevProc_ = readProcStats();
        }
        lastFrameStart_ = now;
        cur_ = FrameTimes{};
    }

    FrameTimes& cur() { return cur_; }
    bool sampleGpuNow() const { return (totalFrames_ % 120) == 0; }

    // ---- event hooks -----------------------------------------------------
    void noteLoad(double ms, bool ok, const std::string& path) {
        cur_.load += ms;
        loads_.add(ms);
        ++totalLoads_;
        if (!ok) ++loadFails_;
        if (ms >= SLOW_LOAD_LOG_MS)
            ofLogWarning("Diag") << "SLOW LOAD " << (int)ms << " ms (" << (ok ? "ok" : "FAILED") << "): "
                                 << ofFile(path).getFileName();
    }
    void noteClose(double ms) {
        cur_.destroy += ms;
        closes_.add(ms);
        ++totalCloses_;
    }
    void noteVideoUpdate(double ms, bool currentPlayer, bool loaded, bool frameNew,
                         const std::string& path) {
        videoUpdates_.add(ms);
        if (currentPlayer) currentVideoUpdates_.add(ms);
        else preloadedVideoUpdates_.add(ms);
        if (loaded) ++videoLoadedUpdateCount_;
        if (frameNew) ++videoNewFrameCount_;
        if (ms >= SLOW_LOAD_LOG_MS)
            ofLogWarning("Diag") << "SLOW VIDEO UPDATE " << (int)ms << " ms ("
                << (currentPlayer ? "current" : "preloaded") << ", loaded=" << loaded
                << "): " << ofFile(path).getFileName();
    }
    void noteDestroy(double ms, int count) {
        cur_.destroy += ms;
        destroys_.add(ms);
        destroyedSlots_ += count;
        if (ms >= SLOW_LOAD_LOG_MS)
            ofLogWarning("Diag") << "SLOW DESTROY " << (int)ms << " ms for " << count << " slots";
    }
    void noteAudioLoad(double ms) { cur_.audio += ms; audioLoads_.add(ms); }
    void noteVideoFrame(int playingSlots, int newFrames) {
        vidNewFrames_ += newFrames;
        playingSlotFrames_ += playingSlots;
    }
    void noteTransition() { ++transitions_; }
    void notePreloadStall() { ++preloadStalls_; }

    // ---- reporting -------------------------------------------------------
    bool reportDue() const {
        return started_ && std::chrono::duration<double>(Clock::now() - lastReport_).count() >= REPORT_INTERVAL_SEC;
    }

    void report(const RendererCounts& rc, const AudioCounts& ac) {
        auto now = Clock::now();
        const double wall = std::chrono::duration<double>(now - lastReport_).count();
        const double uptime = std::chrono::duration<double>(now - startTime_).count();
        lastReport_ = now;

        ProcStats ps = readProcStats();
        const double cpuPct = wall > 0 ? 100.0 * (ps.cpuSec - prevProc_.cpuSec) / wall : 0.0;

        const long long livePlayers = LiveCounter<PlayerTag>::live;
        const long long liveSlots = LiveCounter<SlotTag>::live;
        const long long expectedSlots = rc.slots + rc.nextSlots + rc.pendingSlots;
        const long long expectedPlayers = expectedSlots * 2;
        const double playingSlotSec = playingSlotFrames_ > 0 && frames_.n > 0
            ? playingSlotFrames_ * (frameDt_.avg() / 1000.0) : 0.0;
        const double vidFpsPerSlot = playingSlotSec > 0 ? vidNewFrames_ / playingSlotSec : 0.0;
        const double idleAvg = frameDt_.avg() - updates_.avg() - draws_.avg();

        Row r;
        r.add("timestamp", ofGetTimestampString("%Y-%m-%d %H:%M:%S"));
        r.add("uptime_h", uptime / 3600.0, 3);
        r.add("frames", frameDt_.n);
        r.add("fps", frameDt_.avg() > 0 ? 1000.0 / frameDt_.avg() : 0.0, 1);
        r.add("dt_max_ms", frameDt_.max, 1);
        r.add("hitch_50", hitch50_);
        r.add("hitch_100", hitch100_);
        r.add("hitch_250", hitch250_);
        r.add("hitch_1000", hitch1000_);
        r.add("update_avg_ms", updates_.avg(), 2);
        r.add("update_max_ms", updates_.max, 1);
        r.add("renderer_avg_ms", renderers_.avg(), 2);
        r.add("renderer_max_ms", renderers_.max, 1);
        r.add("draw_avg_ms", draws_.avg(), 2);
        r.add("draw_max_ms", draws_.max, 1);
        r.add("publish_avg_ms", publishes_.avg(), 2);
        r.add("publish_max_ms", publishes_.max, 1);
        r.add("gpu_finish_avg_ms", gpuFinish_.avg(), 2);
        r.add("gpu_finish_max_ms", gpuFinish_.max, 1);
        r.add("idle_avg_ms", idleAvg, 2);
        r.add("vid_fps_per_slot", vidFpsPerSlot, 1);
        r.add("vid_new_frames", vidNewFrames_);
        r.add("loads", loads_.n);
        r.add("load_fails", loadFails_);
        r.add("load_avg_ms", loads_.avg(), 1);
        r.add("load_max_ms", loads_.max, 1);
        r.add("closes", closes_.n);
        r.add("close_avg_ms", closes_.avg(), 2);
        r.add("close_max_ms", closes_.max, 1);
        r.add("video_update_avg_ms", videoUpdates_.avg(), 2);
        r.add("video_update_max_ms", videoUpdates_.max, 1);
        r.add("current_video_update_max_ms", currentVideoUpdates_.max, 1);
        r.add("preloaded_video_update_max_ms", preloadedVideoUpdates_.max, 1);
        r.add("video_loaded_updates", videoLoadedUpdateCount_);
        r.add("video_new_frames", videoNewFrameCount_);
        r.add("destroyed_slots", destroyedSlots_);
        r.add("destroy_max_ms", destroys_.max, 1);
        r.add("audio_loads", audioLoads_.n);
        r.add("audio_load_max_ms", audioLoads_.max, 1);
        r.add("audio_engines_live", ac.liveEngines);
        r.add("audio_load_calls_total", ac.loadCalls);
        r.add("audio_engines_created_total", ac.enginesCreated);
        r.add("audio_engines_released_total", ac.enginesReleased);
        r.add("audio_source_buffer_mb", ac.sourceBufferBytes / (1024.0 * 1024.0), 2);
        r.add("audio_retained_buffer_mb", ac.retainedBufferBytes / (1024.0 * 1024.0), 2);
        r.add("transitions", transitions_);
        r.add("preload_stalls", preloadStalls_);
        r.add("slots", rc.slots);
        r.add("next_slots", rc.nextSlots);
        r.add("pending_slots", rc.pendingSlots);
        r.add("loaded_current", rc.loadedCurrent);
        r.add("loaded_next", rc.loadedNext);
        r.add("loaded_pending", rc.loadedPending);
        r.add("live_slots", liveSlots);
        r.add("expected_slots", expectedSlots);
        r.add("live_players", livePlayers);
        r.add("expected_players", expectedPlayers);
        r.add("total_players_created", LiveCounter<PlayerTag>::created.load());
        r.add("total_players_destroyed", LiveCounter<PlayerTag>::destroyed.load());
        r.add("total_loads", totalLoads_);
        r.add("total_closes", totalCloses_);
        r.add("footprint_mb", ps.footprintMB, 1);
        r.add("resident_mb", ps.residentMB, 1);
        r.add("internal_mb", ps.internalMB, 1);
        r.add("compressed_mb", ps.compressedMB, 1);
        r.add("virtual_mb", ps.virtualMB, 0);
        r.add("threads", ps.threads);
        r.add("mach_ports", ps.machPorts);
        r.add("fds", ps.fds);
        r.add("cpu_pct", cpuPct, 1);
        r.add("proc_inblock", ps.inblock - prevProc_.inblock);
        r.add("proc_oublock", ps.oublock - prevProc_.oublock);
        r.add("proc_majflt", ps.majflt - prevProc_.majflt);
        r.add("sys_wired_mb", ps.sysWiredMB, 0);
        r.add("sys_compressor_mb", ps.sysCompressorMB, 0);
        r.add("sys_free_mb", ps.sysFreeMB, 0);
        r.add("swap_used_mb", ps.swapUsedMB, 0);
        r.add("swapouts", ps.swapouts - prevProc_.swapouts);
        r.add("pageouts", ps.pageouts - prevProc_.pageouts);
        r.add("mem_pressure", ps.pressureLevel);
        writeCsv(r);

        ofLogNotice("Diag") << "up=" << std::fixed << std::setprecision(2) << uptime / 3600.0 << "h"
            << " fps=" << std::setprecision(1) << (frameDt_.avg() > 0 ? 1000.0 / frameDt_.avg() : 0.0)
            << " dtMax=" << (int)frameDt_.max << "ms"
            << " hitch>100ms=" << hitch100_
            << " vidFps/slot=" << vidFpsPerSlot
            << " loadAvg/max=" << (int)loads_.avg() << "/" << (int)loads_.max << "ms"
            << " videoUpdateMax=" << (int)videoUpdates_.max << "ms"
            << " closeMax=" << (int)closes_.max << "ms"
            << " | players live=" << livePlayers << " expected=" << expectedPlayers
            << " loaded=" << (rc.loadedCurrent + rc.loadedNext + rc.loadedPending)
            << " slots live=" << liveSlots << " expected=" << expectedSlots
            << " | footprint=" << (int)ps.footprintMB << "MB threads=" << ps.threads
            << " ports=" << ps.machPorts << " fds=" << ps.fds;

        if (livePlayers != expectedPlayers || liveSlots != expectedSlots)
            ofLogWarning("Diag") << "OBJECT COUNT MISMATCH: live players=" << livePlayers
                << " (expected " << expectedPlayers << "), live slots=" << liveSlots
                << " (expected " << expectedSlots << ") - something is holding on to players";

        prevProc_ = ps;
        resetInterval();

        const double sinceSnap = std::chrono::duration<double>(now - lastSnapshot_).count();
        const double nextSnap = snapshotCount_ == 0 ? FIRST_SNAPSHOT_SEC : SNAPSHOT_INTERVAL_SEC;
        if (sinceSnap >= nextSnap - 1.0 || (snapshotCount_ == 0 && uptime >= FIRST_SNAPSHOT_SEC)) {
            takeSnapshot();
            lastSnapshot_ = now;
        }
    }

private:
    struct Row {
        std::vector<std::pair<std::string, std::string>> cols;
        void add(const std::string& k, const std::string& v) { cols.emplace_back(k, v); }
        void add(const std::string& k, double v, int prec) {
            std::ostringstream o;
            o << std::fixed << std::setprecision(prec) << v;
            cols.emplace_back(k, o.str());
        }
        template <class T>
        void add(const std::string& k, T v) { cols.emplace_back(k, std::to_string(v)); }
    };

    void finishFrame(double dt) {
        ++totalFrames_;
        frameDt_.add(dt);
        updates_.add(cur_.update);
        renderers_.add(cur_.renderer);
        draws_.add(cur_.draw);
        publishes_.add(cur_.publish);
        if (cur_.gpuFinish > 0) gpuFinish_.add(cur_.gpuFinish);
        frames_.add(dt);
        if (dt > 50) ++hitch50_;
        if (dt > 100) ++hitch100_;
        if (dt > 250) ++hitch250_;
        if (dt > 1000) ++hitch1000_;
        if (dt >= HITCH_LOG_MS && hitchLogs_ < MAX_HITCH_LOGS_PER_INTERVAL) {
            ++hitchLogs_;
            ofLogWarning("Diag") << "HITCH frame=" << (int)dt << "ms: update=" << (int)cur_.update
                << " (renderer=" << (int)cur_.renderer << " load=" << (int)cur_.load
                << " destroy=" << (int)cur_.destroy << " audio=" << (int)cur_.audio << ")"
                << " draw=" << (int)cur_.draw << " (publish=" << (int)cur_.publish << ")"
                << " gpuFinish=" << (int)cur_.gpuFinish
                << " other(swap/vsync)=" << (int)(dt - cur_.update - cur_.draw);
        }
    }

    void resetInterval() {
        frames_ = frameDt_ = updates_ = renderers_ = draws_ = publishes_ = gpuFinish_ = Stat{};
        loads_ = closes_ = destroys_ = audioLoads_ = Stat{};
        videoUpdates_ = currentVideoUpdates_ = preloadedVideoUpdates_ = Stat{};
        hitch50_ = hitch100_ = hitch250_ = hitch1000_ = 0;
        hitchLogs_ = 0;
        loadFails_ = destroyedSlots_ = transitions_ = preloadStalls_ = 0;
        vidNewFrames_ = playingSlotFrames_ = 0;
        videoLoadedUpdateCount_ = videoNewFrameCount_ = 0;
    }

    void openFiles() {
        stamp_ = ofGetTimestampString("%Y%m%d_%H%M%S");
        dir_ = ofToDataPath("diagnostics", true);
        ofDirectory::createDirectory(dir_, true, true);
        csvPath_ = dir_ + "/diag_" + stamp_ + ".csv";
        ofLogNotice("Diag") << "Writing diagnostics to " << csvPath_;
    }

    void writeCsv(const Row& r) {
        std::ofstream f(csvPath_, std::ios::app);
        if (!f) return;
        if (!headerWritten_) {
            for (size_t i = 0; i < r.cols.size(); ++i) f << (i ? "," : "") << r.cols[i].first;
            f << "\n";
            headerWritten_ = true;
        }
        for (size_t i = 0; i < r.cols.size(); ++i) f << (i ? "," : "") << r.cols[i].second;
        f << std::endl;
    }

    // Runs in a detached background shell so it can't stall the render loop.
    void takeSnapshot() {
        ++snapshotCount_;
        const std::string pid = std::to_string(getpid());
        const std::string path = dir_ + "/snap_" + stamp_ + "_" + std::to_string(snapshotCount_) + ".txt";
        const std::string cmd =
            "( date; "
            "echo '=== footprint'; /usr/bin/footprint " + pid + "; "
            "echo '=== vmmap -summary'; /usr/bin/vmmap -summary " + pid + "; "
            "echo '=== GPU (ioreg IOAccelerator)'; /usr/sbin/ioreg -r -c IOAccelerator -w0 | grep PerformanceStatistics; "
            "echo '=== thermal'; /usr/bin/pmset -g therm; "
            "echo '=== top'; /usr/bin/top -l 1 -n 8 -o cpu "
            ") > '" + path + "' 2>&1 &";
        std::system(cmd.c_str());
        ofLogNotice("Diag") << "Snapshot #" << snapshotCount_ << " -> " << path;

        // heap: malloc class histogram by size every snapshot. leaks: first snapshot
        // (baseline) and then every 4th. Both run on a forked corpse, so the app is
        // only paused briefly, and in a separate background job.
        const std::string base = dir_ + "/snap_" + stamp_ + "_" + std::to_string(snapshotCount_);
        std::string heapCmd =
            "( /usr/bin/heap --forkCorpse -s -H " + pid + " > '" + base + "_heap.txt' 2>&1";
        if (snapshotCount_ == 1 || snapshotCount_ % 4 == 0)
            heapCmd += "; /usr/bin/leaks --forkCorpse --groupByType " + pid + " > '" + base + "_leaks.txt' 2>&1";
        heapCmd += " ) &";
        std::system(heapCmd.c_str());
    }

    bool started_ = false;
    Clock::time_point startTime_, lastReport_, lastSnapshot_, lastFrameStart_;
    FrameTimes cur_;
    long long totalFrames_ = 0;
    int snapshotCount_ = 0;
    bool headerWritten_ = false;
    std::string stamp_, dir_, csvPath_;
    ProcStats prevProc_;

    Stat frames_, frameDt_, updates_, renderers_, draws_, publishes_, gpuFinish_;
    Stat loads_, closes_, destroys_, audioLoads_;
    Stat videoUpdates_, currentVideoUpdates_, preloadedVideoUpdates_;
    long long hitch50_ = 0, hitch100_ = 0, hitch250_ = 0, hitch1000_ = 0;
    int hitchLogs_ = 0;
    long long loadFails_ = 0, destroyedSlots_ = 0, transitions_ = 0, preloadStalls_ = 0;
    long long vidNewFrames_ = 0, playingSlotFrames_ = 0;
    long long videoLoadedUpdateCount_ = 0, videoNewFrameCount_ = 0;
    long long totalLoads_ = 0, totalCloses_ = 0;
};

inline Monitor& monitor() {
    static Monitor m;
    return m;
}

// Adds elapsed ms to a counter on scope exit.
struct ScopedMs {
    double& target;
    Clock::time_point t0 = Clock::now();
    explicit ScopedMs(double& t) : target(t) {}
    ~ScopedMs() { target += msSince(t0); }
};

inline bool loadVideo(ofVideoPlayer& p, const std::string& path) {
    auto t0 = Clock::now();
    bool ok = p.load(path);
    monitor().noteLoad(msSince(t0), ok, path);
    return ok;
}

inline void updateVideo(ofVideoPlayer& p, bool currentPlayer, const std::string& path) {
    auto t0 = Clock::now();
    p.update();
    const double ms = msSince(t0);
    const bool loaded = p.isLoaded();
    const bool frameNew = loaded && p.isFrameNew();
    monitor().noteVideoUpdate(ms, currentPlayer, loaded, frameNew, path);
}

inline void closeVideo(ofVideoPlayer& p) {
    auto t0 = Clock::now();
    p.close();
    monitor().noteClose(msSince(t0));
}

}  // namespace Diag
