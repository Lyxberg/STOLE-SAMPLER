#include "PluginEditor.h"

namespace
{
    const juce::Colour kBgTop   { 0xff0a0e16 };
    const juce::Colour kBgBot   { 0xff0d1626 };
    const juce::Colour kPanel   { 0xff111827 };
    const juce::Colour kPanel2  { 0xff0d1421 };
    const juce::Colour kBorder  { 0xff28344a };
    const juce::Colour kGrid    { 0xff1a2231 };
    const juce::Colour kAccent  { 0xffe9ff3d };
    const juce::Colour kRed     { 0xffff4d5e };
    const juce::Colour kText    { 0xffdce3ee };
    const juce::Colour kDim     { 0xff8b95a8 };

    constexpr int kDesignW = 760;
    constexpr int kDesignH = 470;

    constexpr int kDisplayX = 16, kDisplayY = 58, kDisplayW = 728, kDisplayH = 230;
}

//==============================================================================
StoleAudioProcessorEditor::StoleAudioProcessorEditor (StoleAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setResizable (false, false);
    setSize (kDesignW, kDesignH);

    for (size_t i = 0; i < fftWindow.size(); ++i)
        fftWindow[i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                               * (float) i / (float) (fftWindow.size() - 1));

    spectrumDb.assign (1024, -90.0f);
    liveSamples.assign (2048, 0.0f);

    startTimerHz (30);
}

//==============================================================================
juce::Rectangle<int> StoleAudioProcessorEditor::displayBounds() const
{
    return { kDisplayX, kDisplayY, kDisplayW, kDisplayH };
}

juce::Rectangle<int> StoleAudioProcessorEditor::buttonBounds (int index) const
{
    switch (index)
    {
        case 0: return { 16, 326, 120, 52 };   // REC
        case 1: return { 144, 326, 88, 52 };   // PLAY
        case 2: return { 240, 326, 88, 52 };   // NORM
        case 3: return { 336, 326, 88, 52 };   // FADE
        case 4: return { 432, 326, 88, 52 };   // REV
        case 5: return { 528, 326, 88, 52 };   // SAVE
        default: return { 624, 326, 120, 52 }; // DRAG
    }
}

juce::Rectangle<int> StoleAudioProcessorEditor::sourceBounds (int index) const
{
    return index == 0 ? juce::Rectangle<int> (556, 16, 84, 30)
                      : juce::Rectangle<int> (648, 16, 84, 30);
}

juce::Rectangle<int> StoleAudioProcessorEditor::folderButtonBounds() const
{
    return { 664, 392, 80, 28 };
}

bool StoleAudioProcessorEditor::isOnButton (juce::Point<int> p, int index) const
{
    return buttonBounds (index).contains (p);
}

//==============================================================================
void StoleAudioProcessorEditor::timerCallback()
{
    processorRef.readWaveform (wave);

    if (juce::Time::getMillisecondCounterHiRes() > statusUntil)
        status.clear();

    repaint();
}

//==============================================================================
int StoleAudioProcessorEditor::getSource() const
{
    return (int) processorRef.apvts.getRawParameterValue ("source")->load();
}

void StoleAudioProcessorEditor::setSource (int index)
{
    if (auto* p = processorRef.apvts.getParameter ("source"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) index));

    repaint();
}

void StoleAudioProcessorEditor::setTrim (float startPct, float endPct)
{
    if (auto* p = processorRef.apvts.getParameter ("start"))
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (0.0f, 100.0f, startPct)));

    if (auto* p = processorRef.apvts.getParameter ("end"))
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (0.0f, 100.0f, endPct)));
}

//==============================================================================
void StoleAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    //--- background ---------------------------------------------------------
    g.setGradientFill (juce::ColourGradient (kBgTop, 0.0f, 0.0f, kBgBot, 0.0f, b.getBottom(), false));
    g.fillAll();

    // soft stage lights
    {
        juce::ColourGradient glow (juce::Colour (0xff1e5c9e).withAlpha (0.26f),
                                   b.getWidth() * 0.78f, b.getHeight() * 1.02f,
                                   juce::Colours::transparentBlack,
                                   b.getWidth() * 0.42f, b.getHeight() * 0.55f, true);
        g.setGradientFill (glow);
        g.fillRect (b);
    }

    {
        juce::ColourGradient glow (juce::Colour (0xff15243c).withAlpha (0.55f),
                                   b.getWidth() * 0.18f, -b.getHeight() * 0.15f,
                                   juce::Colours::transparentBlack,
                                   b.getWidth() * 0.55f, b.getHeight() * 0.45f, true);
        g.setGradientFill (glow);
        g.fillRect (b);
    }

    processorRef.readWaveform (wave);

    drawHeader (g);
    drawDisplay (g);
    drawButtons (g);
    drawFooter (g);

    //--- info row -----------------------------------------------------------
    {
        const double secs = processorRef.getRecordedSeconds();

        g.setColour (kDim);
        g.setFont (juce::Font (13.0f, juce::Font::bold));
        g.drawText (juce::String (secs, 2) + " s  /  " + juce::String (StoleAudioProcessor::kMaxSeconds) + " s",
                    juce::Rectangle<int> (18, 294, 300, 22), juce::Justification::centredLeft, false);

        juce::String right;

        if (processorRef.isSystemSource())
            right = processorRef.isLoopbackRunning() ? "LOOPBACK ACTIVE" : "LOOPBACK OFF";
        else
            right = "PLUGIN INPUT";

        g.setColour (processorRef.isSystemSource() && ! processorRef.isLoopbackRunning() ? kRed : kDim);
        g.drawText (right, juce::Rectangle<int> (420, 294, 324, 22), juce::Justification::centredRight, false);
    }

    //--- status message -----------------------------------------------------
    if (status.isNotEmpty())
    {
        g.setColour (kAccent);
        g.setFont (juce::Font (12.0f, juce::Font::bold));
        g.drawText (status, juce::Rectangle<int> (18, 436, 728, 20), juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (kDim.withAlpha (0.7f));
        g.setFont (juce::Font (12.0f, juce::Font::bold));
        g.drawText ("drag the waveform straight into your DAW", juce::Rectangle<int> (18, 436, 728, 20),
                    juce::Justification::centredLeft, false);
    }
}

//==============================================================================
void StoleAudioProcessorEditor::drawHeader (juce::Graphics& g)
{
    g.setColour (kAccent);
    g.setFont (juce::Font (34.0f, juce::Font::bold));
    g.drawText ("STOLE", juce::Rectangle<int> (18, 4, 220, 46), juce::Justification::centredLeft, false);

    g.setColour (kDim.withAlpha (0.85f));
    g.setFont (juce::Font (11.0f, juce::Font::bold));
    g.drawText ("SYSTEM AUDIO SAMPLER", juce::Rectangle<int> (152, 22, 240, 20),
                juce::Justification::centredLeft, false);

    for (int i = 0; i < 2; ++i)
    {
        auto r = sourceBounds (i);
        const bool on = getSource() == i;

        if (on)
        {
            g.setColour (kAccent);
            g.fillRoundedRectangle (r.toFloat(), 10.0f);
        }
        else
        {
            g.setColour (kPanel);
            g.fillRoundedRectangle (r.toFloat(), 10.0f);

            g.setColour (kBorder);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
        }

        g.setColour (on ? juce::Colour (0xff0a0e16) : kDim);
        g.setFont (juce::Font (12.0f, juce::Font::bold));
        g.drawText (i == 0 ? "SYSTEM" : "INPUT", r, juce::Justification::centred, false);
    }

    // level meter
    const juce::Rectangle<float> meter (18.0f, 50.0f, 724.0f, 3.0f);

    g.setColour (juce::Colour (0xff1a2231));
    g.fillRoundedRectangle (meter, 1.5f);

    const float level = juce::jlimit (0.0f, 1.0f, processorRef.getInputLevel());

    if (level > 0.001f)
    {
        g.setColour ((level > 0.98f ? kRed : kAccent).withAlpha (0.85f));
        g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * level), 1.5f);
    }
}

//==============================================================================
void StoleAudioProcessorEditor::drawDisplay (juce::Graphics& g)
{
    auto r = displayBounds();

    // shadow
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.toFloat().translated (0.0f, 3.0f), 14.0f);

    // body
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff141c2b), 0.0f, (float) r.getY(),
                                             juce::Colour (0xff0d1421), 0.0f, (float) r.getBottom(), false));
    g.fillRoundedRectangle (r.toFloat(), 14.0f);

    // top highlight + border
    g.setColour (juce::Colour (0xff3a4a6a).withAlpha (0.5f));
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 14.0f, 1.0f);

    g.setColour (kBorder.withAlpha (0.9f));
    g.drawRoundedRectangle (r.toFloat().reduced (1.5f), 13.0f, 1.0f);

    // grid
    {
        auto inner = r.reduced (10, 10);

        g.setColour (kGrid.withAlpha (0.9f));

        for (int i = 1; i < 8; ++i)
            g.drawVerticalLine (inner.getX() + inner.getWidth() * i / 8,
                                (float) inner.getY(), (float) inner.getBottom());

        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine (inner.getY() + inner.getHeight() * i / 4,
                                  (float) inner.getX(), (float) inner.getRight());
    }

    if (processorRef.isRecording() || ! processorRef.hasRecording())
        drawSpectrum (g);
    else
        drawWaveform (g);

    if (processorRef.isRecording())
    {
        const bool on = ((int) (juce::Time::getMillisecondCounter() / 400) % 2) == 0;

        if (on)
        {
            g.setColour (kRed);
            g.fillEllipse ((float) r.getRight() - 36.0f, (float) r.getY() + 16.0f, 10.0f, 10.0f);
        }

        g.setColour (kRed);
        g.setFont (juce::Font (13.0f, juce::Font::bold));
        g.drawText ("REC " + juce::String (processorRef.getRecordedSeconds(), 2) + " s",
                    juce::Rectangle<int> (r.getX() + 18, r.getY() + 12, 220, 22),
                    juce::Justification::centredLeft, false);
    }
}

//==============================================================================
void StoleAudioProcessorEditor::drawSpectrum (juce::Graphics& g)
{
    auto r = displayBounds().reduced (18, 20);

    const int n = processorRef.readLive (liveSamples.data(), (int) liveSamples.size());

    if (n <= 0)
        return;

    std::fill (fftData.begin(), fftData.end(), 0.0f);

    for (size_t i = 0; i < fftData.size(); ++i)
        fftData[i] = liveSamples[i] * fftWindow[i];

    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const float norm = 4.0f / (float) fftData.size();
    const int   w = r.getWidth();

    juce::Path path;

    for (int px = 0; px < w; ++px)
    {
        const float f = 20.0f * std::pow (1000.0f, (float) px / (float) juce::jmax (1, w));
        const int bin = juce::jlimit (1, (int) fftData.size() / 2 - 1,
                                      (int) (f / (float) processorRef.getRecordSampleRate() * (float) fftData.size()));

        const float mag = fftData[(size_t) bin] * norm;
        const float db  = juce::Decibels::gainToDecibels (mag, -80.0f);
        const float u   = juce::jlimit (0.0f, 1.0f, (db + 78.0f) / 78.0f);

        const float x = (float) r.getX() + (float) px;
        const float y = (float) r.getBottom() - u * (float) r.getHeight() * 0.95f;

        if (px == 0) path.startNewSubPath (x, y);
        else         path.lineTo (x, y);
    }

    juce::Path fill (path);
    fill.lineTo ((float) r.getRight(), (float) r.getBottom());
    fill.lineTo ((float) r.getX(), (float) r.getBottom());
    fill.closeSubPath();

    g.setColour (kAccent.withAlpha (0.06f));
    g.strokePath (path, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved));
    g.setColour (kAccent.withAlpha (0.14f));
    g.fillPath (fill);

    g.setColour (kAccent.withAlpha (0.9f));
    g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));

    if (! processorRef.hasRecording())
    {
        g.setColour (kDim.withAlpha (0.75f));
        g.setFont (juce::Font (12.0f, juce::Font::bold));
        g.drawText ("press REC to capture", juce::Rectangle<int> (r.getX(), r.getBottom() - 24, r.getWidth(), 18),
                    juce::Justification::centred, false);
    }
}

//==============================================================================
void StoleAudioProcessorEditor::drawWaveform (juce::Graphics& g)
{
    auto r = displayBounds().reduced (18, 16);

    if (! wave.valid || wave.peakMax.empty())
        return;

    const int   buckets = (int) wave.peakMax.size();
    const int   w = r.getWidth();

    const int   used = juce::jlimit (1, buckets,
                                     (int) std::round (wave.seconds / (double) StoleAudioProcessor::kMaxSeconds
                                                       * (double) buckets));

    const float midY = (float) r.getCentreY();
    const float halfH = (float) r.getHeight() * 0.46f;

    const float startPct = processorRef.apvts.getRawParameterValue ("start")->load();
    const float endPct   = processorRef.apvts.getRawParameterValue ("end")->load();

    const int sx = r.getX() + (int) (startPct * 0.01f * (float) w);
    const int ex = r.getX() + (int) (endPct   * 0.01f * (float) w);

    // envelope, sampled per pixel then smoothed so the edges read as a shape
    if ((int) envHi.size() != w)
    {
        envHi.assign ((size_t) w, 0.0f);
        envLo.assign ((size_t) w, 0.0f);
    }

    for (int px = 0; px < w; ++px)
    {
        const int b0 = px * used / juce::jmax (1, w);
        const int b1 = juce::jmin (used - 1, (px + 1) * used / juce::jmax (1, w));

        float lo = 0.0f, hi = 0.0f;

        for (int b = b0; b <= b1; ++b)
        {
            lo = juce::jmin (lo, wave.peakMin[(size_t) b]);
            hi = juce::jmax (hi, wave.peakMax[(size_t) b]);
        }

        envHi[(size_t) px] = hi;
        envLo[(size_t) px] = lo;
    }

    // two soft passes
    for (int pass = 0; pass < 2; ++pass)
    {
        float prevH = envHi[0], prevL = envLo[0];

        for (int px = 1; px < w - 1; ++px)
        {
            const float h = 0.25f * prevH + 0.5f * envHi[(size_t) px] + 0.25f * envHi[(size_t) (px + 1)];
            const float l = 0.25f * prevL + 0.5f * envLo[(size_t) px] + 0.25f * envLo[(size_t) (px + 1)];

            prevH = envHi[(size_t) px];
            prevL = envLo[(size_t) px];

            envHi[(size_t) px] = h;
            envLo[(size_t) px] = l;
        }
    }

    juce::Path env;

    for (int px = 0; px < w; ++px)
    {
        const float x = (float) r.getX() + (float) px;
        const float y = midY - juce::jmax (1.0f, envHi[(size_t) px] * halfH);

        if (px == 0) env.startNewSubPath (x, y);
        else         env.lineTo (x, y);
    }

    for (int px = w - 1; px >= 0; --px)
    {
        const float x = (float) r.getX() + (float) px;
        const float y = midY - juce::jmin (-1.0f, envLo[(size_t) px] * halfH);

        env.lineTo (x, y);
    }

    env.closeSubPath();

    // glow
    g.setColour (kAccent.withAlpha (0.07f));
    g.strokePath (env, juce::PathStrokeType (16.0f, juce::PathStrokeType::curved));
    g.setColour (kAccent.withAlpha (0.12f));
    g.strokePath (env, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved));

    g.setColour (kAccent.withAlpha (0.96f));
    g.fillPath (env);

    // dim outside the trim range
    if (startPct > 0.05f)
    {
        g.setColour (juce::Colour (0xff0a0e16).withAlpha (0.72f));
        g.fillRect (juce::Rectangle<int> (r.getX(), r.getY(), sx - r.getX(), r.getHeight()));
    }

    if (endPct < 99.95f)
    {
        g.setColour (juce::Colour (0xff0a0e16).withAlpha (0.72f));
        g.fillRect (juce::Rectangle<int> (ex, r.getY(), r.getRight() - ex, r.getHeight()));
    }

    // trim handles
    for (int i = 0; i < 2; ++i)
    {
        const int x = i == 0 ? sx : ex;

        g.setColour (kText.withAlpha (0.85f));
        g.drawVerticalLine (x, (float) r.getY(), (float) r.getBottom());

        const juce::Rectangle<float> tab ((float) x - 5.0f, (float) r.getY(), 10.0f, 22.0f);

        g.setColour (kText);
        g.fillRoundedRectangle (tab, 4.0f);

        g.setColour (juce::Colour (0xff0a0e16));
        g.drawVerticalLine (x, (float) r.getY() + 7.0f, (float) r.getY() + 15.0f);
    }

    // playhead
    if (processorRef.isPreviewing())
    {
        const float prog = juce::jlimit (0.0f, 1.0f, processorRef.getPreviewProgress());
        const int x = sx + (int) ((float) (ex - sx) * prog);

        g.setColour (kRed);
        g.drawVerticalLine (x, (float) r.getY(), (float) r.getBottom());
    }
}

//==============================================================================
void StoleAudioProcessorEditor::drawButton (juce::Graphics& g, juce::Rectangle<int> r,
                                             const juce::String& label, bool active, bool accent) const
{
    const auto rf = r.toFloat();

    if (accent)
    {
        g.setColour (active ? kRed : kAccent);
        g.fillRoundedRectangle (rf, 12.0f);

        g.setColour (juce::Colour (0xff0a0e16));
    }
    else
    {
        g.setColour (active ? juce::Colour (0xff1d2635) : juce::Colour (0xff131b29));
        g.fillRoundedRectangle (rf, 12.0f);

        g.setColour (kBorder.withAlpha (0.9f));
        g.drawRoundedRectangle (rf.reduced (0.5f), 12.0f, 1.0f);

        g.setColour (active ? kText : kDim);
    }

    g.setFont (juce::Font (13.0f, juce::Font::bold));
    g.drawText (label, r, juce::Justification::centred, false);
}

void StoleAudioProcessorEditor::drawButtons (juce::Graphics& g)
{
    const bool rec  = processorRef.isRecording();
    const bool play = processorRef.isPreviewing();
    const bool has  = processorRef.hasRecording();

    drawButton (g, buttonBounds (0), rec ? "STOP" : "REC", rec, true);
    drawButton (g, buttonBounds (1), play ? "STOP" : "PLAY", play, has);
    drawButton (g, buttonBounds (2), "NORM", has, false);
    drawButton (g, buttonBounds (3), "FADE", has, false);
    drawButton (g, buttonBounds (4), "REV", has, false);
    drawButton (g, buttonBounds (5), "SAVE", has, false);
    drawButton (g, buttonBounds (6), "DRAG OUT", has, false);
}

void StoleAudioProcessorEditor::drawFooter (juce::Graphics& g)
{
    g.setColour (kDim);
    g.setFont (juce::Font (11.0f, juce::Font::bold));
    g.drawText ("SAVE TO", juce::Rectangle<int> (18, 392, 70, 28), juce::Justification::centredLeft, false);

    auto field = juce::Rectangle<float> (86.0f, 392.0f, 568.0f, 28.0f);

    g.setColour (juce::Colour (0xff101724));
    g.fillRoundedRectangle (field, 9.0f);

    g.setColour (kBorder.withAlpha (0.85f));
    g.drawRoundedRectangle (field.reduced (0.5f), 9.0f, 1.0f);

    g.setColour (kText.withAlpha (0.8f));
    g.setFont (juce::Font (11.0f));
    g.drawText (processorRef.getSaveFolder().getFullPathName(),
                juce::Rectangle<int> (98, 392, 548, 28), juce::Justification::centredLeft, true);

    drawButton (g, folderButtonBounds(), "...", false, false);
}

//==============================================================================
void StoleAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();

    if (sourceBounds (0).contains (p)) { setSource (0); return; }
    if (sourceBounds (1).contains (p)) { setSource (1); return; }

    for (int i = 0; i < 7; ++i)
    {
        if (! isOnButton (p, i))
            continue;

        switch (i)
        {
            case 0:
                if (processorRef.isRecording())
                {
                    processorRef.stopRecording();
                    status = "captured " + juce::String (processorRef.getRecordedSeconds(), 2) + " s";
                    statusUntil = juce::Time::getMillisecondCounterHiRes() + 3000.0;
                }
                else
                {
                    processorRef.startRecording();
                }
                break;

            case 1:
                if (processorRef.isPreviewing())
                {
                    processorRef.stopPreview();
                }
                else if (processorRef.hasRecording())
                {
                    processorRef.startPreview();
                }
                else
                {
                    status = "nothing recorded";
                    statusUntil = juce::Time::getMillisecondCounterHiRes() + 2000.0;
                }
                break;

            case 2: processorRef.normalize(); status = "normalized"; statusUntil = juce::Time::getMillisecondCounterHiRes() + 2000.0; break;
            case 3: processorRef.applyFade(); status = "fade applied"; statusUntil = juce::Time::getMillisecondCounterHiRes() + 2000.0; break;
            case 4: processorRef.reverse();   status = "reversed";     statusUntil = juce::Time::getMillisecondCounterHiRes() + 2000.0; break;
            case 5: doSave(); break;
            default: doDragOut(); break;
        }

        repaint();
        return;
    }

    if (folderButtonBounds().contains (p))
    {
        browseFolder();
        return;
    }

    if (displayBounds().contains (p) && processorRef.hasRecording() && ! processorRef.isRecording())
    {
        auto r = displayBounds().reduced (18, 16);
        const int w = r.getWidth();

        const int sx = r.getX() + (int) (processorRef.apvts.getRawParameterValue ("start")->load() * 0.01f * (float) w);
        const int ex = r.getX() + (int) (processorRef.apvts.getRawParameterValue ("end")->load() * 0.01f * (float) w);

        if (std::abs (p.x - sx) <= 10) { dragMode = 1; return; }
        if (std::abs (p.x - ex) <= 10) { dragMode = 2; return; }

        mayDragOut = true;
        dragOrigin = p;
    }
}

void StoleAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMode == 1 || dragMode == 2)
    {
        auto r = displayBounds().reduced (18, 16);
        const float u = juce::jlimit (0.0f, 1.0f, (float) (e.getPosition().x - r.getX()) / (float) r.getWidth());

        const float start = processorRef.apvts.getRawParameterValue ("start")->load();
        const float end   = processorRef.apvts.getRawParameterValue ("end")->load();

        if (dragMode == 1) setTrim (juce::jmin (end - 0.5f, u * 100.0f), end);
        else               setTrim (start, juce::jmax (start + 0.5f, u * 100.0f));

        repaint();
        return;
    }

    if (mayDragOut && e.getDistanceFromDragStart() > 8)
    {
        mayDragOut = false;
        doDragOut();
    }
}

void StoleAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
{
    dragMode = 0;
    mayDragOut = false;
}

//==============================================================================
void StoleAudioProcessorEditor::doSave()
{
    if (! processorRef.hasRecording())
    {
        status = "nothing recorded";
        statusUntil = juce::Time::getMillisecondCounterHiRes() + 2000.0;
        return;
    }

    auto folder = processorRef.getSaveFolder();
    folder.createDirectory();

    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S");
    const auto target = folder.getChildFile ("STOLE_" + stamp + ".wav");

    juce::String error;
    const auto file = processorRef.exportToFile (target, error);

    status = file.existsAsFile() ? "saved " + file.getFileName() : ("save failed: " + error);
    statusUntil = juce::Time::getMillisecondCounterHiRes() + 4000.0;
    repaint();
}

void StoleAudioProcessorEditor::doDragOut()
{
    if (! processorRef.hasRecording())
    {
        status = "nothing recorded";
        statusUntil = juce::Time::getMillisecondCounterHiRes() + 2000.0;
        repaint();
        return;
    }

    juce::String error;
    const auto file = processorRef.exportTemp (error);

    if (! file.existsAsFile())
    {
        status = "export failed: " + error;
        statusUntil = juce::Time::getMillisecondCounterHiRes() + 3000.0;
        repaint();
        return;
    }

    juce::StringArray files;
    files.add (file.getFullPathName());

    juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this);

    status = "dragging " + file.getFileName();
    statusUntil = juce::Time::getMillisecondCounterHiRes() + 3000.0;
    repaint();
}

void StoleAudioProcessorEditor::browseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Where should STOLE save?",
                                                   processorRef.getSaveFolder());

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();

                              if (f != juce::File())
                              {
                                  processorRef.setSaveFolder (f);
                                  status = "save folder updated";
                                  statusUntil = juce::Time::getMillisecondCounterHiRes() + 2000.0;
                                  repaint();
                              }
                          });
}
