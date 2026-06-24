#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

class SpectrumAnalyzer
{
public:
    static constexpr int kFFTOrder = 11;
    static constexpr int kFFTSize  = 1 << kFFTOrder; // 2048

    SpectrumAnalyzer()
        : forwardFFT (kFFTOrder),
          window     (kFFTSize, juce::dsp::WindowingFunction<float>::hann)
    {}

    void pushSamples(const float* data, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            pushSample(data[i]);
    }

    void pushSample(float sample) noexcept
    {
        if (fifoIndex >= kFFTSize)
        {
            if (!nextBlockReady.load(std::memory_order_relaxed))
            {
                std::fill(fftData.begin(), fftData.end(), 0.0f);
                std::copy(fifo.begin(), fifo.end(), fftData.begin());
                window.multiplyWithWindowingTable(fftData.data(), kFFTSize);
                forwardFFT.performFrequencyOnlyForwardTransform(fftData.data());
                nextBlockReady.store(true, std::memory_order_release);
            }
            fifoIndex = 0;
        }
        fifo[static_cast<size_t>(fifoIndex++)] = sample;
    }

    // Call from GUI thread. Returns true if new data was available.
    bool getNextFFTData(std::array<float, kFFTSize>& dest) noexcept
    {
        if (!nextBlockReady.load(std::memory_order_acquire))
            return false;

        std::copy(fftData.begin(), fftData.begin() + kFFTSize, dest.begin());
        nextBlockReady.store(false, std::memory_order_release);
        return true;
    }

private:
    juce::dsp::FFT                         forwardFFT;
    juce::dsp::WindowingFunction<float>    window;
    std::array<float, kFFTSize>            fifo{};
    std::array<float, kFFTSize * 2>        fftData{};
    int                                    fifoIndex = 0;
    std::atomic<bool>                      nextBlockReady { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzer)
};
