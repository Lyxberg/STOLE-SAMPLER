#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "LoopbackCapture.h"
#include "PluginEditor.h"

//==============================================================================
static void recordDemo (StoleAudioProcessor& proc, double seconds)
{
    if (auto* p = proc.apvts.getParameter ("source"))
        p->setValueNotifyingHost (p->convertTo0to1 (1.0f));   // Input

    proc.prepareToPlay (48000.0, 512);
    proc.startRecording();

    const int total = (int) (48000.0 * seconds);
    const int block = 512;

    juce::Random rng (1234);
    juce::AudioBuffer<float> buf (2, block);
    int pos = 0;

    while (pos < total)
    {
        const int n = juce::jmin (block, total - pos);
        buf.clear();

        for (int i = 0; i < n; ++i)
        {
            const double t = (double) (pos + i) / 48000.0;
            const double phase = std::fmod (t, 0.25);
            const float  env = (float) std::exp (-phase * 16.0);
            const float  hit = 0.6f * env * std::sin ((float) (2.0 * juce::MathConstants<double>::pi * 90.0 * t));
            const float  grit = 0.08f * (rng.nextFloat() * 2.0f - 1.0f) * env;

            buf.setSample (0, i, hit + grit);
            buf.setSample (1, i, hit * 0.8f + grit);
        }

        juce::MidiBuffer midi;
        proc.processBlock (buf, midi);
        pos += n;
    }

    proc.stopRecording();
}

//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    const juce::String mode = argc > 1 ? juce::String (argv[1]) : juce::String();

    if (mode == "ui" && argc > 2)
    {
        StoleAudioProcessor proc;
        recordDemo (proc, 2.4);

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (760, 470);

        const auto img = ed->createComponentSnapshot (ed->getLocalBounds());

        juce::File out { juce::String (argv[2]) };
        out.deleteFile();

        juce::FileOutputStream os (out);
        juce::PNGImageFormat png;

        if (png.writeImageToStream (img, os))
            std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());

        ed.reset();
        return 0;
    }

    if (mode == "save" && argc > 2)
    {
        StoleAudioProcessor proc;
        recordDemo (proc, 2.0);

        juce::String error;
        const auto file = proc.exportToFile (juce::File { juce::String (argv[2]) }, error);

        std::printf ("export %s -> %s\n",
                     file.existsAsFile() ? "OK" : "FAILED",
                     file.existsAsFile() ? file.getFullPathName().toRawUTF8() : error.toRawUTF8());

        return file.existsAsFile() ? 0 : 1;
    }

    if (mode == "state")
    {
        StoleAudioProcessor a;
        a.setSaveFolder (juce::File ("D:\\AudioDev\\ref"));

        juce::MemoryBlock mb;
        a.getStateInformation (mb);

        StoleAudioProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());

        const auto folder = b.getSaveFolder();
        const bool ok = folder.getFullPathName() == "D:\\AudioDev\\ref";

        std::printf ("state -> folder %s : %s\n", folder.getFullPathName().toRawUTF8(),
                     ok ? "OK" : "LOST");
        return ok ? 0 : 1;
    }

    if (mode == "loopback")
    {
        // play a tone through the default output and check the loopback hears it
        struct ToneSource final : public juce::AudioIODeviceCallback
        {
            double phase = 0.0;
            double sr = 48000.0;

            void audioDeviceIOCallbackWithContext (const float* const*, int,
                                                   float* const* out, int numOut,
                                                   int n, const juce::AudioIODeviceCallbackContext&) override
            {
                for (int ch = 0; ch < numOut; ++ch)
                    if (out[ch] != nullptr)
                        for (int i = 0; i < n; ++i)
                            out[ch][i] = 0.25f * (float) std::sin (phase + juce::MathConstants<double>::twoPi
                                                                   * 440.0 * (double) i / sr);

                phase += juce::MathConstants<double>::twoPi * 440.0 * (double) n / sr;
            }

            void audioDeviceAboutToStart (juce::AudioIODevice* dev) override
            {
                sr = dev->getCurrentSampleRate();
            }

            void audioDeviceStopped() override {}
        };

        juce::AudioDeviceManager dm;
        const auto err = dm.initialiseWithDefaultDevices (0, 2);

        if (err.isNotEmpty())
        {
            std::printf ("audio device error: %s\n", err.toRawUTF8());
            return 1;
        }

        ToneSource tone;
        dm.addAudioCallback (&tone);

        LoopbackCapture cap;
        std::atomic<int> frames { 0 };
        std::atomic<float> peak { 0.0f };

        cap.onAudio = [&] (const float* d, int n)
        {
            frames += n;

            for (int i = 0; i < n * 2; ++i)
                peak = juce::jmax (peak.load(), std::abs (d[i]));
        };

        cap.start();
        juce::Thread::sleep (1600);
        cap.stop();
        dm.removeAudioCallback (&tone);

        const bool ok = frames.load() > 0 && peak.load() > 0.01f;

        std::printf ("loopback: %d frames, peak %.4f, %.0f Hz -> %s\n",
                     frames.load(), peak.load(), cap.getSampleRate(), ok ? "OK" : "FAILED");

        return ok ? 0 : 1;
    }

    if (mode == "preview")
    {
        StoleAudioProcessor proc;
        recordDemo (proc, 1.0);
        proc.startPreview();

        juce::AudioBuffer<float> buf (2, 512);
        double sum = 0.0;
        int cnt = 0;
        bool ended = false;

        for (int b = 0; b < 300; ++b)
        {
            buf.clear();
            juce::MidiBuffer m;
            proc.processBlock (buf, m);

            for (int i = 0; i < 512; ++i)
            {
                const double v = buf.getSample (0, i);
                sum += v * v;
                ++cnt;
            }

            if (! proc.isPreviewing())
            {
                ended = true;
                break;
            }
        }

        const double rms = std::sqrt (sum / juce::jmax (1, cnt));
        const bool ok = rms > 0.01 && ended;

        std::printf ("preview rms %.4f, finished %d -> %s\n", rms, (int) ended, ok ? "OK" : "FAILED");
        return ok ? 0 : 1;
    }

    std::printf ("usage: StoleTest ui <png> | save <wav> | state | loopback | preview\n");
    return 0;
}
