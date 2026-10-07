#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

// Flat, dark look for knobs, combo boxes and switches.
struct ProLook : public juce::LookAndFeel_V4
{
    ProLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
};

class EqEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EqEditor (EqProcessor&);
    ~EqEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    static constexpr int kFft = 4096;

    void timerCallback() override;
    void selectBand (int);
    int hitTest (juce::Point<float>) const;
    float freqToX (float f) const;
    float xToFreq (float x) const;
    float dbToY (float d) const;
    float yToDb (float y) const;
    float get (int band, const char* id) const;
    void setParam (int band, const char* id, float plain);
    void gesture (int band, const char* id, bool begin);
    int findFreeBand() const;
    void removeBand (int band);
    void showBandMenu (int band);
    void drawPiano (juce::Graphics&, juce::Rectangle<float>) const;
    void drawTooltip (juce::Graphics&, int band) const;

    EqProcessor& proc;
    ProLook look;

    juce::Rectangle<float> plot;
    juce::Rectangle<int> header, bandGroup, dynGroup, outGroup;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> knobLabels;

    int sel = 0, dragging = -1, hoverBand = -1;
    float dbRange = 18.f;
    juce::Point<float> hover { -1.f, -1.f }, dragMouse, dragNode;

    juce::ComboBox type, slope, mode, range, phase, quality, dir;
    juce::Slider freq, gain, q, dRange, dThr, dAtt, dRel, outGain;
    juce::ToggleButton on { "On" }, dyn { "Dynamic" }, analyzer { "Analyzer" };

    std::unique_ptr<APVTS::ComboBoxAttachment> aType, aSlope, aMode, aDir, aPhase, aQuality;
    std::unique_ptr<APVTS::SliderAttachment> aFreq, aGain, aQ, aRange, aThr, aAtt, aRel, aOut;
    std::unique_ptr<APVTS::ButtonAttachment> aOn, aDyn;

    juce::dsp::FFT fft { 12 };
    juce::dsp::WindowingFunction<float> window { (size_t) kFft, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, kFft> fftIn {};
    std::array<float, 2 * kFft> fftData {};
    std::array<float, kFft / 2> spectrum {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqEditor)
};
