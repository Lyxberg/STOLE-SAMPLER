#pragma once

#include <JuceHeader.h>
#include "LoopbackCapture.h"

//==============================================================================
class StoleAudioProcessor final : public juce::AudioProcessor,
                                  private juce::Timer
{
public:
    StoleAudioProcessor();
    ~StoleAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "STOLE"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    static constexpr int kMaxSeconds   = 60;
    static constexpr int kPeakBuckets  = 2048;
    static constexpr int kLiveSize     = 4096;

    void     startRecording();
    void     stopRecording();
    bool     isRecording() const noexcept { return recording.load(); }
    bool     hasRecording() const;
    double   getRecordedSeconds() const;
    double   getRecordSampleRate() const;
    bool     isSystemSource() const;

    void     clearRecording();

    void     startPreview();
    void     stopPreview();
    bool     isPreviewing() const noexcept { return previewing.load(); }
    float    getPreviewProgress() const noexcept { return previewProgress.load(); }
    void     normalize();
    void     applyFade();
    void     reverse();

    juce::File getSaveFolder() const;
    void       setSaveFolder (const juce::File&);

    juce::File exportToFile (const juce::File& target, juce::String& error);
    juce::File exportTemp (juce::String& error);

    struct WaveView
    {
        std::vector<float> peakMin, peakMax;
        double seconds = 0.0;
        bool   valid   = false;
    };

    bool readWaveform (WaveView& dest) const;
    int  readLive (float* dest, int maxSamples) const;

    // live input level for the meter
    float  getInputLevel() const noexcept { return inputLevel.load(); }
    bool   isLoopbackRunning() const noexcept { return loopback.isRunning(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    void timerCallback() override;
    void appendAudio (const float* l, const float* r, int n, double sr);
    void feedLive (const float* l, const float* r, int n);
    void updateSource();

    LoopbackCapture loopback;

    std::vector<float> recL, recR;
    mutable juce::SpinLock recLock;
    std::atomic<int>  recLen { 0 };
    double recSR = 48000.0;
    int    maxLen = 0;

    std::array<float, kPeakBuckets> peakMin {}, peakMax {};

    mutable juce::SpinLock liveLock;
    std::array<float, kLiveSize> liveRing {};
    int livePos = 0;
    std::atomic<float> inputLevel { 0.0f };

    std::atomic<bool> recording { false };

    std::vector<float> previewL, previewR;
    juce::SpinLock previewLock;
    std::atomic<int>    previewLen { 0 };
    std::atomic<bool>   previewing { false };
    std::atomic<double> previewPos { 0.0 };
    std::atomic<float>  previewProgress { 0.0f };
    double previewSR = 48000.0;
    double lastSourceRate = 48000.0;
    double currentSampleRate = 48000.0;
    int    lastSource = -1;

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StoleAudioProcessor)
};
