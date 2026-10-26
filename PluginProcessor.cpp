#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
StoleAudioProcessor::StoleAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    loopback.onAudio = [this] (const float* interleaved, int frames)
    {
        static thread_local std::vector<float> l, r;

        l.resize ((size_t) frames);
        r.resize ((size_t) frames);

        for (int i = 0; i < frames; ++i)
        {
            l[(size_t) i] = interleaved[(size_t) i * 2];
            r[(size_t) i] = interleaved[(size_t) i * 2 + 1];
        }

        feedLive (l.data(), r.data(), frames);

        if (recording.load() && isSystemSource())
            appendAudio (l.data(), r.data(), frames, loopback.getSampleRate());
    };

    startTimerHz (15);
}

StoleAudioProcessor::~StoleAudioProcessor()
{
    stopTimer();
    loopback.stop();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout StoleAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "source", 1 }, "Source",
        juce::StringArray { "System", "Input" }, 0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "start", 1 }, "Trim Start",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "end", 1 }, "Trim End",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), 100.0f));

    return layout;
}

//==============================================================================
void StoleAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    updateSource();
}

void StoleAudioProcessor::releaseResources()
{
    loopback.stop();
}

bool StoleAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void StoleAudioProcessor::updateSource()
{
    if (isSystemSource())
        loopback.start();
    else
        loopback.stop();
}

bool StoleAudioProcessor::isSystemSource() const
{
    return (int) apvts.getRawParameterValue ("source")->load() == 0;
}

//==============================================================================
void StoleAudioProcessor::feedLive (const float* l, const float* r, int n)
{
    if (n <= 0)
        return;

    float peak = 0.0f;

    const juce::SpinLock::ScopedLockType sl (liveLock);

    for (int i = 0; i < n; ++i)
    {
        liveRing[(size_t) livePos] = 0.5f * (l[i] + r[i]);
        livePos = (livePos + 1) % kLiveSize;

        peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
    }

    inputLevel = juce::jmax (inputLevel.load(), peak);
}

void StoleAudioProcessor::appendAudio (const float* l, const float* r, int n, double sr)
{
    const juce::SpinLock::ScopedLockType sl (recLock);

    if (recL.empty() || std::abs (sr - recSR) > 1.0)
    {
        recSR = sr > 0.0 ? sr : 48000.0;
        maxLen = (int) (recSR * (double) kMaxSeconds);

        recL.assign ((size_t) maxLen, 0.0f);
        recR.assign ((size_t) maxLen, 0.0f);

        recLen = 0;
        peakMin.fill (0.0f);
        peakMax.fill (0.0f);
    }

    const int start = recLen.load();
    const int count = juce::jmin (n, maxLen - start);

    if (count <= 0)
    {
        recording = false;
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        recL[(size_t) (start + i)] = l[i];
        recR[(size_t) (start + i)] = r[i];
    }

    // fixed resolution peaks so the editor never scans the whole buffer
    for (int i = 0; i < count; ++i)
    {
        const int bucket = juce::jlimit (0, kPeakBuckets - 1,
                                         (int) ((juce::int64) (start + i) * kPeakBuckets / juce::jmax (1, maxLen)));

        peakMin[(size_t) bucket] = juce::jmin (peakMin[(size_t) bucket], l[i], r[i]);
        peakMax[(size_t) bucket] = juce::jmax (peakMax[(size_t) bucket], l[i], r[i]);
    }

    recLen = start + count;
}

//==============================================================================
void StoleAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // effect: audio passes straight through. We only listen to it.
    const int n = buffer.getNumSamples();

    if (n <= 0)
        return;

    const int src = (int) apvts.getRawParameterValue ("source")->load();

    if (src != lastSource)
    {
        lastSource = src;
        updateSource();
    }

    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : l;

    feedLive (l, r, n);

    if (recording.load() && src == 1)
        appendAudio (l, r, n, currentSampleRate);

    //--- preview playback ---------------------------------------------------
    if (previewing.load())
    {
        const juce::SpinLock::ScopedTryLockType psl (previewLock);

        if (psl.isLocked())
        {
            const int len = previewLen.load();

            if (len < 2)
            {
                previewing = false;
            }
            else
            {
                float* outL = buffer.getWritePointer (0);
                float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

                const double ratio = previewSR / currentSampleRate;
                double p = previewPos.load();

                for (int i = 0; i < n; ++i)
                {
                    if (p >= (double) (len - 1))
                    {
                        previewing = false;
                        previewProgress = 1.0f;
                        break;
                    }

                    const int   i0 = (int) p;
                    const float fr = (float) (p - (double) i0);

                    outL[i] += previewL[(size_t) i0] + (previewL[(size_t) i0 + 1] - previewL[(size_t) i0]) * fr;

                    if (outR != nullptr)
                        outR[i] += previewR[(size_t) i0] + (previewR[(size_t) i0 + 1] - previewR[(size_t) i0]) * fr;

                    p += ratio;
                }

                previewPos = p;
                previewProgress = juce::jlimit (0.0f, 1.0f, (float) (p / (double) len));
            }
        }
    }
}

//==============================================================================
void StoleAudioProcessor::startRecording()
{
    stopRecording();
    stopPreview();

    {
        const juce::SpinLock::ScopedLockType sl (recLock);

        recL.clear();
        recR.clear();
        recLen = 0;
        maxLen = 0;
        peakMin.fill (0.0f);
        peakMax.fill (0.0f);
    }

    if (isSystemSource() && ! loopback.isRunning())
        loopback.start();

    recording = true;
}

void StoleAudioProcessor::stopRecording()
{
    recording = false;
}

bool StoleAudioProcessor::hasRecording() const
{
    return recLen.load() > 8;
}

double StoleAudioProcessor::getRecordedSeconds() const
{
    const juce::SpinLock::ScopedLockType sl (recLock);
    return recSR > 0.0 ? (double) recLen.load() / recSR : 0.0;
}

double StoleAudioProcessor::getRecordSampleRate() const
{
    const juce::SpinLock::ScopedLockType sl (recLock);
    return recSR;
}

void StoleAudioProcessor::clearRecording()
{
    recording = false;
    stopPreview();

    const juce::SpinLock::ScopedLockType sl (recLock);

    recL.clear();
    recR.clear();
    recLen = 0;
    maxLen = 0;
    peakMin.fill (0.0f);
    peakMax.fill (0.0f);
}

//==============================================================================
namespace
{
    juce::Range<int> trimRange (int len, float startPct, float endPct)
    {
        int a = (int) (juce::jlimit (0.0f, 1.0f, startPct * 0.01f) * (float) len);
        int b = (int) (juce::jlimit (0.0f, 1.0f, endPct   * 0.01f) * (float) len);

        if (b <= a) b = juce::jmin (len, a + 1);

        return { juce::jlimit (0, len, a), juce::jlimit (0, len, b) };
    }
}

//==============================================================================
void StoleAudioProcessor::startPreview()
{
    stopPreview();

    const juce::SpinLock::ScopedLockType psl (previewLock);

    previewing = false;

    const juce::SpinLock::ScopedLockType sl (recLock);

    if (recLen.load() < 8)
        return;

    const auto range = trimRange (recLen.load(), apvts.getRawParameterValue ("start")->load(),
                                                  apvts.getRawParameterValue ("end")->load());

    previewL.assign (recL.begin() + range.getStart(), recL.begin() + range.getEnd());
    previewR.assign (recR.begin() + range.getStart(), recR.begin() + range.getEnd());

    previewLen = (int) previewL.size();
    previewSR  = recSR;
    previewPos = 0.0;
    previewProgress = 0.0f;
    previewing = true;
}

void StoleAudioProcessor::stopPreview()
{
    previewing = false;
    previewProgress = 0.0f;
}

//==============================================================================
void StoleAudioProcessor::timerCallback()
{
    inputLevel.store (inputLevel.load() * 0.82f);

    if (inputLevel < 1.0e-4f)
        inputLevel = 0.0f;
}

//==============================================================================
bool StoleAudioProcessor::readWaveform (WaveView& dest) const
{
    const juce::SpinLock::ScopedLockType sl (recLock);

    if (recLen.load() < 8 || recL.empty())
    {
        dest.valid = false;
        return false;
    }

    dest.peakMin.assign (peakMin.begin(), peakMin.end());
    dest.peakMax.assign (peakMax.begin(), peakMax.end());
    dest.seconds = (double) recLen.load() / juce::jmax (1.0, recSR);
    dest.valid = true;
    return true;
}

int StoleAudioProcessor::readLive (float* dest, int maxSamples) const
{
    const juce::SpinLock::ScopedLockType sl (liveLock);

    const int n = juce::jmin (maxSamples, kLiveSize);

    for (int i = 0; i < n; ++i)
        dest[i] = liveRing[(size_t) ((livePos + kLiveSize - n + i) % kLiveSize)];

    return n;
}

void StoleAudioProcessor::normalize()
{
    const juce::SpinLock::ScopedLockType sl (recLock);

    const auto range = trimRange (recLen.load(), apvts.getRawParameterValue ("start")->load(),
                                                  apvts.getRawParameterValue ("end")->load());

    float peak = 0.0f;

    for (int i = range.getStart(); i < range.getEnd(); ++i)
        peak = juce::jmax (peak, std::abs (recL[(size_t) i]), std::abs (recR[(size_t) i]));

    if (peak < 1.0e-6f)
        return;

    const float g = juce::Decibels::decibelsToGain (-0.5f) / peak;

    for (int i = range.getStart(); i < range.getEnd(); ++i)
    {
        recL[(size_t) i] *= g;
        recR[(size_t) i] *= g;
    }

    // refresh the display peaks
    peakMin.fill (0.0f);
    peakMax.fill (0.0f);

    for (int i = 0; i < recLen.load(); ++i)
    {
        const int bucket = juce::jlimit (0, kPeakBuckets - 1,
                                         (int) ((juce::int64) i * kPeakBuckets / juce::jmax (1, maxLen)));
        peakMin[(size_t) bucket] = juce::jmin (peakMin[(size_t) bucket], recL[(size_t) i], recR[(size_t) i]);
        peakMax[(size_t) bucket] = juce::jmax (peakMax[(size_t) bucket], recL[(size_t) i], recR[(size_t) i]);
    }
}

void StoleAudioProcessor::applyFade()
{
    const juce::SpinLock::ScopedLockType sl (recLock);

    const auto range = trimRange (recLen.load(), apvts.getRawParameterValue ("start")->load(),
                                                  apvts.getRawParameterValue ("end")->load());

    const int len = range.getLength();
    const int fade = juce::jmin (len / 2, (int) (recSR * 0.01));

    if (fade < 1)
        return;

    for (int i = 0; i < fade; ++i)
    {
        const float g = (float) i / (float) fade;

        recL[(size_t) (range.getStart() + i)] *= g;
        recR[(size_t) (range.getStart() + i)] *= g;

        recL[(size_t) (range.getEnd() - 1 - i)] *= g;
        recR[(size_t) (range.getEnd() - 1 - i)] *= g;
    }

    peakMin.fill (0.0f);
    peakMax.fill (0.0f);

    for (int i = 0; i < recLen.load(); ++i)
    {
        const int bucket = juce::jlimit (0, kPeakBuckets - 1,
                                         (int) ((juce::int64) i * kPeakBuckets / juce::jmax (1, maxLen)));
        peakMin[(size_t) bucket] = juce::jmin (peakMin[(size_t) bucket], recL[(size_t) i], recR[(size_t) i]);
        peakMax[(size_t) bucket] = juce::jmax (peakMax[(size_t) bucket], recL[(size_t) i], recR[(size_t) i]);
    }
}

void StoleAudioProcessor::reverse()
{
    const juce::SpinLock::ScopedLockType sl (recLock);

    const auto range = trimRange (recLen.load(), apvts.getRawParameterValue ("start")->load(),
                                                  apvts.getRawParameterValue ("end")->load());

    std::reverse (recL.begin() + range.getStart(), recL.begin() + range.getEnd());
    std::reverse (recR.begin() + range.getStart(), recR.begin() + range.getEnd());

    peakMin.fill (0.0f);
    peakMax.fill (0.0f);

    for (int i = 0; i < recLen.load(); ++i)
    {
        const int bucket = juce::jlimit (0, kPeakBuckets - 1,
                                         (int) ((juce::int64) i * kPeakBuckets / juce::jmax (1, maxLen)));
        peakMin[(size_t) bucket] = juce::jmin (peakMin[(size_t) bucket], recL[(size_t) i], recR[(size_t) i]);
        peakMax[(size_t) bucket] = juce::jmax (peakMax[(size_t) bucket], recL[(size_t) i], recR[(size_t) i]);
    }
}

//==============================================================================
juce::File StoleAudioProcessor::getSaveFolder() const
{
    auto stored = apvts.state.getProperty ("folder").toString();

    if (stored.isNotEmpty())
    {
        juce::File f (stored);

        if (f.isDirectory())
            return f;
    }

    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
             .getChildFile ("STOLE");
}

void StoleAudioProcessor::setSaveFolder (const juce::File& folder)
{
    apvts.state.setProperty ("folder", folder.getFullPathName(), nullptr);
}

juce::File StoleAudioProcessor::exportToFile (const juce::File& target, juce::String& error)
{
    const juce::SpinLock::ScopedLockType sl (recLock);

    if (recLen.load() < 8)
    {
        error = "Nothing recorded";
        return {};
    }

    const auto range = trimRange (recLen.load(), apvts.getRawParameterValue ("start")->load(),
                                                  apvts.getRawParameterValue ("end")->load());

    const int len = range.getLength();

    juce::AudioBuffer<float> buf (2, len);
    std::copy (recL.begin() + range.getStart(), recL.begin() + range.getEnd(), buf.getWritePointer (0));
    std::copy (recR.begin() + range.getStart(), recR.begin() + range.getEnd(), buf.getWritePointer (1));

    target.getParentDirectory().createDirectory();
    target.deleteFile();

    auto stream = std::make_unique<juce::FileOutputStream> (target);

    if (! stream->openedOk())
    {
        error = "Cannot write " + target.getFullPathName();
        return {};
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.release(), recSR, 2, 24, {}, 0));

    if (writer == nullptr)
    {
        error = "Cannot create WAV writer";
        return {};
    }

    writer->writeFromAudioSampleBuffer (buf, 0, len);
    writer->flush();

    return target;
}

juce::File StoleAudioProcessor::exportTemp (juce::String& error)
{
    auto folder = getSaveFolder();
    folder.createDirectory();

    if (! folder.isDirectory())
        folder = juce::File::getSpecialLocation (juce::File::tempDirectory);

    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S");
    return exportToFile (folder.getChildFile ("STOLE_" + stamp + ".wav"), error);
}

//==============================================================================
void StoleAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void StoleAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));

    lastSource = (int) apvts.getRawParameterValue ("source")->load();
    updateSource();
}

//==============================================================================
juce::AudioProcessorEditor* StoleAudioProcessor::createEditor()
{
    return new StoleAudioProcessorEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StoleAudioProcessor();
}
