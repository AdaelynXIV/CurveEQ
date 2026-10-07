#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "Dsp.h"
#include "LinearPhase.h"

class EqProcessor : public juce::AudioProcessor,
                    private juce::AsyncUpdater,
                    private juce::AudioProcessorValueTreeState::Listener
{
public:
    EqProcessor();
    ~EqProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Curve EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // editor access
    static constexpr int kRing = 16384;
    void copyLatest (float* dest, int num) const;
    float getDynAmt (int band) const { return dynAmt[(size_t) band].load (std::memory_order_relaxed); }
    bool isLinearPhase() const { return phaseP->load() > 0.5f; }
    int latencyForSettings() const;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct BandParams { std::atomic<float> *on, *type, *freq, *gain, *q, *slope, *mode, *dyn, *range, *thr, *att, *rel, *dir; };
    struct BandState
    {
        double z[2][eq::kMaxSections][2] {};
        double dz[2] {};          // detector filter state
        double env = 0.0;         // detector envelope
        double cf = 0, cg = 0, cq = 0; // smoothed log-freq, gain, log-Q
        bool wasOn = false;
    };
    struct Snap { int type, mode, slope, dir; bool dyn; float freq, gain, q, range, thr, att, rel; };

    Snap snapshot (int band) const;
    float detect (BandState&, const Snap&, int nCh, const float* l, const float* r, int m, double freq);
    void processMinPhase (float* L, float* R, int n);
    void processLinear (float* L, float* R, int n);
    void runConvolution (float* L, float* R, int n);
    void rebuildIR (std::vector<float>& lastSignature);

    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { setLatencySamples (latencyForSettings()); }

    struct IRThread;
    std::unique_ptr<IRThread> irThread;

    std::array<BandParams, eq::kNumBands> bp {};
    std::array<BandState, eq::kNumBands> state {};
    std::array<std::atomic<float>, eq::kNumBands> dynAmt {};
    std::atomic<float> *outGain = nullptr, *phaseP = nullptr, *qualityP = nullptr;
    double fs = 44100.0;
    int maxBlock = 512;
    std::atomic<bool> mono { false }, crossActive { false };

    juce::dsp::Convolution conv[4];
    juce::AudioBuffer<float> tmp;

    std::array<float, kRing> ring {};
    std::atomic<int> wpos { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqProcessor)
};
