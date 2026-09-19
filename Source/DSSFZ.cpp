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
    currentGroup = juce::ValueTree();
    currentRegion = juce::ValueTree();

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
    } else if (token == "<group>")
    {
        currentSection = GROUP;
        currentGroup = juce::ValueTree("group");
        valueTree.appendChild(currentGroup, nullptr);
    }
    else if (token == "<region>")
    {
        currentSection = REGION;
        currentRegion = juce::ValueTree("region");
        if(!currentGroup.isValid()) {
            currentSection = GROUP;
            currentGroup = juce::ValueTree("group");
            valueTree.appendChild(currentGroup, nullptr);
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
    
    if(currentSection == GLOBAL) {
        valueTree.setProperty(key, value, nullptr);
    } else if(currentSection == CONTROL) {
        if (key == "default_path") {
            valueTree.setProperty("default_path", value, nullptr);
        }
    } else if (currentSection == GROUP) {
        currentGroup.setProperty(key, value, nullptr);
    } else if (currentSection == REGION) {
        currentRegion.setProperty(key, value, nullptr);
    }
}
