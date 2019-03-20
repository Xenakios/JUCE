/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2017 - ROLI Ltd.

   JUCE is an open source library subject to commercial or open-source
   licensing.

   By using JUCE, you agree to the terms of both the JUCE 5 End-User License
   Agreement and JUCE 5 Privacy Policy (both updated and effective as of the
   27th April 2017).

   End User License Agreement: www.juce.com/juce-5-licence
   Privacy Policy: www.juce.com/juce-5-privacy-policy

   Or: You may also use this code under the terms of the GPL v3 (see
   www.gnu.org/licenses).

   JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

#include "../JuceLibraryCode/JuceHeader.h"
#include "InternalFilters.h"
#include "FilterGraph.h"
#include <random>

//==============================================================================
class InternalPlugin   : public AudioPluginInstance
{
protected:
    InternalPlugin (const PluginDescription& descr,
                    const AudioChannelSet& channelSetToUse = AudioChannelSet::stereo())
        : AudioPluginInstance (getBusProperties (descr.numInputChannels == 0, channelSetToUse)),
          name  (descr.fileOrIdentifier.upToFirstOccurrenceOf (":", false, false)),
          state (descr.fileOrIdentifier.fromFirstOccurrenceOf (":", false, false)),
          isGenerator (descr.numInputChannels == 0),
          hasMidi (descr.isInstrument),
          channelSet (channelSetToUse)
    {
        jassert (channelSetToUse.size() == descr.numOutputChannels);
    }

public:
    //==============================================================================
    const String getName() const override                     { return name; }
    double getTailLengthSeconds() const override              { return 0.0; }
    bool acceptsMidi() const override                         { return hasMidi; }
    bool producesMidi() const override                        { return hasMidi; }
    AudioProcessorEditor* createEditor() override             { return nullptr; }
    bool hasEditor() const override                           { return false; }
    int getNumPrograms() override                             { return 0; }
    int getCurrentProgram() override                          { return 0; }
    void setCurrentProgram (int) override                     {}
    const String getProgramName (int) override                { return {}; }
    void changeProgramName (int, const String&) override      {}
    void getStateInformation (juce::MemoryBlock&) override    {}
    void setStateInformation (const void*, int) override      {}

    //==============================================================================
    bool isBusesLayoutSupported (const BusesLayout& layout) const override
    {
        if (! isGenerator)
        {
            if (layout.getMainOutputChannelSet() != channelSet)
                return false;
        }

        if (layout.getMainInputChannelSet() != channelSet)
            return false;

        return true;
    }

    //==============================================================================
    void fillInPluginDescription (PluginDescription& description) const override
    {
        description = getPluginDescription (name + ":" + state,
                                            isGenerator,
                                            hasMidi,
                                            channelSet);
    }

    static PluginDescription getPluginDescription (const String& identifier,
                                                   bool registerAsGenerator,
                                                   bool acceptsMidi,
                                                   const AudioChannelSet& channelSetToUse
                                                      = AudioChannelSet::stereo())
    {
        PluginDescription descr;
        auto pluginName  = identifier.upToFirstOccurrenceOf (":", false, false);
        auto pluginState = identifier.fromFirstOccurrenceOf (":", false, false);

        descr.name              = pluginName;
        descr.descriptiveName   = pluginName;
        descr.pluginFormatName  = "Internal";
        descr.category          = (registerAsGenerator ? (acceptsMidi ? "Synth" : "Generator") : "Effect");
        descr.manufacturerName  = "JUCE";
        descr.version           = ProjectInfo::versionString;
        descr.fileOrIdentifier  = pluginName + ":" + pluginState;
        descr.uid               = pluginName.hashCode();
        descr.isInstrument      = (acceptsMidi && registerAsGenerator);
        descr.numInputChannels  = (registerAsGenerator ? 0 : channelSetToUse.size());
        descr.numOutputChannels = channelSetToUse.size();

        return descr;
    }
private:
    static BusesProperties getBusProperties (bool registerAsGenerator,
                                             const AudioChannelSet& channelSetToUse)
    {
        return registerAsGenerator ? BusesProperties().withOutput ("Output", channelSetToUse)
                                   : BusesProperties().withInput  ("Input",  channelSetToUse)
                                                      .withOutput ("Output", channelSetToUse);
    }

    //==============================================================================
    String name, state;
    bool isGenerator, hasMidi;
    AudioChannelSet channelSet;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InternalPlugin)
};

//==============================================================================
class SineWaveSynth :   public InternalPlugin
{
public:
    SineWaveSynth (const PluginDescription& descr) :   InternalPlugin (descr)
    {
        const int numVoices = 8;

        // Add some voices...
        for (int i = numVoices; --i >= 0;)
            synth.addVoice (new SineWaveVoice());

        // ..and give the synth a sound to play
        synth.addSound (new SineWaveSound());
    }

    static String getIdentifier()
    {
        return "Sine Wave Synth";
    }

    static PluginDescription getPluginDescription()
    {
        return InternalPlugin::getPluginDescription (getIdentifier(), true, true);
    }

    //==============================================================================
    void prepareToPlay (double newSampleRate, int) override
    {
        synth.setCurrentPlaybackSampleRate (newSampleRate);
    }

    void releaseResources() override {}

    //==============================================================================
    void processBlock (AudioBuffer<float>& buffer, MidiBuffer& midiMessages) override
    {
        const int numSamples = buffer.getNumSamples();

        buffer.clear();
        synth.renderNextBlock (buffer, midiMessages, 0, numSamples);
        buffer.applyGain (0.8f);
    }

private:
    //==============================================================================
    class SineWaveSound : public SynthesiserSound
    {
    public:
        SineWaveSound() {}

        bool appliesToNote (int /*midiNoteNumber*/) override    { return true; }
        bool appliesToChannel (int /*midiChannel*/) override    { return true; }
    };

    class SineWaveVoice   : public SynthesiserVoice
    {
    public:
        SineWaveVoice()
        : currentAngle (0), angleDelta (0), level (0), tailOff (0)
        {
        }

        bool canPlaySound (SynthesiserSound* sound) override
        {
            return dynamic_cast<SineWaveSound*> (sound) != nullptr;
        }

        void startNote (int midiNoteNumber, float velocity,
                        SynthesiserSound* /*sound*/,
                        int /*currentPitchWheelPosition*/) override
        {
            currentAngle = 0.0;
            level = velocity * 0.15;
            tailOff = 0.0;

            double cyclesPerSecond = MidiMessage::getMidiNoteInHertz (midiNoteNumber);
            double cyclesPerSample = cyclesPerSecond / getSampleRate();

            angleDelta = cyclesPerSample * 2.0 * double_Pi;
        }

        void stopNote (float /*velocity*/, bool allowTailOff) override
        {
            if (allowTailOff)
            {
                // start a tail-off by setting this flag. The render callback will pick up on
                // this and do a fade out, calling clearCurrentNote() when it's finished.

                if (tailOff == 0.0) // we only need to begin a tail-off if it's not already doing so - the
                    // stopNote method could be called more than once.
                    tailOff = 1.0;
            }
            else
            {
                // we're being told to stop playing immediately, so reset everything..

                clearCurrentNote();
                angleDelta = 0.0;
            }
        }

        void pitchWheelMoved (int /*newValue*/) override
        {
            // not implemented for the purposes of this demo!
        }

        void controllerMoved (int /*controllerNumber*/, int /*newValue*/) override
        {
            // not implemented for the purposes of this demo!
        }

        void renderNextBlock (AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override
        {
            if (angleDelta != 0.0)
            {
                if (tailOff > 0)
                {
                    while (--numSamples >= 0)
                    {
                        const float currentSample = (float) (sin (currentAngle) * level * tailOff);

                        for (int i = outputBuffer.getNumChannels(); --i >= 0;)
                            outputBuffer.addSample (i, startSample, currentSample);

                        currentAngle += angleDelta;
                        ++startSample;

                        tailOff *= 0.99;

                        if (tailOff <= 0.005)
                        {
                            // tells the synth that this voice has stopped
                            clearCurrentNote();

                            angleDelta = 0.0;
                            break;
                        }
                    }
                }
                else
                {
                    while (--numSamples >= 0)
                    {
                        const float currentSample = (float) (sin (currentAngle) * level);

                        for (int i = outputBuffer.getNumChannels(); --i >= 0;)
                            outputBuffer.addSample (i, startSample, currentSample);

                        currentAngle += angleDelta;
                        ++startSample;
                    }
                }
            }
        }

    private:
        double currentAngle, angleDelta, level, tailOff;
    };

    //==============================================================================
    Synthesiser synth;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SineWaveSynth)
};

//==============================================================================
class ReverbFilter    : public InternalPlugin
{
public:
    ReverbFilter (const PluginDescription& descr) :   InternalPlugin (descr)
    {}

    static String getIdentifier()
    {
        return "Reverb";
    }

    static PluginDescription getPluginDescription()
    {
        return InternalPlugin::getPluginDescription (getIdentifier(), false, false);
    }

    void prepareToPlay (double newSampleRate, int) override
    {
        reverb.setSampleRate (newSampleRate);
    }

    void reset() override
    {
        reverb.reset();
    }

    void releaseResources() override {}

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        auto numChannels = buffer.getNumChannels();

        if (numChannels == 1)
            reverb.processMono (buffer.getWritePointer (0), buffer.getNumSamples());
        else
            reverb.processStereo (buffer.getWritePointer (0),
                                  buffer.getWritePointer (1),
                                  buffer.getNumSamples());

        for (int ch = 2; ch < numChannels; ++ch)
            buffer.clear (ch, 0, buffer.getNumSamples());
    }

private:
    Reverb reverb;
};

class MyThumbCache : public AudioThumbnailCache
{
public:
	MyThumbCache() : AudioThumbnailCache(100) {}
};

class FilePlayerPlugin;

class FilePlayPluginEditor : public AudioProcessorEditor, public ChangeListener, public MultiTimer, public Value::Listener
{
public:
	FilePlayPluginEditor(FilePlayerPlugin& plug);
	void changeListenerCallback(ChangeBroadcaster* bc) override
	{
		repaint();
	}
	void timerCallback(int id) override;
	void valueChanged(Value& v) override;
	void paint(Graphics& g) override;
	
	void mouseDown(const MouseEvent& ev) override;
	
private:
	FilePlayerPlugin& m_fp;
	TextButton m_importbutton;
	SharedResourcePointer<MyThumbCache> m_thumbcache;
	std::unique_ptr<AudioThumbnail> m_thumb;
	
};

class FilePlayerPlugin : public InternalPlugin
{
public:
	FilePlayerPlugin(const PluginDescription& descr) : InternalPlugin(descr)
	{
		m_randgen = std::mt19937((int)this);
		m_formatmanager.registerBasicFormats();
	}
	~FilePlayerPlugin()
	{
		
	}
	static String getIdentifier()
	{
		return "File Player";
	}

	static PluginDescription getPluginDescription()
	{
		return InternalPlugin::getPluginDescription(getIdentifier(), true, false);
	}
	void randomizePlayposition()
	{
		std::uniform_int<int64> dist(m_looppoints.getStart(), m_looppoints.getEnd());
		m_filepos = dist(m_randgen);
	}
	void setAudioFileToPlay(File infile)
	{
		ScopedLock locker(m_cs);
		auto temp = std::unique_ptr<AudioFormatReader>(m_formatmanager.createReaderFor(infile));
		if (temp)
		{
			m_reader = std::move(temp);
			m_looppoints = { 0 ,m_reader->lengthInSamples };
			m_filepos = m_looppoints.getStart();
			currentFile = infile.getFullPathName();
			updateHostDisplay();
		}
		else Logger::writeToLog("Could not open file " + infile.getFullPathName());
		
	}
	void getStateInformation(MemoryBlock& block) override
	{
		ValueTree vt("fileplayerstate");
		vt.setProperty("filename", currentFile.getValue(), nullptr);
		MemoryOutputStream ms(block,true);
		vt.writeToStream(ms);
	}
	void setStateInformation(const void* data, int size) override
	{
		ValueTree state = ValueTree::readFromData(data, size);
		if (state.isValid())
		{
			String fn = state.getProperty("filename");
			setAudioFileToPlay(File(fn));
		}
	}
	void prepareToPlay(double newSampleRate, int) override
	{
		
	}
	void releaseResources() override
	{

	}
	void processBlock(AudioBuffer<float>& buffer, MidiBuffer& midimessages)
	{
		ScopedLock locker(m_cs);
		buffer.clear();
		if (m_reader)
		{
			m_reader->read(&buffer, 0, buffer.getNumSamples(), m_filepos, true, true);
			buffer.applyGain(0.25f);
			m_filepos += buffer.getNumSamples();
			if (m_filepos >= m_looppoints.getEnd())
				m_filepos = m_looppoints.getStart();
		}
	}
	bool hasEditor() const override 
	{ 
		return true; 
	}
	AudioProcessorEditor* createEditor() override
	{
		return new FilePlayPluginEditor(*this);
	}
	AudioFormatManager m_formatmanager;
	
	double getPlayPositionPercent()
	{
		if (m_reader == nullptr)
			return 0.0;
		return 1.0 / m_reader->lengthInSamples*m_filepos;
	}
	Value currentFile;
private:
	std::unique_ptr<AudioFormatReader> m_reader;
	int64 m_filepos = 0;
	Range<int64> m_looppoints;
	std::mt19937 m_randgen;
	CriticalSection m_cs;
	
};

FilePlayPluginEditor::FilePlayPluginEditor(FilePlayerPlugin& fp) : AudioProcessorEditor(fp),
	m_fp(fp)
{
	setSize(500, 200);
	m_thumb = std::make_unique<AudioThumbnail>(128, m_fp.m_formatmanager, *m_thumbcache);
	m_thumb->addChangeListener(this);
	if (m_fp.currentFile.getValue().isVoid()==false)
		m_thumb->setSource(new FileInputSource(m_fp.currentFile.toString()));
	addAndMakeVisible(m_importbutton);
	m_importbutton.setButtonText("Import...");
	m_importbutton.setBounds(1, 1, 198, 24);
	m_importbutton.onClick = [this]()
	{
		FileChooser chooser("Choose audio file", File(), "*.wav;*.flac", true);
		if (chooser.browseForFileToOpen())
		{
			m_fp.setAudioFileToPlay(chooser.getResult());
			
		}
	};
	m_fp.currentFile.addListener(this);
	startTimer(1, 100);
}

void FilePlayPluginEditor::paint(Graphics& g)
{
	g.fillAll(Colours::black);
	if (m_thumb && m_thumb->getTotalLength() > 0.0)
	{
		g.setColour(Colours::lightgrey);
		m_thumb->drawChannels(g, { 0,30,getWidth(),getHeight() - 30 }, 0.0, m_thumb->getTotalLength(), 1.0f);
		g.setColour(Colours::white);
		double xcor = jmap<double>(m_fp.getPlayPositionPercent(), 0.0, 1.0, 0.0, getWidth());
		g.drawLine(xcor, 30.0, xcor, getHeight());
	}
}

void FilePlayPluginEditor::valueChanged(Value& v)
{
	if (v.refersToSameSourceAs(m_fp.currentFile))
	{
		m_thumb->setSource(new FileInputSource(v.toString()));
	}
	
}

void FilePlayPluginEditor::timerCallback(int id)
{
	if (id == 0)
	{
	}
	if (id == 1)
	{
		repaint();
	}
}

void FilePlayPluginEditor::mouseDown(const MouseEvent& ev)
{
	//m_fp.randomizePlayposition();
}

//==============================================================================
InternalPluginFormat::InternalPluginFormat()
{
    {
        AudioProcessorGraph::AudioGraphIOProcessor p (AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode);
        p.fillInPluginDescription (audioOutDesc);
    }

    {
        AudioProcessorGraph::AudioGraphIOProcessor p (AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode);
        p.fillInPluginDescription (audioInDesc);
    }

    {
        AudioProcessorGraph::AudioGraphIOProcessor p (AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode);
        p.fillInPluginDescription (midiInDesc);
    }
}

AudioPluginInstance* InternalPluginFormat::createInstance (const String& name)
{
    if (name == audioOutDesc.name) return new AudioProcessorGraph::AudioGraphIOProcessor (AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode);
    if (name == audioInDesc.name)  return new AudioProcessorGraph::AudioGraphIOProcessor (AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode);
    if (name == midiInDesc.name)   return new AudioProcessorGraph::AudioGraphIOProcessor (AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode);


    if (name == SineWaveSynth::getIdentifier()) return new SineWaveSynth (SineWaveSynth::getPluginDescription());
    if (name == ReverbFilter::getIdentifier())  return new ReverbFilter  (ReverbFilter::getPluginDescription());
	if (name == FilePlayerPlugin::getIdentifier()) return new FilePlayerPlugin(FilePlayerPlugin::getPluginDescription());
    return nullptr;
}

void InternalPluginFormat::createPluginInstance (const PluginDescription& desc,
                                                 double /*initialSampleRate*/,
                                                 int /*initialBufferSize*/,
                                                 void* userData,
                                                 PluginCreationCallback callback)
{
    auto* p = createInstance (desc.name);

    callback (userData, p, p == nullptr ? NEEDS_TRANS ("Invalid internal filter name") : String());
}

bool InternalPluginFormat::requiresUnblockedMessageThreadDuringCreation (const PluginDescription&) const noexcept
{
    return false;
}

void InternalPluginFormat::getAllTypes (OwnedArray<PluginDescription>& results)
{
    results.add (new PluginDescription (audioInDesc));
    results.add (new PluginDescription (audioOutDesc));
    results.add (new PluginDescription (midiInDesc));
    results.add (new PluginDescription (SineWaveSynth::getPluginDescription()));
    results.add (new PluginDescription (ReverbFilter::getPluginDescription()));
	results.add (new PluginDescription(FilePlayerPlugin::getPluginDescription()));
}
