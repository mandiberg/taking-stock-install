#pragma once

// The openFrameworks GLFW main loop never drains an autorelease pool, so AVFoundation
// objects autoreleased on the main thread (per-frame AVTelemetryInterval, playback items)
// pile up for the lifetime of the process. This RAII scope drains them every frame.
// Uses the Objective-C runtime directly so it works from plain .cpp files.
extern "C" {
void* objc_autoreleasePoolPush(void);
void objc_autoreleasePoolPop(void*);
}

struct ScopedAutoreleasePool {
    void* pool;
    ScopedAutoreleasePool() : pool(objc_autoreleasePoolPush()) {}
    ~ScopedAutoreleasePool() { objc_autoreleasePoolPop(pool); }
    ScopedAutoreleasePool(const ScopedAutoreleasePool&) = delete;
    ScopedAutoreleasePool& operator=(const ScopedAutoreleasePool&) = delete;
};
