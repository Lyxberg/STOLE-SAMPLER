#include "LoopbackCapture.h"

#if JUCE_WINDOWS

namespace
{
    // Defined locally so we never depend on the import libraries.
    const GUID kClsidMMDeviceEnumerator =
        { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };

    const GUID kIIDIMMDeviceEnumerator =
        { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };

    const GUID kIIDIAudioClient =
        { 0x1CB9AD4C, 0xDBFA, 0x4C32, { 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2 } };

    const GUID kIIDIAudioCaptureClient =
        { 0xC8ADBD64, 0xE71E, 0x48A0, { 0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17 } };

    const GUID kFloatSubFormat =
        { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
}

#endif

//==============================================================================
LoopbackCapture::LoopbackCapture()
    : juce::Thread ("STOLE loopback")
{
}

LoopbackCapture::~LoopbackCapture()
{
    stop();
}

bool LoopbackCapture::start()
{
   #if JUCE_WINDOWS
    if (isThreadRunning())
        return true;

    running = false;
    startThread (juce::Thread::Priority::high);
    return true;
   #else
    return false;
   #endif
}

void LoopbackCapture::stop()
{
    if (! isThreadRunning())
        return;

    signalThreadShouldExit();
    stopThread (800);
    running = false;
}

//==============================================================================
void LoopbackCapture::run()
{
   #if JUCE_WINDOWS
    const HRESULT hrCom = CoInitializeEx (nullptr, COINIT_MULTITHREADED);

    auto releaseAll = [this]()
    {
        if (capture    != nullptr) { capture->Release();    capture = nullptr; }
        if (client     != nullptr) { client->Stop(); client->Release(); client = nullptr; }
        if (mixFormat  != nullptr) { CoTaskMemFree (mixFormat);  mixFormat = nullptr; }
        if (enumerator != nullptr) { enumerator->Release(); enumerator = nullptr; }
    };

    HRESULT hr = CoCreateInstance (kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                   kIIDIMMDeviceEnumerator, (void**) &enumerator);

    IMMDevice* device = nullptr;

    if (SUCCEEDED (hr))
        hr = enumerator->GetDefaultAudioEndpoint (eRender, eConsole, &device);

    if (SUCCEEDED (hr))
    {
        hr = device->Activate (kIIDIAudioClient, CLSCTX_ALL, nullptr, (void**) &client);
        device->Release();
    }

    if (SUCCEEDED (hr))
        hr = client->GetMixFormat (&mixFormat);

    if (SUCCEEDED (hr) && mixFormat != nullptr)
    {
        sampleRate  = (double) mixFormat->nSamplesPerSec;
        numChannels = (int) mixFormat->nChannels;

        // 10 ms buffer, loopback of the default render endpoint
        hr = client->Initialize (AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_LOOPBACK,
                                 10000000, 0, mixFormat, nullptr);
    }

    if (SUCCEEDED (hr))
        hr = client->GetService (kIIDIAudioCaptureClient, (void**) &capture);

    if (SUCCEEDED (hr))
        hr = client->Start();

    if (FAILED (hr))
    {
        releaseAll();
        if (SUCCEEDED (hrCom)) CoUninitialize();
        running = false;
        return;
    }

    running = true;

    std::vector<float> frames;
    const bool isFloat = mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
                      || (mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE
                          && IsEqualGUID (((WAVEFORMATEXTENSIBLE*) mixFormat)->SubFormat, kFloatSubFormat));

    const int srcChannels = (int) mixFormat->nChannels;

    while (! threadShouldExit())
    {
        UINT32 packetFrames = 0;

        if (FAILED (capture->GetNextPacketSize (&packetFrames)))
            break;

        if (packetFrames == 0)
        {
            wait (8);
            continue;
        }

        BYTE*  data  = nullptr;
        UINT32 got   = 0;
        DWORD  flags = 0;

        if (FAILED (capture->GetBuffer (&data, &got, &flags, nullptr, nullptr)))
            break;

        if (got == 0)
        {
            capture->ReleaseBuffer (0);
            continue;
        }

        frames.resize ((size_t) got * 2, 0.0f);

        if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr)
        {
            // silence: leave the zeros
        }
        else if (isFloat)
        {
            const float* src = (const float*) data;

            for (UINT32 i = 0; i < got; ++i)
            {
                const int base = (int) i * srcChannels;
                frames[(size_t) i * 2]     = src[base];
                frames[(size_t) i * 2 + 1] = srcChannels > 1 ? src[base + 1] : src[base];
            }
        }
        else if (mixFormat->wBitsPerSample == 16)
        {
            const juce::int16* src = (const juce::int16*) data;

            for (UINT32 i = 0; i < got; ++i)
            {
                const int base = (int) i * srcChannels;
                frames[(size_t) i * 2]     = src[base] / 32768.0f;
                frames[(size_t) i * 2 + 1] = (srcChannels > 1 ? src[base + 1] : src[base]) / 32768.0f;
            }
        }
        else if (mixFormat->wBitsPerSample == 32)
        {
            const juce::int32* src = (const juce::int32*) data;

            for (UINT32 i = 0; i < got; ++i)
            {
                const int base = (int) i * srcChannels;
                frames[(size_t) i * 2]     = (float) (src[base] / 2147483648.0);
                frames[(size_t) i * 2 + 1] = (float) ((srcChannels > 1 ? src[base + 1] : src[base]) / 2147483648.0);
            }
        }

        if (onAudio != nullptr)
            onAudio (frames.data(), (int) got);

        capture->ReleaseBuffer (got);
    }

    releaseAll();

    if (SUCCEEDED (hrCom))
        CoUninitialize();

    running = false;
   #endif
}
