#import "CoreAudioPlayer.h"
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>
#import <algorithm>
#import <cstring>
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

// AVAudioFormat's channels: initializer returns nil for >2 channels. Always
// pass an explicit layout so 4-channel (quad) files don't crash on buffer alloc.
static AVAudioFormat* makePlanarFloatFormat(double sampleRate, AVAudioChannelCount channels,
                                                AVAudioChannelLayout* preferredLayout) {
    if (preferredLayout) {
        AVAudioFormat* fmt = [[AVAudioFormat alloc]
            initWithCommonFormat:AVAudioPCMFormatFloat32
                      sampleRate:sampleRate
                     interleaved:NO
                   channelLayout:preferredLayout];
        if (fmt && fmt.channelCount == channels) return fmt;
    }
    if (channels <= 2) {
        return [[AVAudioFormat alloc]
            initWithCommonFormat:AVAudioPCMFormatFloat32
                      sampleRate:sampleRate
                        channels:channels
                     interleaved:NO];
    }
    AudioChannelLayoutTag tag = kAudioChannelLayoutTag_DiscreteInOrder | channels;
    if (channels == 4) tag = kAudioChannelLayoutTag_Quadraphonic;
    else if (channels == 6) tag = kAudioChannelLayoutTag_MPEG_5_1_A;
    AVAudioChannelLayout* layout = [AVAudioChannelLayout layoutWithLayoutTag:tag];
    return [[AVAudioFormat alloc]
        initWithCommonFormat:AVAudioPCMFormatFloat32
                  sampleRate:sampleRate
                 interleaved:NO
               channelLayout:layout];
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

    NSLog(@"CoreAudioPlayer: loading %s", path.c_str());

    // ---- 1. Open the audio file ----
    NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
    NSURL* url = [NSURL fileURLWithPath:nsPath];
    NSError* err = nil;
    AVAudioFile* file = [[AVAudioFile alloc] initForReading:url error:&err];
    if (!file) {
        NSLog(@"CoreAudioPlayer: failed to open %s: %@", path.c_str(), err);
        return false;
    }

    // processingFormat is already deinterleaved float and includes a channel layout
    // for >2ch files (the channels: initializer returns nil for those).
    AVAudioFormat* fileFormat = file.processingFormat;
    double sampleRate = fileFormat.sampleRate;
    AVAudioChannelCount fileCh = fileFormat.channelCount;
    AVAudioFrameCount frames = (AVAudioFrameCount)file.length;
    if (!fileFormat || fileCh == 0 || frames == 0) {
        NSLog(@"CoreAudioPlayer: invalid format for %s (ch=%u frames=%u)",
              path.c_str(), fileCh, frames);
        return false;
    }

    AVAudioPCMBuffer* srcBuf = [[AVAudioPCMBuffer alloc]
        initWithPCMFormat:fileFormat frameCapacity:frames];
    if (!srcBuf) {
        NSLog(@"CoreAudioPlayer: could not allocate source buffer for %s", path.c_str());
        return false;
    }
    if (![file readIntoBuffer:srcBuf error:&err]) {
        NSLog(@"CoreAudioPlayer: failed to read %s: %@", path.c_str(), err);
        return false;
    }
    if (!srcBuf.floatChannelData) {
        NSLog(@"CoreAudioPlayer: no planar float data in %s", path.c_str());
        return false;
    }
    frames = srcBuf.frameLength;

    // ---- 2. Build engine and select device so we can query hardware channel count ----
    impl->engine = [[AVAudioEngine alloc] init];
    impl->player = [[AVAudioPlayerNode alloc] init];
    [impl->engine attachNode:impl->player];

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

    AVAudioFormat* hwFormat = [impl->engine.outputNode outputFormatForBus:0];
    AVAudioChannelCount hwCh = hwFormat ? hwFormat.channelCount : 0;
    NSLog(@"CoreAudioPlayer: current output device reports %u ch @ %.0f Hz "
          "(informational — quad output is not limited to this)",
          hwCh, hwFormat ? hwFormat.sampleRate : 0.0);

    // ---- 3. Determine output format ----
    // Always emit 4 channels when AUDIO_SURROUND is on. Do not clamp to whatever
    // this machine's current default device reports — the install uses a different
    // 4-channel interface, and the pre-start query here is often 2ch even then.
    bool useSurround = impl->surroundEnabled;
    if (useSurround && fileCh < 4) {
        NSLog(@"CoreAudioPlayer: AUDIO_SURROUND=true but file has only %u channel(s) — "
              "falling back to native format for %s", fileCh, path.c_str());
        useSurround = false;
    }

    AVAudioChannelCount outCh = useSurround ? 4 : fileCh;

    AVAudioChannelLayout* preferredLayout = nil;
    if (useSurround && hwFormat && hwFormat.channelCount == 4 && hwFormat.channelLayout)
        preferredLayout = hwFormat.channelLayout;
    else if (!useSurround && fileFormat.channelCount == outCh)
        preferredLayout = fileFormat.channelLayout;

    AVAudioFormat* outFormat = makePlanarFloatFormat(sampleRate, outCh, preferredLayout);
    if (!outFormat) {
        NSLog(@"CoreAudioPlayer: could not create %u-channel output format for %s",
              outCh, path.c_str());
        impl->engine = nil;
        impl->player = nil;
        return false;
    }

    // ---- 4. Allocate the output buffer and apply channel map + gains ----
    AVAudioPCMBuffer* dstBuf = [[AVAudioPCMBuffer alloc]
        initWithPCMFormat:outFormat frameCapacity:frames];
    if (!dstBuf || !dstBuf.floatChannelData) {
        NSLog(@"CoreAudioPlayer: could not allocate %u-channel output buffer for %s",
              outCh, path.c_str());
        impl->engine = nil;
        impl->player = nil;
        return false;
    }
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

        float* dst = dstBuf.floatChannelData[outIdx];
        if (!dst) continue;

        if (srcIdx < 0 || srcIdx >= (int)fileCh) {
            memset(dst, 0, (size_t)frames * sizeof(float));
            continue;
        }

        float* src = srcBuf.floatChannelData[srcIdx];
        if (!src) {
            memset(dst, 0, (size_t)frames * sizeof(float));
            continue;
        }
        if (gain == 1.0f) {
            memcpy(dst, src, (size_t)frames * sizeof(float));
        } else {
            for (AVAudioFrameCount f = 0; f < frames; f++)
                dst[f] = src[f] * gain;
        }
    }

    impl->buffer = dstBuf;

    // ---- 5. Connect player → mainMixerNode ----
    @try {
        [impl->engine connect:impl->player
                           to:impl->engine.mainMixerNode
                       format:outFormat];
    } @catch (NSException* ex) {
        NSLog(@"CoreAudioPlayer: connect failed (%@). Retrying with native file format.", ex);
        @try {
            [impl->engine connect:impl->player
                               to:impl->engine.mainMixerNode
                           format:fileFormat];
            impl->buffer = srcBuf;
            outCh = fileCh;
        } @catch (NSException* ex2) {
            NSLog(@"CoreAudioPlayer: connect retry failed: %@", ex2);
            impl->engine = nil;
            impl->player = nil;
            impl->buffer = nil;
            return false;
        }
    }

    // ---- 6. Start engine ----
    NSError* startErr = nil;
    if (![impl->engine startAndReturnError:&startErr]) {
        NSLog(@"CoreAudioPlayer: AVAudioEngine failed to start: %@", startErr);
        impl->engine = nil;
        impl->player = nil;
        impl->buffer = nil;
        return false;
    }

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
