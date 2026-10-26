#pragma once

#include <JuceHeader.h>

#if JUCE_WINDOWS
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
 #include <mmdeviceapi.h>
 #include <audioclient.h>
#endif

//==============================================================================
/**
    Grabs whatever Windows is currently playing (WASAPI loopback of the default
    render device) on its own thread. The plugin only ever sees float frames.
*/
class LoopbackCapture : private juce::Thread
{
public:
    LoopbackCapture();
    ~LoopbackCapture() override;

    bool start();
    void stop();

    bool isRunning() const noexcept { return running.load(); }
    double getSampleRate() const noexcept { return sampleRate.load(); }
    int    getNumChannels() const noexcept { return numChannels.load(); }

    /** interleaved stereo frames, called on the capture thread */
    std::function<void (const float* interleaved, int numFrames)> onAudio;

private:
    void run() override;

    std::atomic<bool> running { false };
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int> numChannels { 2 };

    juce::String error;

   #if JUCE_WINDOWS
    IMMDeviceEnumerator* enumerator = nullptr;
    IAudioClient*        client     = nullptr;
    IAudioCaptureClient* capture    = nullptr;
    WAVEFORMATEX*        mixFormat  = nullptr;
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopbackCapture)
};
