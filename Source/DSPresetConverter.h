/*
  ==============================================================================

    DSPresetConverter.h
    Created: 16 Aug 2021 9:55:21pm
    Author:  David Hilowitz

  ==============================================================================
*/

#pragma once

#include "DSEXS24.h"

struct DSSF2Region {
  juce::String name;
  juce::String samplePath;
  int rootNote = 60;
  int loNote = 0;
  int hiNote = 127;
  int loVel = 0;
  int hiVel = 127;
  float tuning = 0.0f;
  float pan = 0.0f;
  float volumeDb = 0.0f;
  bool pitchKeyTrack = true;
  int start = 0;
  int end = -1;
  bool loopEnabled = false;
  int loopStart = 0;
  int loopEnd = 0;
  int seqPosition = 0;
};

struct DSSF2Preset {
  juce::String name;
  juce::Array<DSSF2Region> regions;
};

class DSPresetConverter {
public:
    DSPresetConverter();
    void parseDSEXS24(DSEXS24 exs24);
    void parseSFZValueTree(juce::ValueTree valueTree);
  void parseDSSF2Preset(const DSSF2Preset& preset);
  void setPathChangeLoggingEnabled(bool shouldLog) { pathChangeLoggingEnabled = shouldLog; }
    
    bool huntForSamples(juce::File inputDirectory, juce::String sampleSetName);
    bool convertPathsToDesiredDirectory(juce::File inputDirectory, juce::String desiredDirectoryName);
    bool convertPathsToRelative(juce::File inputDirectory);
    bool convertEXSLoopCrossfadePoints();
    bool copySamplesOverToNewDirectory(juce::File rootOutputDirectory, juce::String sampleSetName, bool skipAudioProcessing, int overrideBitrate, bool unifyDirectories = false, bool truncateLongFilenames = false, bool trimOnly = false);
    bool stripMissingImageReferences(juce::File outputDirectory);
    
    
    juce::String getXML() {
        juce::XmlElement::TextFormat format;
        format.lineWrapLength = 20000;
//        format.newLineChars = "";
        return valueTree.toXmlString(format);
    }
    juce::String getSFZ();
    
    juce::ValueTree getValueTree() { return valueTree; }
    std::unique_ptr<juce::XmlElement> getXMLObject() { return valueTree.createXml(); }
    
    enum HeaderLevel {
        headerLevelGlobal,
        headerLevelGroup,
        headerLevelRegion
    };
private:
    juce::AudioFormatManager audioFormatManager;
    juce::ValueTree valueTree;
  bool pathChangeLoggingEnabled = true;
    void translateSFZRegionProperties(juce::ValueTree sfzRegion, juce::ValueTree &dsSample, HeaderLevel level);
    void addGenericUI();
  void logPathChange(const juce::String& updatedPath) const;

    double linearOrDbStringToDb(const juce::String inputString);
};
