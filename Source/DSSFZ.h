/*
  ==============================================================================

    DSSFZ.h
    Created: 13 Oct 2020 1:28:26pm
    Author:  David Hilowitz

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <map>

class DSSFZ {
public:
    void parseFromFile(juce::File file);
    
    juce::ValueTree getValueTree() { return valueTree; }
    juce::String getXML() { return valueTree.toXmlString();}
private:
    juce::ValueTree valueTree;
    juce::ValueTree currentMaster;
    juce::ValueTree currentGroup;
    juce::ValueTree currentRegion;
    juce::ValueTree currentCurve;
    std::map<int, juce::StringArray> curveUsers; // curve_index -> the *_curveccN opcodes naming it
    juce::String fileString;

    
    enum sectionType { UNKNOWN, GROUP, REGION, GLOBAL, CONTROL, MASTER, CURVE };
    sectionType currentSection = UNKNOWN;
    
    void pushHeader(juce::String);
    void pushOpcode(juce::String);
    void startGroup();
    void reportCurves();
    static juce::String stripComments(const juce::String &text);
};
