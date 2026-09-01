#import "CoreAudioPlayer.h"
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>
#import <string>
#import <vector>

// ---------------------------------------------------------------------------
// Helper: find an AudioDeviceID by exact device name.
// Returns kAudioDeviceUnknown if no match is found.
// ---------------------------------------------------------------------------
static AudioDeviceID findDeviceByName(const std::string& name) {
    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 size = 0;
    AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &size);
    if (size == 0) return kAudioDeviceUnknown;

    int count = (int)(size / sizeof(AudioDeviceID));
    std::vector<AudioDeviceID> devices(count);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, devices.data());

    NSString* target = [NSString stringWithUTF8String:name.c_str()];
    for (AudioDeviceID devID : devices) {
        CFStringRef cfName = nullptr;
        AudioObjectPropertyAddress nameAddr = {
            kAudioDevicePropertyDeviceNameCFString,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        UInt32 propSize = sizeof(cfName);
        OSStatus err = AudioObjectGetPropertyData(devID, &nameAddr, 0, nullptr, &propSize, &cfName);
        if (err != noErr || !cfName) continue;
        NSString* devName = (__bridge_transfer NSString*)cfName;
        if ([devName isEqualToString:target]) return devID;
    }
    return kAudioDeviceUnknown;
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct CoreAudioPlayer::Impl {
    AVAudioEngine*      engine  = nil;
    AVAudioPlayerNode*  player  = nil;
    AVAudioPCMBuffer*   buffer  = nil;  // pre-processed buffer (channel map + gains applied)

    bool  loop    = false;
    float volume  = 1.0f;
    bool  playing = false;

    // Configuration (set before load(); persist across loads)
    bool              surroundEnabled = false;
    std::vector<int>  channelMap;    // source channel index for each output channel
    std::vector<float> channelGains; // per-output-channel gain multiplier
    std::string       deviceName;    // empty = system default
};

// ---------------------------------------------------------------------------
CoreAudioPlayer::CoreAudioPlayer() : impl(new Impl()) {}

CoreAudioPlayer::~CoreAudioPlayer() {
    stop();
    if (impl->engine) {
        [impl->engine stop];
        impl->engine = nil;
    }
    delete impl;
}

// ---------------------------------------------------------------------------
// Configuration setters
// ---------------------------------------------------------------------------
void CoreAudioPlayer::setSurroundEnabled(bool surround) {
    impl->surroundEnabled = surround;
}

void CoreAudioPlayer::setChannelMap(const std::vector<int>& map) {
    impl->channelMap = map;
}

void CoreAudioPlayer::setChannelGains(const std::vector<float>& gains) {
    impl->channelGains = gains;
}

void CoreAudioPlayer::setOutputDeviceName(const std::string& name) {
    impl->deviceName = name;
}

// ---------------------------------------------------------------------------
// load()
// ---------------------------------------------------------------------------
bool CoreAudioPlayer::load(const std::string& path) {
    stop();
    if (impl->engine) {
        [impl->engine stop];
        impl->engine = nil;
        impl->player = nil;
        impl->buffer = nil;
    }

    // ---- 1. Open the audio file ----
    NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
    NSURL* url = [NSURL fileURLWithPath:nsPath];
    NSError* err = nil;
    AVAudioFile* file = [[AVAudioFile alloc] initForReading:url error:&err];
    if (!file) {
        NSLog(@"CoreAudioPlayer: failed to open %s: %@", path.c_str(), err);
        return false;
    }

    // ---- 2. Read the whole file into a non-interleaved float buffer ----
    // Build an intermediate format: same sample rate and channel count as the file,
    // but non-interleaved float (the format AVAudioPCMBuffer uses for floatChannelData).
    AVAudioFormat* fileFormat = file.processingFormat;
    double sampleRate = fileFormat.sampleRate;
    AVAudioChannelCount fileCh = fileFormat.channelCount;
    AVAudioFrameCount  frames  = (AVAudioFrameCount)file.length;

    AVAudioFormat* readFormat = [[AVAudioFormat alloc]
        initWithCommonFormat:AVAudioPCMFormatFloat32
                  sampleRate:sampleRate
                    channels:fileCh
                 interleaved:NO];

    AVAudioPCMBuffer* srcBuf = [[AVAudioPCMBuffer alloc]
        initWithPCMFormat:readFormat frameCapacity:frames];
    srcBuf.frameLength = frames;

    // Use ExtAudioFile to decode into our float non-interleaved format
    // (AVAudioFile.readIntoBuffer uses the processingFormat; we need float non-interleaved)
    CFStringRef cfPath = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    CFURLRef    cfURL  = CFURLCreateWithFileSystemPath(nullptr, cfPath, kCFURLPOSIXPathStyle, false);
    CFRelease(cfPath);

    ExtAudioFileRef extFile = nullptr;
    OSStatus status = ExtAudioFileOpenURL(cfURL, &extFile);
    CFRelease(cfURL);
    if (status != noErr) {
        NSLog(@"CoreAudioPlayer: ExtAudioFileOpenURL failed (%d) for %s", (int)status, path.c_str());
        return false;
    }

    // Configure client format: non-interleaved float, preserving channel count and sample rate
    AudioStreamBasicDescription clientFmt;
    memset(&clientFmt, 0, sizeof(clientFmt));
    clientFmt.mSampleRate       = sampleRate;
    clientFmt.mFormatID         = kAudioFormatLinearPCM;
    clientFmt.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsNonInterleaved | kAudioFormatFlagIsPacked;
    clientFmt.mBitsPerChannel   = 32;
    clientFmt.mChannelsPerFrame = fileCh;
    clientFmt.mBytesPerFrame    = 4;
    clientFmt.mFramesPerPacket  = 1;
    clientFmt.mBytesPerPacket   = 4;

    ExtAudioFileSetProperty(extFile, kExtAudioFileProperty_ClientDataFormat,
                            sizeof(clientFmt), &clientFmt);

    // Build an AudioBufferList pointing into the AVAudioPCMBuffer's channel arrays
    AudioBufferList* abl = (AudioBufferList*)malloc(
        sizeof(AudioBufferList) + (fileCh - 1) * sizeof(AudioBuffer));
    abl->mNumberBuffers = fileCh;
    for (UInt32 ch = 0; ch < fileCh; ch++) {
        abl->mBuffers[ch].mNumberChannels = 1;
        abl->mBuffers[ch].mDataByteSize   = frames * sizeof(float);
        abl->mBuffers[ch].mData           = srcBuf.floatChannelData[ch];
    }

    UInt32 framesToRead = frames;
    ExtAudioFileRead(extFile, &framesToRead, abl);
    free(abl);
    ExtAudioFileDispose(extFile);

    // ---- 3. Determine output format ----
    // Quad layout: FL(0), FR(1), BL(2), BR(3) — WAV/FFmpeg standard quad channel order.
    bool useSurround = impl->surroundEnabled;
    if (useSurround && fileCh < 4) {
        NSLog(@"CoreAudioPlayer: AUDIO_SURROUND=true but file has only %u channel(s) — "
              "falling back to native format for %s", fileCh, path.c_str());
        useSurround = false;
    }

    AVAudioFormat* outFormat;
    AVAudioChannelCount outCh;
    if (useSurround) {
        outCh = 4;
        AVAudioChannelLayout* layout = [AVAudioChannelLayout
            layoutWithLayoutTag:kAudioChannelLayoutTag_Quadraphonic];
        outFormat = [[AVAudioFormat alloc]
            initWithCommonFormat:AVAudioPCMFormatFloat32
                      sampleRate:sampleRate
                   interleaved:NO
                   channelLayout:layout];
    } else {
        outCh = fileCh;
        outFormat = readFormat;
    }

    // ---- 4. Allocate the output buffer and apply channel map + gains ----
    AVAudioPCMBuffer* dstBuf = [[AVAudioPCMBuffer alloc]
        initWithPCMFormat:outFormat frameCapacity:frames];
    dstBuf.frameLength = frames;

    const std::vector<int>&   map   = impl->channelMap;
    const std::vector<float>& gains = impl->channelGains;

    for (AVAudioChannelCount outIdx = 0; outIdx < outCh; outIdx++) {
        int   srcIdx = (outIdx < (AVAudioChannelCount)map.size())
                       ? map[(size_t)outIdx]
                       : (int)outIdx;
        float gain   = (outIdx < (AVAudioChannelCount)gains.size())
                       ? gains[(size_t)outIdx]
                       : 1.0f;

        // Clamp source channel to valid range
        if (srcIdx < 0 || srcIdx >= (int)fileCh) {
            // Out-of-range source: fill with silence
            memset(dstBuf.floatChannelData[outIdx], 0, frames * sizeof(float));
            continue;
        }

        float* dst = dstBuf.floatChannelData[outIdx];
        float* src = srcBuf.floatChannelData[srcIdx];
        if (gain == 1.0f) {
            memcpy(dst, src, frames * sizeof(float));
        } else {
            for (AVAudioFrameCount f = 0; f < frames; f++)
                dst[f] = src[f] * gain;
        }
    }

    impl->buffer = dstBuf;

    // ---- 5. Build AVAudioEngine ----
    impl->engine = [[AVAudioEngine alloc] init];
    impl->player = [[AVAudioPlayerNode alloc] init];
    [impl->engine attachNode:impl->player];

    // ---- 6. Device selection (must happen before engine start) ----
    if (!impl->deviceName.empty()) {
        AudioDeviceID devID = findDeviceByName(impl->deviceName);
        if (devID != kAudioDeviceUnknown) {
            AudioUnit outputAU = impl->engine.outputNode.audioUnit;
            OSStatus setErr = AudioUnitSetProperty(
                outputAU,
                kAudioOutputUnitProperty_CurrentDevice,
                kAudioUnitScope_Global,
                0,
                &devID,
                sizeof(devID));
            if (setErr != noErr)
                NSLog(@"CoreAudioPlayer: failed to set output device '%s' (err=%d); using system default",
                      impl->deviceName.c_str(), (int)setErr);
            else
                NSLog(@"CoreAudioPlayer: output device set to '%s'", impl->deviceName.c_str());
        } else {
            NSLog(@"CoreAudioPlayer: device '%s' not found — using system default",
                  impl->deviceName.c_str());
        }
    }

    // ---- 7. Connect player → mainMixerNode ----
    [impl->engine connect:impl->player
                       to:impl->engine.mainMixerNode
                   format:outFormat];

    // ---- 8. Start engine ----
    NSError* startErr = nil;
    if (![impl->engine startAndReturnError:&startErr]) {
        NSLog(@"CoreAudioPlayer: AVAudioEngine failed to start: %@", startErr);
        impl->engine = nil;
        impl->player = nil;
        impl->buffer = nil;
        return false;
    }

    // Apply initial volume
    impl->player.volume = impl->volume;

    NSLog(@"CoreAudioPlayer: loaded %s (%u frames, %u ch → %u ch out, %.0f Hz%s)",
          path.c_str(), frames, fileCh, outCh, sampleRate,
          useSurround ? ", quad" : "");
    return true;
}

// ---------------------------------------------------------------------------
// play()
// ---------------------------------------------------------------------------
void CoreAudioPlayer::play() {
    if (!impl->player || !impl->buffer) return;
    [impl->player stop];

    if (impl->loop) {
        [impl->player scheduleBuffer:impl->buffer
                              atTime:nil
                             options:AVAudioPlayerNodeBufferLoops
                   completionHandler:nil];
    } else {
        [impl->player scheduleBuffer:impl->buffer
                   completionHandler:nil];
    }
    [impl->player play];
    impl->playing = true;
}

// ---------------------------------------------------------------------------
// stop()
// ---------------------------------------------------------------------------
void CoreAudioPlayer::stop() {
    if (!impl->player) return;
    [impl->player stop];
    impl->playing = false;
}

// ---------------------------------------------------------------------------
// setLoop()
// ---------------------------------------------------------------------------
void CoreAudioPlayer::setLoop(bool loop) {
    impl->loop = loop;
}

// ---------------------------------------------------------------------------
// setVolume()  — live adjustment, no restart required
// ---------------------------------------------------------------------------
void CoreAudioPlayer::setVolume(float volume) {
    impl->volume = std::max(0.0f, std::min(1.0f, volume));
    if (impl->player)
        impl->player.volume = impl->volume;
}

// ---------------------------------------------------------------------------
// isPlaying()
// ---------------------------------------------------------------------------
bool CoreAudioPlayer::isPlaying() {
    return impl->playing && impl->player && impl->player.isPlaying;
}
