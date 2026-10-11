#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ids
{
const juce::String sensitivity = "sensitivity", depth = "maxDepth", width = "width", notches = "notches",
                   mode = "mode", hold = "hold", release = "release";
}

LZDefeedbackProcessor::LZDefeedbackProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "LZDefeedback", createLayout())
{
    pSensitivity = params.getRawParameterValue (ids::sensitivity);
    pDepth = params.getRawParameterValue (ids::depth);
    pWidth = params.getRawParameterValue (ids::width);
    pNotches = params.getRawParameterValue (ids::notches);
    pMode = params.getRawParameterValue (ids::mode);
    pHold = params.getRawParameterValue (ids::hold);
    pRelease = params.getRawParameterValue (ids::release);
}

juce::AudioProcessorValueTreeState::ParameterLayout LZDefeedbackProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto withUnit = [] (const char* unit, int decimals)
    {
        return AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (
            [unit, decimals] (float v, int) { return String (v, decimals) + " " + unit; });
    };
    auto pct = withUnit ("%", 0);
    auto dB = withUnit ("dB", 1);
    auto sec = withUnit ("s", 1);

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::sensitivity, 1 }, "Empfindlichkeit",
                                                  NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::depth, 1 }, "Max. Tiefe",
                                                  NormalisableRange<float> (-40.0f, -6.0f, 0.5f), -24.0f, dB));
    l.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::width, 1 }, "Breite", NormalisableRange<float> (0.025f, 0.33f, 0.0f), 0.1f,
        AudioParameterFloatAttributes()
            .withLabel ("okt")
            .withStringFromValueFunction ([] (float v, int) { return "1/" + String (juce::roundToInt (1.0f / v)) + " okt"; })));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { ids::notches, 1 }, "Filter", 1,
                                                lzdf::NotchBank::maxNotches, 12));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::mode, 1 }, "Modus",
                                                   StringArray { "Live", "Einmessen" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::hold, 1 }, "Halten",
                                                  NormalisableRange<float> (1.0f, 60.0f, 0.5f, 0.5f), 8.0f, sec));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::release, 1 }, String (CharPointer_UTF8 ("R\xc3\xbc" "ckkehr")),
                                                  NormalisableRange<float> (2.0f, 120.0f, 1.0f, 0.5f), 20.0f, sec));
    return l;
}

bool LZDefeedbackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void LZDefeedbackProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate.store (sampleRate);
    suppressor.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    updateSettings();
    setLatencySamples (0);
}

void LZDefeedbackProcessor::updateSettings() noexcept
{
    lzdf::FeedbackSuppressor::Settings s;
    s.sensitivity = pSensitivity->load() / 100.0;
    s.maxDepthDb = pDepth->load();
    s.widthOct = pWidth->load();
    s.numNotches = (int) pNotches->load();
    s.mode = pMode->load() > 0.5f ? lzdf::FeedbackSuppressor::Mode::setup : lzdf::FeedbackSuppressor::Mode::live;
    s.holdSeconds = pHold->load();
    s.releaseSeconds = pRelease->load();

    const auto& cur = suppressor.getSettings();
    auto differs = [] (double a, double b) { return std::abs (a - b) > 1e-9; };
    if (differs (s.sensitivity, cur.sensitivity) || differs (s.maxDepthDb, cur.maxDepthDb)
        || differs (s.widthOct, cur.widthOct) || s.numNotches != cur.numNotches || s.mode != cur.mode
        || differs (s.holdSeconds, cur.holdSeconds) || differs (s.releaseSeconds, cur.releaseSeconds))
        suppressor.setSettings (s);
}

void LZDefeedbackProcessor::applyPendingRestore() noexcept
{
    const int n = pendingCount.exchange (-1);
    if (n < 0)
        return;
    auto& slots = suppressor.getBank().getSlots();
    for (int i = 0; i < n && i < lzdf::NotchBank::maxNotches; ++i)
    {
        auto& s = slots[(size_t) i];
        s.active = true;
        s.fixed = true;
        s.freq = pending[(size_t) i].freq;
        s.targetDb = pending[(size_t) i].depthDb;
        s.secondsSinceHit = 0.0;
        suppressor.getBank().updateCoefficients (s);
    }
}

void LZDefeedbackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (resetRequested.exchange (false))
        suppressor.reset();
    applyPendingRestore();
    if (fixRequested.exchange (false))
        suppressor.fixAllCurrent();

    updateSettings();
    suppressor.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

void LZDefeedbackProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = params.copyState();
    juce::ValueTree fixedNotches ("FixedNotches");
    for (const auto& p : suppressor.getBank().published())
        if (p.active.load() && p.fixed.load())
        {
            juce::ValueTree n ("Notch");
            n.setProperty ("freq", p.freq.load(), nullptr);
            n.setProperty ("depth", p.depthDb.load(), nullptr);
            fixedNotches.appendChild (n, nullptr);
        }
    state.removeChild (state.getChildWithName ("FixedNotches"), nullptr);
    state.appendChild (fixedNotches, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void LZDefeedbackProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (params.state.getType()))
        return;

    int n = 0;
    for (auto child : state.getChildWithName ("FixedNotches"))
    {
        if (n >= lzdf::NotchBank::maxNotches)
            break;
        const float f = child.getProperty ("freq", 0.0f);
        const float d = child.getProperty ("depth", 0.0f);
        if (f > 20.0f && d < -0.5f)
            pending[(size_t) n++] = { f, juce::jlimit (-40.0f, 0.0f, d) };
    }
    state.removeChild (state.getChildWithName ("FixedNotches"), nullptr);
    params.replaceState (state);
    pendingCount.store (n);
}

juce::AudioProcessorEditor* LZDefeedbackProcessor::createEditor()
{
    return new LZDefeedbackEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LZDefeedbackProcessor();
}
