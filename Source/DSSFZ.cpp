/*
  ==============================================================================

    DSSFZ.cpp
    Created: 13 Oct 2020 1:28:26pm
    Author:  David Hilowitz

  ==============================================================================
*/

#include "DSSFZ.h"

// Comments are stripped before anything is tokenised: "//" to the end of the line, and
// "/* ... */" blocks, which may span lines (VCSL comments out whole regions that way).
// Neither has any meaning inside an SFZ value, so this cannot damage one (0000479).
juce::String DSSFZ::stripComments(const juce::String &text) {
    juce::String out;
    out.preallocateBytes(text.getNumBytesAsUTF8());
    const int length = text.length();
    for (int i = 0; i < length; ) {
        const juce::juce_wchar c = text[i];
        const juce::juce_wchar next = (i + 1 < length) ? text[i + 1] : 0;
        if (c == '/' && next == '/') {
            while (i < length && text[i] != '\n' && text[i] != '\r') i++;
        } else if (c == '/' && next == '*') {
            i += 2;
            while (i < length && !(text[i] == '*' && i + 1 < length && text[i + 1] == '/')) {
                if (text[i] == '\n') out << '\n'; // keep line structure for #include and friends
                i++;
            }
            i += 2;
        } else {
            out << c;
            i++;
        }
    }
    return out;
}

void DSSFZ::parseFromFile(juce::File file) {
    valueTree = juce::ValueTree("sfz");
    currentSection = UNKNOWN;
    currentMaster = juce::ValueTree();
    currentGroup = juce::ValueTree();
    currentRegion = juce::ValueTree();
    currentCurve = juce::ValueTree();
    curveUsers.clear();

    std::unique_ptr<juce::FileInputStream> inputStream = file.createInputStream();
    if (inputStream == nullptr) {
        return;
    }
    fileString = stripComments(inputStream->readEntireStreamAsString());

    // Tokens are headers ("<region>") and words. An opcode is a word containing "=", and
    // its value runs to the next opcode or header, so "sample=My Sample.wav lokey=36" gives
    // sample its two words back. This is how every other SFZ player reads a value with
    // spaces in it; the previous scanner guessed the value ended at its last space, which
    // worked for that case and fell apart around comments and several opcodes on a line.
    juce::String opcodeKey, opcodeValue;
    auto flushOpcode = [&]() {
        if (opcodeKey.isNotEmpty()) {
            pushOpcode(opcodeKey + "=" + opcodeValue.trim());
        }
        opcodeKey.clear();
        opcodeValue.clear();
    };

    juce::StringArray lines;
    lines.addLines(fileString);
    for (const juce::String &rawLine : lines) {
        const juce::String line = rawLine.trim();
        if (line.startsWithChar('#')) {
            // #include / #define are SFZ v2 directives this importer does not follow.
            std::cerr << "SFZ2DS: skipping directive '" << line << "'" << std::endl;
            continue;
        }
        const int length = line.length();
        for (int i = 0; i < length; ) {
            const juce::juce_wchar c = line[i];
            if (juce::CharacterFunctions::isWhitespace(c)) { i++; continue; }
            if (c == '<') {
                const int close = line.indexOfChar(i, '>');
                if (close < 0) { i = length; break; }
                flushOpcode();
                pushHeader(line.substring(i, close + 1).trim());
                i = close + 1;
                continue;
            }
            int wordEnd = i;
            while (wordEnd < length && !juce::CharacterFunctions::isWhitespace(line[wordEnd]) && line[wordEnd] != '<') wordEnd++;
            const juce::String word = line.substring(i, wordEnd);
            i = wordEnd;
            const int equals = word.indexOfChar('=');
            if (equals > 0) {
                flushOpcode();
                opcodeKey = word.substring(0, equals);
                opcodeValue = word.substring(equals + 1);
            } else if (opcodeKey.isNotEmpty()) {
                opcodeValue << ' ' << word;      // continuation of a value containing spaces
            } else {
                std::cerr << "SFZ2DS: ignoring '" << word << "', which is not an opcode" << std::endl;
            }
        }
        // A value never continues onto the next line.
        flushOpcode();
    }
    flushOpcode();
    reportCurves();
}

// A group starts out with its <master>'s opcodes, so they reach every region below it the way
// SFZ inherits them (global -> master -> group -> region), and the group's own opcodes,
// applied afterwards, override them. DecentSampler has no level between <groups> and <group>,
// so this is where a master's settings have to live.
void DSSFZ::startGroup() {
    currentGroup = juce::ValueTree("group");
    if (currentMaster.isValid()) {
        currentGroup.copyPropertiesFrom(currentMaster, nullptr);
    }
    valueTree.appendChild(currentGroup, nullptr);
}

// <curve> only shapes MIDI CC modulation (the *_curveccN opcodes), and no CC modulation is
// imported: DecentSampler's CC sources start at 0 with no per-preset default, so an imported
// amplitude_oncc7 or pan_oncc10 would leave the instrument silent or panned hard until the
// controller moved. Say exactly what is being dropped rather than just "unsupported".
void DSSFZ::reportCurves() {
    for (auto curve : valueTree) {
        if (!curve.hasType("curve")) continue;
        const int index = curve.getProperty("curve_index", -1);
        auto users = curveUsers.find(index);
        if (users != curveUsers.end()) {
            std::cerr << "SFZ2DS: <curve> " << index << " ignored: it shapes "
                      << users->second.joinIntoString(", ")
                      << ", and MIDI CC modulation is not imported" << std::endl;
        } else {
            std::cerr << "SFZ2DS: <curve> " << index << " ignored: no opcode uses it" << std::endl;
        }
    }
}

void DSSFZ::pushHeader(juce::String token) {
    token = token.toLowerCase();
    if (token == "<global>")
    {
        currentSection = GLOBAL;
    } else if (token == "<control>")
    {
        // Holds default_path (and #define-style things this importer ignores). Kept on the
        // root so the converter can prefix sample paths with it.
        currentSection = CONTROL;
    } else if (token == "<master>")
    {
        // A new master replaces the previous one and ends its group, so a region that comes
        // straight after it gets a group of its own carrying this master's opcodes.
        currentSection = MASTER;
        currentMaster = juce::ValueTree("master");
        currentGroup = juce::ValueTree();
    } else if (token == "<group>")
    {
        currentSection = GROUP;
        startGroup();
    } else if (token == "<curve>")
    {
        currentSection = CURVE;
        currentCurve = juce::ValueTree("curve");
        valueTree.appendChild(currentCurve, nullptr);
    }
    else if (token == "<region>")
    {
        currentSection = REGION;
        currentRegion = juce::ValueTree("region");
        // A region with no <group> above it gets one of its own. The region's opcodes stay on
        // the region: this used to switch to GROUP, so the first such region's sample, key and
        // the rest landed on the group, became defaults for every later region in it, and the
        // region itself came out as an empty <sample/>.
        if(!currentGroup.isValid()) {
            startGroup();
        }
        currentGroup.appendChild(currentRegion, nullptr);
    } else
    {
        currentSection = UNKNOWN;
        std::cerr << "The header '" << token << "' is unsupported by SFZ2DS!" << std::endl;
    }
}

void DSSFZ::pushOpcode(juce::String token) {
    if (currentSection == UNKNOWN)
        return;
    
    int equalsIndex = token.indexOf("=");
    if (equalsIndex == -1) {
        std::cerr << "The token '" << token << "' doesn't have an '=' in it." << std::endl;
        return;
    }
    juce::String key = token.substring(0, equalsIndex);
    juce::String value = token.substring(equalsIndex+1).trimEnd();

    if (key.contains("_curvecc")) {
        auto &users = curveUsers[value.getIntValue()];
        users.addIfNotAlreadyThere(key);
    }
    
    if(currentSection == GLOBAL) {
        valueTree.setProperty(key, value, nullptr);
    } else if(currentSection == CONTROL) {
        if (key == "default_path") {
            valueTree.setProperty("default_path", value, nullptr);
        }
    } else if (currentSection == MASTER) {
        currentMaster.setProperty(key, value, nullptr);
    } else if (currentSection == CURVE) {
        currentCurve.setProperty(key, value, nullptr);
    } else if (currentSection == GROUP) {
        currentGroup.setProperty(key, value, nullptr);
    } else if (currentSection == REGION) {
        currentRegion.setProperty(key, value, nullptr);
    }
}
