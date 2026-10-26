#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class StoleAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit StoleAudioProcessorEditor (StoleAudioProcessor&);
    ~StoleAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override {}

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    void drawHeader (juce::Graphics&);
    void drawDisplay (juce::Graphics&);
    void drawWaveform (juce::Graphics&);
    void drawSpectrum (juce::Graphics&);
    void drawButtons (juce::Graphics&);
    void drawFooter (juce::Graphics&);

    void drawButton (juce::Graphics&, juce::Rectangle<int>, const juce::String&,
                     bool active, bool accent) const;

    juce::Rectangle<int> displayBounds() const;
    juce::Rectangle<int> buttonBounds (int index) const;
    juce::Rectangle<int> sourceBounds (int index) const;
    juce::Rectangle<int> folderButtonBounds() const;

    void setSource (int index);
    int  getSource() const;
    void setTrim (float startPct, float endPct);

    void doSave();
    void doDragOut();
    void browseFolder();

    bool isOnButton (juce::Point<int>, int index) const;

    StoleAudioProcessor& processorRef;

    juce::dsp::FFT fft { 11 };
    std::array<float, 2048> fftData {};
    std::array<float, 2048> fftWindow {};
    std::vector<float> spectrumDb;

    StoleAudioProcessor::WaveView wave;
    std::vector<float> envHi, envLo;
    std::vector<float> liveSamples;

    int   dragMode = 0;          // 0 none, 1 trim start, 2 trim end, 3 external drag
    float dragOffset = 0.0f;
    juce::Point<int> dragOrigin;
    bool  mayDragOut = false;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status;
    double statusUntil = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StoleAudioProcessorEditor)
};
