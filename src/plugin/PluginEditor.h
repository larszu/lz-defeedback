#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace lzui
{
// Brand palette (LZM Brand Guide 2.0); derived tones are mixes of two palette colours.
const juce::Colour deepNavy { 0xff132040 }, navy { 0xff1d324f }, offWhite { 0xfff6f5f0 }, eisblau { 0xffe1ecef },
    stahlblau { 0xff8c9cb3 }, tally { 0xffd6402e }, warn { 0xffc8892b };

inline juce::Colour mix (juce::Colour a, juce::Colour b, float amountOfB) { return a.interpolatedWith (b, amountOfB); }
inline juce::Colour surface2() { return mix (navy, offWhite, 0.06f); }
inline juce::Colour surface3() { return mix (navy, offWhite, 0.10f); }
inline juce::Colour line (float alpha) { return offWhite.withAlpha (alpha); }

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                           juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::FontOptions (13.0f, juce::Font::bold); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f, juce::Font::bold); }
    void drawPopupMenuBackground (juce::Graphics& g, int, int) override { g.fillAll (surface3()); }
};
} // namespace lzui

class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (LZDefeedbackProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    float xFor (double f) const;
    float yFor (double db) const;
    LZDefeedbackProcessor& proc;
};

class LZDefeedbackEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LZDefeedbackEditor (LZDefeedbackProcessor&);
    ~LZDefeedbackEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name);

    LZDefeedbackProcessor& proc;
    lzui::LookAndFeel lnf;
    SpectrumView spectrum;
    Knob sensitivity, depth, width, notches, hold, release;
    juce::ComboBox mode;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    juce::TextButton fixButton { "Alle fixieren" }, resetButton { juce::CharPointer_UTF8 ("Zur\xc3\xbc" "cksetzen") };
    juce::String notchSummary;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LZDefeedbackEditor)
};
