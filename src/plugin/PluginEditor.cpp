#include "PluginEditor.h"

using namespace lzui;

// ---------------------------------------------------------------- LookAndFeel

lzui::LookAndFeel::LookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, offWhite);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, stahlblau);
    setColour (juce::TextButton::textColourOffId, offWhite);
    setColour (juce::TextButton::textColourOnId, offWhite);
    setColour (juce::ComboBox::textColourId, offWhite);
    setColour (juce::ComboBox::arrowColourId, offWhite);
    setColour (juce::PopupMenu::textColourId, offWhite);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, mix (surface3(), offWhite, 0.06f));
    setColour (juce::PopupMenu::highlightedTextColourId, offWhite);
}

void lzui::LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start,
                                          float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const juce::PathStrokeType stroke (3.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt);

    juce::Path track;
    track.addCentredArc (c.x, c.y, r, r, 0.0f, start, end, true);
    g.setColour (line (0.16f));
    g.strokePath (track, stroke);

    juce::Path value;
    value.addCentredArc (c.x, c.y, r, r, 0.0f, start, angle, true);
    g.setColour (s.isEnabled() ? offWhite : line (0.4f));
    g.strokePath (value, stroke);

    // square pointer
    juce::Path p;
    p.addRectangle (-1.5f, -r + 6.0f, 3.0f, r * 0.45f);
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c));
}

void lzui::LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over,
                                              bool down)
{
    auto r = b.getLocalBounds().toFloat();
    g.setColour (down ? mix (navy, offWhite, 0.10f) : (over ? surface2() : navy));
    g.fillRect (r);
    g.setColour (line (over ? 1.0f : 0.32f));
    g.drawRect (r, 1.0f);
    if (b.hasKeyboardFocus (true))
    {
        g.setColour (tally);
        g.drawRect (r.expanded (2.0f), 2.0f);
    }
}

void lzui::LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (0, 0, w, h).toFloat();
    g.setColour (box.isMouseOver (true) ? mix (surface2(), offWhite, 0.06f) : surface2());
    g.fillRect (r);
    g.setColour (line (0.32f));
    g.drawRect (r, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (offWhite);
    g.fillPath (arrow);
}

// ---------------------------------------------------------------- Spectrum

namespace
{
constexpr double fMin = 20.0, fMax = 20000.0, dbTop = 0.0, dbBottom = -100.0;
}

float SpectrumView::xFor (double f) const
{
    return (float) (std::log (f / fMin) / std::log (fMax / fMin) * getWidth());
}

float SpectrumView::yFor (double db) const
{
    return (float) ((dbTop - db) / (dbTop - dbBottom) * getHeight());
}

void SpectrumView::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (deepNavy);
    g.fillRect (area);

    // grid
    g.setFont (juce::FontOptions (11.0f));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = xFor (f);
        g.setColour (line (0.10f));
        g.drawVerticalLine ((int) x, 0.0f, area.getBottom());
        g.setColour (stahlblau);
        g.drawText (f >= 1000.0 ? juce::String (f / 1000.0, 0) + "k" : juce::String ((int) f), (int) x + 3,
                    getHeight() - 16, 40, 14, juce::Justification::left);
    }
    for (double db = -20.0; db > dbBottom; db -= 20.0)
    {
        g.setColour (line (0.10f));
        g.drawHorizontalLine ((int) yFor (db), 0.0f, area.getRight());
        g.setColour (stahlblau);
        g.drawText (juce::String ((int) db), 4, (int) yFor (db) + 2, 40, 14, juce::Justification::left);
    }

    // spectrum (post-filter signal)
    const auto& det = proc.getSuppressor().getDetector();
    const auto& spec = det.getSpectrum();
    const int bins = det.getNumSpectrumBins();
    const double binHz = det.getBinHz();
    juce::Path sp;
    bool started = false;
    float lastX = -10.0f;
    for (int k = 1; k < bins; ++k)
    {
        const double f = k * binHz;
        if (f < fMin || f > fMax)
            continue;
        const float x = xFor (f);
        float db = spec[(size_t) k].load (std::memory_order_relaxed);
        if (x - lastX < 1.0f && started)
            continue; // one point per pixel is enough
        lastX = x;
        const float y = yFor (juce::jlimit ((float) dbBottom, (float) dbTop, db));
        if (! started)
        {
            sp.startNewSubPath (x, y);
            started = true;
        }
        else
            sp.lineTo (x, y);
    }
    g.setColour (eisblau.withAlpha (0.85f));
    g.strokePath (sp, juce::PathStrokeType (1.2f));

    // filter response (top band, 0 dB at 12 % height, 1 px = 0.5 dB)
    const auto& pub = proc.getSuppressor().getBank().published();
    const double fs = proc.getAnalysisSampleRate();
    const double widthOct = proc.params.getRawParameterValue ("width")->load();
    const float zeroY = area.getHeight() * 0.12f;
    juce::Path resp;
    for (int px = 0; px <= getWidth(); px += 2)
    {
        const double f = fMin * std::pow (fMax / fMin, (double) px / getWidth());
        double db = 0.0;
        for (const auto& n : pub)
        {
            const double d = n.depthDb.load (std::memory_order_relaxed);
            if (d > -0.01)
                continue;
            const double nf = n.freq.load (std::memory_order_relaxed);
            const double A = std::pow (10.0, d / 40.0), w0 = 2.0 * lzdf::kPi * nf / fs;
            const double alpha = std::sin (w0) * std::sinh (std::log (2.0) / 2.0 * widthOct * w0 / std::sin (w0));
            const double w = 2.0 * lzdf::kPi * f / fs;
            const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
            const auto num = (1.0 + alpha * A) + (-2.0 * std::cos (w0)) * z1 + (1.0 - alpha * A) * z2;
            const auto den = (1.0 + alpha / A) + (-2.0 * std::cos (w0)) * z1 + (1.0 - alpha / A) * z2;
            db += 20.0 * std::log10 (std::abs (num / den) + 1e-12);
        }
        const float y = zeroY - (float) db * 2.0f;
        if (px == 0)
            resp.startNewSubPath ((float) px, y);
        else
            resp.lineTo ((float) px, y);
    }
    g.setColour (offWhite);
    g.strokePath (resp, juce::PathStrokeType (1.5f));

    // notch markers
    for (const auto& n : pub)
    {
        if (! n.active.load() && n.depthDb.load() > -0.01f)
            continue;
        const float x = xFor (n.freq.load());
        const float d = n.depthDb.load();
        g.setColour (n.fixed.load() ? stahlblau : offWhite);
        g.fillRect (x - 1.0f, zeroY, 2.0f, -d * 2.0f);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::String (juce::roundToInt (n.freq.load())), (int) x - 30, (int) (zeroY - d * 2.0f) + 2, 60, 14,
                    juce::Justification::centred);
    }

    g.setColour (line (0.16f));
    g.drawRect (area, 1.0f);
}

// ---------------------------------------------------------------- Editor

LZDefeedbackEditor::LZDefeedbackEditor (LZDefeedbackProcessor& p)
    : AudioProcessorEditor (&p), proc (p), spectrum (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (spectrum);

    setupKnob (sensitivity, "sensitivity", "Empfindlichkeit");
    setupKnob (depth, "maxDepth", "Max. Tiefe");
    setupKnob (width, "width", "Breite");
    setupKnob (notches, "notches", "Filter");
    setupKnob (hold, "hold", "Halten");
    setupKnob (release, "release", juce::CharPointer_UTF8 ("R\xc3\xbc" "ckkehr"));

    mode.addItemList ({ "Live", "Einmessen" }, 1);
    mode.setTooltip (juce::CharPointer_UTF8 ("Einmessen: neue Filter bleiben fest stehen. Live: Filter kehren nach der Haltezeit zur\xc3\xbc" "ck."));
    addAndMakeVisible (mode);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.params, "mode", mode);

    fixButton.onClick = [this] { proc.requestFixAll(); };
    resetButton.onClick = [this] { proc.requestReset(); };
    addAndMakeVisible (fixButton);
    addAndMakeVisible (resetButton);

    setResizable (true, true);
    setResizeLimits (720, 420, 1600, 1000);
    setSize (880, 500);
    startTimerHz (30);
}

LZDefeedbackEditor::~LZDefeedbackEditor()
{
    setLookAndFeel (nullptr);
}

void LZDefeedbackEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& name)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, offWhite);
    addAndMakeVisible (k.slider);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, paramId, k.slider);
}

void LZDefeedbackEditor::timerCallback()
{
    int active = 0, fixedCount = 0;
    for (const auto& n : proc.getSuppressor().getBank().published())
        if (n.active.load())
        {
            ++active;
            fixedCount += n.fixed.load() ? 1 : 0;
        }
    notchSummary = juce::String (active) + " aktiv  /  " + juce::String (fixedCount) + " fest  /  Latenz 0 ms";
    repaint();
}

void LZDefeedbackEditor::paint (juce::Graphics& g)
{
    g.fillAll (navy);

    auto top = getLocalBounds().reduced (16).removeFromTop (28);
    g.setColour (offWhite);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("LZ Defeedback", top.removeFromLeft (220), juce::Justification::centredLeft);

    g.setColour (stahlblau);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (notchSummary, top.removeFromLeft (320), juce::Justification::centredLeft);
}

void LZDefeedbackEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto header = r.removeFromTop (28);
    resetButton.setBounds (header.removeFromRight (120).reduced (0, 2));
    header.removeFromRight (8);
    fixButton.setBounds (header.removeFromRight (120).reduced (0, 2));
    header.removeFromRight (8);
    mode.setBounds (header.removeFromRight (130).reduced (0, 2));

    r.removeFromTop (12);
    auto controls = r.removeFromBottom (120);
    r.removeFromBottom (12);
    spectrum.setBounds (r);

    Knob* knobs[] = { &sensitivity, &depth, &width, &notches, &hold, &release };
    const int w = controls.getWidth() / 6;
    for (auto* k : knobs)
    {
        auto cell = controls.removeFromLeft (w);
        k->label.setBounds (cell.removeFromTop (16));
        k->slider.setBounds (cell);
    }
}
