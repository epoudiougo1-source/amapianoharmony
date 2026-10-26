#include "PluginProcessor.h"

AmapianoHarmonyAudioProcessor::AmapianoHarmonyAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout AmapianoHarmonyAudioProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    const int   defSemi[3] = { 3, 7, -12 };       // tierce, quinte, octave en dessous
    const float defVol[3]  = { 0.6f, 0.5f, 0.4f };

    for (int i = 0; i < 3; ++i)
    {
        const String n (i + 1);
        p.push_back (std::make_unique<AudioParameterInt>   (ParameterID { "semi" + n, 1 }, "Voix " + n + " demi-tons", -24, 24, defSemi[i]));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "vol" + n, 1 },  "Voix " + n + " volume", NormalisableRange<float> (0.0f, 1.0f, 0.01f), defVol[i]));
    }

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "chorus", 1 }, "Chorus", NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.25f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "reverb", 1 }, "Reverb", NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.30f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 },    "Mix choeur", NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.50f));

    return { p.begin(), p.end() };
}

bool AmapianoHarmonyAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void AmapianoHarmonyAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int ch = juce::jmax (1, getTotalNumInputChannels());

    for (auto& s : stretchers)
    {
        s.presetDefault (ch, (float) sampleRate);
        s.reset();
    }

    const int latency = stretchers[0].inputLatency() + stretchers[0].outputLatency();
    setLatencySamples (latency);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, (juce::uint32) ch };

    chorus.prepare (spec);
    chorus.setRate (0.8f);
    chorus.setDepth (0.25f);
    chorus.setCentreDelay (8.0f);
    chorus.setFeedback (0.0f);

    reverb.prepare (spec);

    // la voix d'origine est retardée pour rester alignée avec les harmonies
    dryDelay.setMaximumDelayInSamples (latency + 16);
    dryDelay.prepare (spec);
    dryDelay.setDelay ((float) latency);

    harmony.setSize (ch, samplesPerBlock);
    for (auto& b : voiceBuffers)
        b.setSize (ch, samplesPerBlock);
}

void AmapianoHarmonyAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n  = buffer.getNumSamples();
    const int ch = buffer.getNumChannels();

    harmony.setSize (ch, n, false, false, true);
    harmony.clear();

    // 1) les 3 voix d'harmonie
    for (int v = 0; v < numVoices; ++v)
    {
        const juce::String id (v + 1);
        const float semis = *apvts.getRawParameterValue ("semi" + id);
        const float vol   = *apvts.getRawParameterValue ("vol" + id);

        voiceBuffers[v].setSize (ch, n, false, false, true);

        stretchers[v].setTransposeSemitones (semis);
        stretchers[v].process (buffer.getArrayOfReadPointers(), n,
                               voiceBuffers[v].getArrayOfWritePointers(), n);

        for (int c = 0; c < ch; ++c)
            harmony.addFrom (c, 0, voiceBuffers[v], c, 0, n, vol);
    }

    // 2) chorus léger puis reverb douce, uniquement sur le choeur
    const float chorusAmt = *apvts.getRawParameterValue ("chorus");
    const float reverbAmt = *apvts.getRawParameterValue ("reverb");
    const float mix       = *apvts.getRawParameterValue ("mix");

    chorus.setMix (chorusAmt * 0.5f);

    juce::dsp::Reverb::Parameters rp;
    rp.roomSize   = 0.45f;
    rp.damping    = 0.7f;
    rp.wetLevel   = reverbAmt * 0.35f;
    rp.dryLevel   = 1.0f;
    rp.width      = 1.0f;
    rp.freezeMode = 0.0f;
    reverb.setParameters (rp);

    juce::dsp::AudioBlock<float> block (harmony);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    chorus.process (ctx);
    reverb.process (ctx);

    // 3) voix d'origine (retardée) + choeur
    for (int c = 0; c < ch; ++c)
    {
        auto* data = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            dryDelay.pushSample (c, data[i]);
            data[i] = dryDelay.popSample (c);
        }
        buffer.addFrom (c, 0, harmony, c, 0, n, mix);
    }
}

juce::AudioProcessorEditor* AmapianoHarmonyAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void AmapianoHarmonyAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AmapianoHarmonyAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AmapianoHarmonyAudioProcessor();
}
