#import "CoreAudioPlayer.h"
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>
#import <string>
#import <vector>
#import <cstring>

static const int kNumBuffers = 8;
static const int kBufferSize = 131072;

struct CoreAudioPlayer::Impl {
    AudioQueueRef queue = nullptr;
    AudioStreamBasicDescription format;
    std::vector<float> audioData;
    size_t playhead = 0;
    bool loop = false;
    float volume = 1.0f;
    bool playing = false;
    AudioQueueBufferRef buffers[kNumBuffers];
    
    static void callback(void* userData, AudioQueueRef queue, AudioQueueBufferRef buffer) {
        Impl* self = (Impl*)userData;
        if (!self->playing) {
            memset(buffer->mAudioData, 0, buffer->mAudioDataBytesCapacity);
            buffer->mAudioDataByteSize = buffer->mAudioDataBytesCapacity;
            AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
            return;
        }
        
        UInt32 bytesPerFrame = self->format.mBytesPerFrame;
        UInt32 totalFrames = (UInt32)(self->audioData.size() / self->format.mChannelsPerFrame);
        UInt32 framesPerBuffer = buffer->mAudioDataBytesCapacity / bytesPerFrame;
        
        float* out = (float*)buffer->mAudioData;
        UInt32 framesWritten = 0;
        
        while (framesWritten < framesPerBuffer) {
            size_t framesRemaining = totalFrames - self->playhead;
            UInt32 framesToCopy = (UInt32)std::min((size_t)(framesPerBuffer - framesWritten), framesRemaining);
            
            size_t sampleStart = self->playhead * self->format.mChannelsPerFrame;
            memcpy(out + framesWritten * self->format.mChannelsPerFrame,
                   self->audioData.data() + sampleStart,
                   framesToCopy * self->format.mChannelsPerFrame * sizeof(float));
            
            framesWritten += framesToCopy;
            self->playhead += framesToCopy;
            
            if (self->playhead >= totalFrames) {
                if (self->loop) {
                    self->playhead = 0;
                } else {
                    self->playing = false;
                    // fill remainder with silence
                    memset(out + framesWritten * self->format.mChannelsPerFrame, 0,
                           (framesPerBuffer - framesWritten) * self->format.mChannelsPerFrame * sizeof(float));
                    break;
                }
            }
        }
        
        // apply volume
        UInt32 totalSamples = framesPerBuffer * self->format.mChannelsPerFrame;
        for (UInt32 i = 0; i < totalSamples; i++) {
            out[i] *= self->volume;
        }
        
        buffer->mAudioDataByteSize = framesPerBuffer * bytesPerFrame;
        AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
    }
};

CoreAudioPlayer::CoreAudioPlayer() : impl(new Impl()) {}

CoreAudioPlayer::~CoreAudioPlayer() {
    stop();
    if (impl->queue) {
        AudioQueueDispose(impl->queue, true);
    }
    delete impl;
}

bool CoreAudioPlayer::load(const std::string& path) {
    stop();
    if (impl->queue) {
        AudioQueueDispose(impl->queue, true);
        impl->queue = nullptr;
    }
    impl->audioData.clear();
    impl->playhead = 0;

    CFStringRef cfPath = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, cfPath, kCFURLPOSIXPathStyle, false);
    CFRelease(cfPath);

    AudioFileID fileID;
    OSStatus err = AudioFileOpenURL(url, kAudioFileReadPermission, 0, &fileID);
    CFRelease(url);
    if (err != noErr) {
        NSLog(@"CoreAudioPlayer: failed to open file: %s (err=%d)", path.c_str(), (int)err);
        return false;
    }

    // get format
    UInt32 size = sizeof(impl->format);
    AudioFileGetProperty(fileID, kAudioFilePropertyDataFormat, &size, &impl->format);

    // convert to float if needed
    AudioStreamBasicDescription floatFormat;
    memset(&floatFormat, 0, sizeof(floatFormat));
    floatFormat.mSampleRate       = impl->format.mSampleRate;
    floatFormat.mFormatID         = kAudioFormatLinearPCM;
    floatFormat.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    floatFormat.mChannelsPerFrame = impl->format.mChannelsPerFrame;
    floatFormat.mBitsPerChannel   = 32;
    floatFormat.mBytesPerFrame    = 4 * floatFormat.mChannelsPerFrame;
    floatFormat.mFramesPerPacket  = 1;
    floatFormat.mBytesPerPacket   = floatFormat.mBytesPerFrame;

    ExtAudioFileRef extFile;
    CFStringRef cfPath2 = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    CFURLRef url2 = CFURLCreateWithFileSystemPath(nullptr, cfPath2, kCFURLPOSIXPathStyle, false);
    CFRelease(cfPath2);
    ExtAudioFileOpenURL(url2, &extFile);
    CFRelease(url2);
    ExtAudioFileSetProperty(extFile, kExtAudioFileProperty_ClientDataFormat,
                            sizeof(floatFormat), &floatFormat);

    SInt64 numFrames = 0;
    size = sizeof(numFrames);
    ExtAudioFileGetProperty(extFile, kExtAudioFileProperty_FileLengthFrames, &size, &numFrames);

    impl->audioData.resize(numFrames * floatFormat.mChannelsPerFrame);
    
    AudioBufferList bufList;
    bufList.mNumberBuffers = 1;
    bufList.mBuffers[0].mNumberChannels = floatFormat.mChannelsPerFrame;
    bufList.mBuffers[0].mDataByteSize = (UInt32)(impl->audioData.size() * sizeof(float));
    bufList.mBuffers[0].mData = impl->audioData.data();
    
    UInt32 framesToRead = (UInt32)numFrames;
    ExtAudioFileRead(extFile, &framesToRead, &bufList);
    ExtAudioFileDispose(extFile);
    AudioFileClose(fileID);

    impl->format = floatFormat;

    // create AudioQueue targeting system default output device
    err = AudioQueueNewOutput(&impl->format, Impl::callback, impl, nullptr, nullptr, 0, &impl->queue);
    if (err != noErr) {
        NSLog(@"CoreAudioPlayer: failed to create AudioQueue (err=%d)", (int)err);
        return false;
    }

    // explicitly set to system default output device
    AudioObjectPropertyAddress propAddr = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioDeviceID defaultDevice;
    UInt32 propSize = sizeof(defaultDevice);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &propAddr, 0, nullptr, &propSize, &defaultDevice);
    
    CFStringRef deviceUID = nullptr;
    AudioObjectPropertyAddress uidAddr = {
        kAudioDevicePropertyDeviceUID,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    propSize = sizeof(deviceUID);
    AudioObjectGetPropertyData(defaultDevice, &uidAddr, 0, nullptr, &propSize, &deviceUID);
    
    if (deviceUID) {
        AudioQueueSetProperty(impl->queue, kAudioQueueProperty_CurrentDevice, &deviceUID, sizeof(deviceUID));
        NSLog(@"CoreAudioPlayer: routing to device UID: %@", deviceUID);
        CFRelease(deviceUID);
    }

    // allocate buffers
    for (int i = 0; i < kNumBuffers; i++) {
        AudioQueueAllocateBuffer(impl->queue, kBufferSize, &impl->buffers[i]);
        impl->buffers[i]->mAudioDataByteSize = kBufferSize;
        memset(impl->buffers[i]->mAudioData, 0, kBufferSize);
        AudioQueueEnqueueBuffer(impl->queue, impl->buffers[i], 0, nullptr);
    }

    AudioQueueSetParameter(impl->queue, kAudioQueueParam_Volume, impl->volume);
    
    NSLog(@"CoreAudioPlayer: loaded %s (%lld frames, %d ch, %.0f Hz)",
          path.c_str(), numFrames, floatFormat.mChannelsPerFrame, floatFormat.mSampleRate);
    return true;
}

void CoreAudioPlayer::play() {
    if (!impl->queue) return;
    impl->playhead = 0;
    impl->playing = true;
    AudioQueueStart(impl->queue, nullptr);
}

void CoreAudioPlayer::stop() {
    if (!impl->queue) return;
    impl->playing = false;
    AudioQueueStop(impl->queue, true);
}

void CoreAudioPlayer::setLoop(bool loop) {
    impl->loop = loop;
}

void CoreAudioPlayer::setVolume(float volume) {
    impl->volume = volume;
    if (impl->queue) {
        AudioQueueSetParameter(impl->queue, kAudioQueueParam_Volume, volume);
    }
}

bool CoreAudioPlayer::isPlaying() {
    return impl->playing;
}