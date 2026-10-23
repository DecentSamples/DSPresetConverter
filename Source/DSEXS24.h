/*
  ==============================================================================

    DSEXS24.h
    Created: 16 Aug 2021 9:55:07pm
    Author:  David Hilowitz

    Reads Logic EXS instruments. The format, and the evidence for every field, is in the EXSFormat
    repo's FORMAT.md; its reference reader (exsformat/exs.py) is what this parser is checked against.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <map>
#include <optional>

struct DSEXS24Zone {
    int id = 0;
    juce::String name;
    bool pitch = true;              // pitch tracking
    bool oneShot = false;
    bool reverse = false;
    short key = 60;
    short fineTuning = 0;           // cents
    short pan = 0;
    float volume = 0;               // dB
    short coarseTuning = 0;         // semitones
    short keyLow = 0;
    short keyHigh = 127;
    bool velocityRangeOn = false;
    short loVel = 0;
    short hiVel = 127;
    int sampleStart = 0;
    int sampleEnd = 0;              // exclusive; 0 = to the end of the sample
    int loopStart = 0;
    int loopEnd = 0;                // exclusive
    int loopCrossfadeMilliseconds = 0;
    bool loopEnabled = false;
    bool loopEqualPower = false;
    short loopTune = 0;             // cents
    int flags = 0;                  // the raw option bits
    short output = -1;
    int groupIndex = -1;            // -1 = ungrouped
    int sampleIndex = -1;           // may be out of range in real files
    int sampleFade = 0;
    int offset = 0;
};

struct DSEXS24Group {
    juce::String name;
    short volume = 0;               // dB
    short pan = 0;
    short polyphony = 0;            // 0 = the instrument's
    short velRangeLow = 0;
    short velRangeHigh = 127;
    short trigger = 0;              // 1 = release trigger
    short output = 0;
    int exsSequence = -1;           // round robin: the group this one plays after ("play after group"); -1 = none.
                                    // Only meaningful when sequenceMode is 2; other modes leave stale values here
    int sequenceMode = 0;           // 2 = round robin member; the other modes aren't mapped
    int seqNumber = 0;              // position in its round robin chain, from 1; 0 = not in one
    int seqLength = 0;              // the length of that chain
    int seqChain = -1;              // which chain (an index; -1 = none)
};

struct DSEXS24Sample {
    int id = 0;
    juce::String name;
    int length = 0;
    int sampleRate = 0;
    short bitDepth = 0;
    int type = 0;
    juce::String filePath;
    juce::String fileName;
};

/** The instrument parameters that are mapped (FORMAT.md, "Instrument parameters"). */
struct DSEXS24Settings {
    std::optional<int> volumeDb, transpose, fineTune, voices, bendUp, bendDown, glideRaw, filterType;
    std::optional<double> ampAttackMs, ampDecayMs, ampSustain, ampReleaseMs, filterCutoff, filterResonance;
    std::optional<int> ampEnvelopeType;                 // 0 AD, 1 AR, 2 ADSR, 3 AHDSR, 4 DADSR, 5 DAHDSR
    std::optional<int> ampAttackCurve;                  // -99..99; which way the sign bends is not yet known
    std::optional<double> ampDelayMs, ampHoldMs;        // only when the type has that stage
    std::optional<bool> filterOn;
};

class DSEXS24 {
public:
    DSEXS24() {}
    bool loadExs(juce::File file);
    /** Parses an instrument from memory. On failure returns false and sets getError(). */
    bool loadExsData(const void* data, size_t size);

    juce::Array<DSEXS24Zone> & getZones() { return zones; }
    juce::Array<DSEXS24Group> & getGroups() { return groups; }
    juce::Array<DSEXS24Sample> & getSamples() { return samples; }
    const std::map<int, int>& getParameters() const { return parameters; }
    const DSEXS24Settings& getSettings() const { return settings; }
    const juce::String& getName() const { return name; }
    bool isBigEndian() const { return bigEndian; }
    const juce::String& getError() const { return error; }        // "not_exs" or "corrupt"

    /** Envelope times are stored as 0..127 on a fourth-power curve to 10 s. */
    static double envelopeMs (int value);
    /** Sustain is stored as 0..127 on a square-law curve. */
    static double sustainLevel (int value);

private:
    juce::Array<DSEXS24Zone> zones;
    juce::Array<DSEXS24Group> groups;
    juce::Array<DSEXS24Sample> samples;
    juce::Array<juce::Array<int>> sequences;
    std::map<int, int> parameters;
    DSEXS24Settings settings;
    juce::String name;
    juce::String error;
    bool bigEndian = false;

    void readSequences();
    void convertSeqNumbers();
};
