/*
  ==============================================================================

    DSSFZ.h
    Created: 13 Oct 2020 1:28:26pm
    Author:  David Hilowitz

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>

class DSSFZ {
public:
    void parseFromFile(juce::File file);
    
    juce::ValueTree getValueTree() { return valueTree; }
    juce::String getXML() { return valueTree.toXmlString();}
private:
    juce::ValueTree valueTree;
    juce::ValueTree currentGroup;
    juce::ValueTree currentRegion;
    juce::String fileString;

    
    enum sectionType { UNKNOWN, GROUP, REGION, GLOBAL, CONTROL };
    sectionType currentSection = UNKNOWN;
    
    void pushHeader(juce::String);
    void pushOpcode(juce::String);
    static juce::String stripComments(const juce::String &text);
};
