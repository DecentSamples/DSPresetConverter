/*
  ==============================================================================

    DSPresetConverter.cpp
    Created: 16 Aug 2021 9:55:21pm
    Author:  David Hilowitz

  ==============================================================================
*/

#include "DSPresetConverter.h"
#include <algorithm>
#include <functional>
#include <map>
#include "../../../DSHelpers.h"

 DSPresetConverter::DSPresetConverter() {
     audioFormatManager.registerBasicFormats();
}

void DSPresetConverter::logPathChange(const juce::String& updatedPath) const {
    if (pathChangeLoggingEnabled) {
        std::cout << "Sample file path changed to \"" << updatedPath << "\"." << std::endl;
    }
}

// Sets the standard knob bound to an instrument-level envelope stage (addGenericUI) to `value`,
// widening its range if needed, so the knob starts where the imported instrument is.
static void setStandardKnobValue(juce::ValueTree preset, const juce::String& parameter, double value) {
    std::function<void(juce::ValueTree)> visit = [&](juce::ValueTree node) {
        for (auto child : node) {
            if (child.hasType("binding") && child.getProperty("parameter").toString() == parameter
                && child.getProperty("level").toString() == "instrument") {
                node.setProperty("value", value, nullptr);
                if (value > (double) node.getProperty("maxValue", value)) node.setProperty("maxValue", value, nullptr);
                if (value < (double) node.getProperty("minValue", value)) node.setProperty("minValue", value, nullptr);
            }
            visit(child);
        }
    };
    visit(preset.getChildWithName("ui"));
}

void DSPresetConverter::parseDSEXS24(DSEXS24 exs24) {

    // Setup the EXS24 data
    juce::Array<DSEXS24Zone> &zones = exs24.getZones();
    juce::Array<DSEXS24Group> &groups = exs24.getGroups();
    juce::Array<DSEXS24Sample> &samples = exs24.getSamples();
    const DSEXS24Settings &settings = exs24.getSettings();
    valueTree = juce::ValueTree("DecentSampler");
    
    // Add a generic UI
    addGenericUI();
    
    juce::ValueTree groupsVT = juce::ValueTree("groups");
    valueTree.appendChild(groupsVT, nullptr);

    // Instrument settings (the EXS parameter block)
    if (settings.volumeDb && *settings.volumeDb != 0) {
        groupsVT.setProperty("globalVolume", juce::String(*settings.volumeDb) + "dB", nullptr);
    }
    const double globalTuning = settings.transpose.value_or(0) + settings.fineTune.value_or(0) / 100.0;
    if (globalTuning != 0) {
        groupsVT.setProperty("globalTuning", globalTuning, nullptr);
    }
    if (settings.bendUp && *settings.bendUp != 2) {
        groupsVT.setProperty("pitchBendRange", *settings.bendUp, nullptr);
    }
    // The amp envelope, which the standard knobs control
    const struct { const std::optional<double>& value; const char* attribute; const char* parameter; double scale; } stages[] = {
        { settings.ampAttackMs, "attack", "ENV_ATTACK", 0.001 },
        { settings.ampDecayMs, "decay", "ENV_DECAY", 0.001 },
        { settings.ampSustain, "sustain", "ENV_SUSTAIN", 1.0 },
        { settings.ampReleaseMs, "release", "ENV_RELEASE", 0.001 },
    };
    for (const auto& stage : stages) {
        if (stage.value) {
            // three decimals is a millisecond, finer than Logic's own steps
            const double v = std::round(*stage.value * stage.scale * 1000.0) / 1000.0;
            groupsVT.setProperty(stage.attribute, juce::String(v, 3), nullptr);
            setStandardKnobValue(valueTree, stage.parameter, v);
        }
    }

    // Zones whose group doesn't exist play ungrouped, as they would have nowhere else to go
    for (DSEXS24Zone &zone : zones) {
        if (zone.groupIndex >= groups.size() || zone.groupIndex < -1) {
            zone.groupIndex = -1;
        }
    }

    juce::ValueTree tagsVT("tags");
    // round robin chain -> (position in the chain, the group as written), numbered once all are known
    std::map<int, std::vector<std::pair<int, juce::ValueTree>>> chains;
    
    // Iterate through the groups and add their respective samples
    for(int groupIndex = -1; groupIndex < groups.size(); groupIndex++) {
        bool hasSamples = false;
        DSEXS24Group group = (groupIndex >= 0) ? groups[groupIndex] : DSEXS24Group();
        juce::ValueTree dsGroup ("group");
        if(group.name != "") {
            dsGroup.setProperty("name", group.name, nullptr);
        }
        if(group.pan != 0) {
            dsGroup.setProperty("pan", group.pan, nullptr);
        }
        if(group.volume != 0) {
            dsGroup.setProperty("volume", juce::String(group.volume) + "dB", nullptr);
        }
        if(group.trigger != 0) {
            dsGroup.setProperty("trigger", "release", nullptr);
        }

        if(group.polyphony > 0) {
            // a group voice limit becomes a tag with that polyphony
            const juce::String tag = "exs_group_" + juce::String(groupIndex);
            dsGroup.setProperty("tags", tag, nullptr);
            juce::ValueTree tagVT("tag");
            tagVT.setProperty("name", tag, nullptr);
            tagVT.setProperty("polyphony", group.polyphony, nullptr);
            tagsVT.appendChild(tagVT, nullptr);
        }
        
        for (const DSEXS24Zone &zone : zones) {
            if (zone.groupIndex == groupIndex) {
                juce::ValueTree dsSample("sample");
                
                int sampleIndex = zone.sampleIndex;
                if(sampleIndex < 0 || sampleIndex >= samples.size()) {
                    continue;
                }

                // the zone's velocity range, narrowed by its group's
                int loVel = zone.velocityRangeOn ? zone.loVel : 0;
                int hiVel = zone.velocityRangeOn ? zone.hiVel : 127;
                if (groupIndex >= 0) {
                    loVel = juce::jmax(loVel, (int) group.velRangeLow);
                    hiVel = juce::jmin(hiVel, (int) group.velRangeHigh);
                }
                if (loVel > hiVel) {
                    continue;
                }
                
                DSEXS24Sample &sample = samples.getReference(sampleIndex);
                dsSample.setProperty("path", sample.fileName, nullptr);
                if (sample.filePath.isNotEmpty()) {
                    dsSample.setProperty("_exsFilePath", sample.filePath, nullptr);
                }
                    
                dsSample.setProperty("name", zone.name, nullptr);
                if(zone.pitch == false) {
                    dsSample.setProperty("pitchKeyTrack", 0, nullptr);
                }
                dsSample.setProperty("rootNote", zone.key, nullptr);
                dsSample.setProperty("loNote", zone.keyLow, nullptr);
                dsSample.setProperty("hiNote", zone.keyHigh, nullptr);
                dsSample.setProperty("loVel", loVel, nullptr);
                dsSample.setProperty("hiVel", hiVel, nullptr);
                
                float tuning = (float) zone.coarseTuning + (((float)zone.fineTuning)/100.0f);
                if(tuning != 0) {
                    dsSample.setProperty("tuning", tuning, nullptr);
                }
                if(zone.pan != 0) {
                    dsSample.setProperty("pan", zone.pan, nullptr);
                }
                if(zone.volume != 0) {
                    dsSample.setProperty("volume", juce::String(zone.volume) + "dB", nullptr);
                }
                
                if(zone.sampleStart != 0) {
                    dsSample.setProperty("start", zone.sampleStart, nullptr);
                }
                
                if(zone.sampleEnd != 0) {
                    dsSample.setProperty("end", zone.sampleEnd - 1, nullptr);
                }
                
                if(zone.loopEnabled) {
                    dsSample.setProperty("loopEnabled", zone.loopEnabled, nullptr);
                    dsSample.setProperty("loopStart", zone.loopStart, nullptr);
                    dsSample.setProperty("loopEnd", ((zone.loopEnd > 0 ? zone.loopEnd - 1 : 0)), nullptr);
                
                    if(zone.loopCrossfadeMilliseconds != 0) {
                        dsSample.setProperty("loopCrossfadeMilliseconds", zone.loopCrossfadeMilliseconds, nullptr);
                    }
                    
                    dsSample.setProperty("loopCrossfadeMode", zone.loopEqualPower ? "equal_power" : "linear", nullptr);
                }
                
                hasSamples = true;
                dsGroup.appendChild(dsSample, nullptr);
            }
        }
        if(hasSamples) {
            groupsVT.appendChild(dsGroup, nullptr);
            if (group.seqChain >= 0) {
                chains[group.seqChain].push_back({ group.seqNumber, dsGroup });
            }
        }
    }

    // Number each round robin over the groups that made it into the preset: a chain member with no
    // playable zones would otherwise leave a silent step in the cycle
    for (auto& [chain, members] : chains) {
        if (members.size() < 2) {
            continue;
        }
        std::sort(members.begin(), members.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (size_t k = 0; k < members.size(); k++) {
            members[k].second.setProperty("seqMode", "round_robin", nullptr);
            members[k].second.setProperty("seqPosition", (int) k + 1, nullptr);
            members[k].second.setProperty("seqLength", (int) members.size(), nullptr);
        }
    }

    if (tagsVT.getNumChildren() > 0) {
        valueTree.appendChild(tagsVT, nullptr);
    }
}

// SFZ key opcodes take either a MIDI number or a note name, "c-1" to "g9" with "#" or "b",
// in either case (0000479). Middle C is c4 = 60. Returns the input untouched when it is not
// a note name, so a number passes straight through.
static juce::var sfzKeyValue(const juce::var &value) {
    const juce::String text = value.toString().trim().toLowerCase();
    if (text.isEmpty() || juce::CharacterFunctions::isDigit(text[0]) || text[0] == '-') {
        return value;
    }
    static const juce::String letters = "c d e f g a b";
    const int letterIndex = letters.indexOfChar(text[0]);
    if (letterIndex < 0) {
        return value;
    }
    static const int semitones[] = { 0, 0, 2, 0, 4, 0, 5, 0, 7, 0, 9, 0, 11 };
    int note = semitones[letterIndex];
    int i = 1;
    if (i < text.length() && text[i] == '#') { note++; i++; }
    else if (i < text.length() && text[i] == 'b') { note--; i++; }
    const juce::String octaveText = text.substring(i);
    if (octaveText.isEmpty() || !octaveText.containsOnly("-0123456789") || octaveText == "-") {
        return value;
    }
    const int octave = octaveText.getIntValue();
    return juce::var(juce::jlimit(0, 127, (octave + 1) * 12 + note));
}

// Regions that share a key and velocity range and carry lorand/hirand are one random round
// robin: each lands in its own position, in order of lorand, over a sequence as long as the
// set (0000479). The SFZ ranges themselves are dropped, since DecentSampler picks uniformly.
static void translateSFZRandomRoundRobins(juce::ValueTree &dsGroup) {
    juce::Array<juce::ValueTree> candidates;
    for (auto sample : dsGroup) {
        if (sample.hasProperty("_lorand") || sample.hasProperty("_hirand")) {
            candidates.add(sample);
        }
    }
    while (!candidates.isEmpty()) {
        juce::ValueTree first = candidates.getFirst();
        auto sameZone = [&first](const juce::ValueTree &other) {
            for (const char* key : { "loNote", "hiNote", "rootNote", "loVel", "hiVel" }) {
                if (first.getProperty(key).toString() != other.getProperty(key).toString()) return false;
            }
            return true;
        };
        juce::Array<juce::ValueTree> zone;
        for (int i = candidates.size(); --i >= 0;) {
            if (sameZone(candidates.getReference(i))) {
                zone.add(candidates.getReference(i));
                candidates.remove(i);
            }
        }
        std::sort(zone.begin(), zone.end(), [](const juce::ValueTree &a, const juce::ValueTree &b) {
            return (double) a.getProperty("_lorand", 0.0) < (double) b.getProperty("_lorand", 0.0);
        });
        for (int i = 0; i < zone.size(); i++) {
            zone.getReference(i).setProperty("seqMode", "random", nullptr);
            zone.getReference(i).setProperty("seqPosition", i + 1, nullptr);
            zone.getReference(i).setProperty("seqLength", zone.size(), nullptr);
        }
    }
    for (auto sample : dsGroup) {
        sample.removeProperty("_lorand", nullptr);
        sample.removeProperty("_hirand", nullptr);
    }
}

void DSPresetConverter::parseSFZValueTree(juce::ValueTree sfz) {
    valueTree = juce::ValueTree("DecentSampler");
    
    // Add a generic UI
    addGenericUI();
    
    juce::ValueTree groupsVT = juce::ValueTree("groups");
    translateSFZRegionProperties(sfz, groupsVT, headerLevelGlobal);
    valueTree.appendChild(groupsVT, nullptr);

    // <control> default_path prefixes every sample path (0000479).
    juce::String defaultPath = sfz.getProperty("default_path", "").toString().replaceCharacter('\\', '/');
    if (defaultPath.isNotEmpty() && !defaultPath.endsWithChar('/')) {
        defaultPath << '/';
    }
    
    for (auto sfzSection : sfz) {
        if(sfzSection.hasType("group")) {
            juce::ValueTree dsGroup ("group");
            translateSFZRegionProperties(sfzSection, dsGroup, headerLevelGroup);
            for (auto sfzRegion : sfzSection) {
                juce::ValueTree dsSample("sample");
                translateSFZRegionProperties(sfzRegion, dsSample, headerLevelRegion);
                if (defaultPath.isNotEmpty() && dsSample.hasProperty("path")) {
                    juce::String path = dsSample.getProperty("path").toString().replaceCharacter('\\', '/');
                    if (!juce::File::isAbsolutePath(path)) {
                        dsSample.setProperty("path", defaultPath + path, nullptr);
                    }
                }
                dsGroup.appendChild(dsSample, nullptr);
            }
            translateSFZRandomRoundRobins(dsGroup);
            groupsVT.appendChild(dsGroup, nullptr);
        }
    }
}

void DSPresetConverter::parseDSSF2Preset(const DSSF2Preset& preset) {
    valueTree = juce::ValueTree("DecentSampler");

    // Add a generic UI for imported presets.
    addGenericUI();

    juce::ValueTree groupsVT = juce::ValueTree("groups");
    valueTree.appendChild(groupsVT, nullptr);

    juce::ValueTree dsGroup("group");
    if (preset.name.isNotEmpty()) {
        dsGroup.setProperty("name", preset.name, nullptr);
    }

    int sequenceLength = 0;
    for (const auto& region : preset.regions) {
        juce::ValueTree dsSample("sample");
        dsSample.setProperty("path", region.samplePath, nullptr);

        if (region.name.isNotEmpty()) {
            dsSample.setProperty("name", region.name, nullptr);
        }

        if (!region.pitchKeyTrack) {
            dsSample.setProperty("pitchKeyTrack", 0, nullptr);
        }

        dsSample.setProperty("rootNote", region.rootNote, nullptr);
        dsSample.setProperty("loNote", region.loNote, nullptr);
        dsSample.setProperty("hiNote", region.hiNote, nullptr);
        dsSample.setProperty("loVel", region.loVel, nullptr);
        dsSample.setProperty("hiVel", region.hiVel, nullptr);

        if (region.tuning != 0.0f) {
            dsSample.setProperty("tuning", region.tuning, nullptr);
        }

        if (region.pan != 0.0f) {
            dsSample.setProperty("pan", region.pan, nullptr);
        }

        if (region.volumeDb != 0.0f) {
            dsSample.setProperty("volume", juce::String(region.volumeDb) + "dB", nullptr);
        }

        if (region.start > 0) {
            dsSample.setProperty("start", region.start, nullptr);
        }

        if (region.end >= 0) {
            dsSample.setProperty("end", region.end, nullptr);
        }

        if (region.loopEnabled && region.loopEnd > region.loopStart) {
            dsSample.setProperty("loopEnabled", true, nullptr);
            dsSample.setProperty("loopStart", region.loopStart, nullptr);
            dsSample.setProperty("loopEnd", region.loopEnd, nullptr);
        }

        if (region.envDelay)   dsSample.setProperty("ampEnvDelay", *region.envDelay, nullptr);
        if (region.envAttack)  dsSample.setProperty("attack", *region.envAttack, nullptr);
        if (region.envHold)    dsSample.setProperty("ampEnvHold", *region.envHold, nullptr);
        if (region.envDecay)   dsSample.setProperty("decay", *region.envDecay, nullptr);
        if (region.envSustain) dsSample.setProperty("sustain", *region.envSustain, nullptr);
        if (region.envRelease) dsSample.setProperty("release", *region.envRelease, nullptr);

        if (region.seqPosition > 0) {
            dsSample.setProperty("seqMode", "round_robin", nullptr);
            dsSample.setProperty("seqPosition", region.seqPosition, nullptr);
            sequenceLength = juce::jmax(sequenceLength, region.seqPosition);
        }

        dsGroup.appendChild(dsSample, nullptr);
    }

    if (sequenceLength > 0) {
        for (auto sampleVT : dsGroup) {
            if (sampleVT.hasProperty("seqPosition")) {
                sampleVT.setProperty("seqLength", sequenceLength, nullptr);
            }
        }
    }

    if (dsGroup.getNumChildren() > 0) {
        groupsVT.appendChild(dsGroup, nullptr);
    }
}

void DSPresetConverter::translateSFZRegionProperties(juce::ValueTree sfzRegion, juce::ValueTree &dsEntity, HeaderLevel level) {
    for (int i = 0; i < sfzRegion.getNumProperties(); i++) {
        juce::String key = sfzRegion.getPropertyName(i).toString();
        juce::var value =      sfzRegion.getProperty(key);
        
        if(level == headerLevelGroup && key == "group_label") {
            dsEntity.setProperty("name", value, nullptr);
        } else if(key == "amp_veltrack") {
            dsEntity.setProperty("ampVelTrack", ((float)value)/100.0f, nullptr);
        } else if(key == "ampeg_attack") {
            dsEntity.setProperty("attack", value, nullptr);
        } else if(key == "ampeg_release") {
            dsEntity.setProperty("release", value, nullptr);
        } else if(key == "ampeg_sustain") {
            // SFZ gives the sustain level as a percentage; Decent Sampler's is 0 to 1
            dsEntity.setProperty("sustain", juce::jlimit(0.0, 1.0, ((double)value)/100.0), nullptr);
        } else if(key == "ampeg_decay") {
            dsEntity.setProperty("decay", value, nullptr);
        } else if(key == "ampeg_delay") {
            dsEntity.setProperty("ampEnvDelay", value, nullptr);
        } else if(key == "ampeg_hold") {
            dsEntity.setProperty("ampEnvHold", value, nullptr);
        } else if(key == "group") {
            dsEntity.setProperty("tags", "voice-group-" + value.toString(), nullptr);
        } else if(key == "end") {
            dsEntity.setProperty("end", value, nullptr);
        } else if(key == "hikey") {
            dsEntity.setProperty("hiNote", sfzKeyValue(value), nullptr);
        } else if(key == "hivel") {
            dsEntity.setProperty("hiVel", value, nullptr);
        } else if(key == "key") {
            dsEntity.setProperty("rootNote", sfzKeyValue(value), nullptr);
            dsEntity.setProperty("loNote", sfzKeyValue(value), nullptr);
            dsEntity.setProperty("hiNote", sfzKeyValue(value), nullptr);
        } else if(key == "lokey") {
            dsEntity.setProperty("loNote", sfzKeyValue(value), nullptr);
        } else if(key == "lorand") {
            dsEntity.setProperty("_lorand", (double) value, nullptr);
        } else if(key == "hirand") {
            dsEntity.setProperty("_hirand", (double) value, nullptr);
        } else if(key == "loop_end") {
            dsEntity.setProperty("loopEnd", value, nullptr);
        } else if(key == "loop_mode") {
            // Set explicitly either way: loop points with loop_mode=no_loop used to come
            // through as loopStart/loopEnd alone, which the engine treats as a loop (0000479).
            const juce::String mode = value.toString().trim().toLowerCase();
            const bool loops = (mode == "loop_continuous" || mode == "loop_sustain");
            dsEntity.setProperty("loopEnabled", loops ? "true" : "false", nullptr);
        } else if(key == "loop_start") {
            dsEntity.setProperty("loopStart", value, nullptr);
        } else if(key == "lovel") {
            dsEntity.setProperty("loVel", value, nullptr);
        } else if(key == "off_by") {
            dsEntity.setProperty("silencedByTags", "voice-group-" + value.toString(), nullptr);
        } else if(key == "off_mode") {
            dsEntity.setProperty("silencingMode", value, nullptr);
        } else if(key == "offset") {
            dsEntity.setProperty("start", value, nullptr);
        } else if(key == "pitch_keycenter") {
            dsEntity.setProperty("rootNote", sfzKeyValue(value), nullptr);
        } else if(key == "sample") {
            dsEntity.setProperty("path", value, nullptr);
        } else if(key == "seq_position") {
            dsEntity.setProperty("seqPosition", value, nullptr);
            dsEntity.setProperty("seqMode", "round_robin", nullptr);
        } else if(key == "seq_length") {
            dsEntity.setProperty("seqLength", value, nullptr);
            dsEntity.setProperty("seqMode", "round_robin", nullptr);
        } else if(key == "sw_previous") {
            dsEntity.setProperty("previousNote", sfzKeyValue(value), nullptr);
        } else if(key == "trigger") {
            dsEntity.setProperty("trigger", value, nullptr);
        } else if(key == "tune") {
            dsEntity.setProperty("tuning", ((int)value)/100.0, nullptr);
        } else if(key == "volume") {
            dsEntity.setProperty("volume", value.toString() + "dB", nullptr);
        } else {
            switch (level) {
                case headerLevelGlobal:
                    DBG("<global> opcode " + key + " not supported.");
                    break;
                case headerLevelGroup:
                    DBG("<group> opcode " + key + " not supported.");
                    break;
                case headerLevelRegion:
                default:
                    DBG("<region> opcode " + key + " not supported.");
                    break;
            }
        }
    }
}

void DSPresetConverter::addGenericUI() {
    // Shared with the Preset Editor's own "Add Standard Controls & Effects..." command - see
    // DSHelpers.cpp for the actual XML, kept in one place rather than two copies.
    addStandardControlsAndEffects(valueTree, nullptr);
}

// Convert the current internal valueTree, which is in a basic DecentSampler format, into an SFZ file
juce::String DSPresetConverter::getSFZ() {
    // Initialize the SFZ file with a header
    juce::String sfz = "// SFZ file created with EXS2ALL by David Hilowitz\n\n";
    
    juce::ValueTree groupsValueTree = valueTree.getChildWithName("groups");
    if(groupsValueTree == juce::ValueTree()) {
        sfz = sfz + "// Converted EXS file was empty. ";
        return sfz;
    }
    
    sfz = sfz + "\n<control>\n";

    auto parseSampleAndGroupProperties = [this](juce::String &sfzFile, juce::ValueTree &valueTree, HeaderLevel level) {
        if(level == headerLevelGroup) {
             if (valueTree.hasProperty("name")) {
                 sfzFile = sfzFile + "group_label=" + valueTree.getProperty("name").toString() + " ";
             }
        }
        if(valueTree.hasProperty("path")) {
            sfzFile = sfzFile + "sample=" + valueTree.getProperty("path").toString() + " ";
        }
        if(valueTree.hasProperty("rootNote")) {
            sfzFile = sfzFile + "pitch_keycenter=" + valueTree.getProperty("rootNote").toString() + " ";
        }
        if(valueTree.hasProperty("loNote") ) {
            sfzFile = sfzFile + "lokey=" + valueTree.getProperty("loNote").toString() + " ";
        }
        if(valueTree.hasProperty("hiNote")) {
            sfzFile = sfzFile + "hikey=" + valueTree.getProperty("hiNote").toString() + " ";
        }
        if(valueTree.hasProperty("loVel") && valueTree.getProperty("loVel").toString() != "0") {
            sfzFile = sfzFile + "lovel=" + valueTree.getProperty("loVel").toString() + " ";
        }
        if(valueTree.hasProperty("hiVel") && valueTree.getProperty("hiVel").toString() != "127") {
            sfzFile = sfzFile + "hivel=" + valueTree.getProperty("hiVel").toString() + " ";
        }
        if(valueTree.hasProperty("start") && valueTree.getProperty("start").toString() != "0") {
            sfzFile = sfzFile + "offset=" + valueTree.getProperty("start").toString() + " ";
        }
        if(valueTree.hasProperty("end")) {
            sfzFile = sfzFile + "end=" + valueTree.getProperty("end").toString() + " ";
        }
        if(valueTree.hasProperty("loopEnabled")) {
            sfzFile = sfzFile + "loop_mode=loop_continuous ";
        }
        if(valueTree.hasProperty("loopStart")) {
            sfzFile = sfzFile + "loop_start=" + valueTree.getProperty("loopStart").toString() + " ";
        }
        if(valueTree.hasProperty("loopEnd")) {
            sfzFile = sfzFile + "loop_end=" + valueTree.getProperty("loopEnd").toString() + " ";
        }
        
        if(valueTree.hasProperty("pan")) {
            sfzFile = sfzFile + "pan=" + valueTree.getProperty("pan").toString() + " ";
        }

        if(valueTree.hasProperty("ampVelTrack") && valueTree.getProperty("ampVelTrack") != "1.0") {
            float value = valueTree.getProperty("ampVelTrack");
            if(value >= 0 && value < 1) {
                sfzFile = sfzFile + "amp_veltrack=" + juce::String((int)(value*100)) + " ";
            }
        }
        // 0000583: ampEnvAttack and the like are other spellings of attack and the like, and win
        auto envelope = [&valueTree] (const char* preferred, const char* legacy) {
            return valueTree.hasProperty(preferred) ? valueTree.getProperty(preferred) : valueTree.getProperty(legacy);
        };
        auto hasEnvelope = [&valueTree] (const char* preferred, const char* legacy) {
            return valueTree.hasProperty(preferred) || valueTree.hasProperty(legacy);
        };
        if(valueTree.hasProperty("ampEnvDelay")) {
            sfzFile = sfzFile + "ampeg_delay=" + valueTree.getProperty("ampEnvDelay").toString() + " ";
        }
        if(hasEnvelope("ampEnvAttack", "attack")) {
            sfzFile = sfzFile + "ampeg_attack=" + envelope("ampEnvAttack", "attack").toString() + " ";
        }
        if(valueTree.hasProperty("ampEnvHold")) {
            sfzFile = sfzFile + "ampeg_hold=" + valueTree.getProperty("ampEnvHold").toString() + " ";
        }
        if(hasEnvelope("ampEnvRelease", "release")) {
            sfzFile = sfzFile + "ampeg_release=" + envelope("ampEnvRelease", "release").toString() + " ";
        }
        if(hasEnvelope("ampEnvSustain", "sustain")) {
            sfzFile = sfzFile + "ampeg_sustain=" + juce::String(std::round(((double) envelope("ampEnvSustain", "sustain")) * 1000000.0) / 10000.0) + " ";
        }
        if(hasEnvelope("ampEnvDecay", "decay")) {
            sfzFile = sfzFile + "ampeg_decay=" + envelope("ampEnvDecay", "decay").toString() + " ";
        }
        if(valueTree.hasProperty("seqPosition")) {
            sfzFile = sfzFile + "seq_position=" + valueTree.getProperty("seqPosition").toString() + " ";
        }
        if(valueTree.hasProperty("seqLength")) {
            sfzFile = sfzFile + "seq_length=" + valueTree.getProperty("seqLength").toString() + " ";
        }
        if(valueTree.hasProperty("tags")) {
            if(valueTree.getProperty("tags").toString().contains("voice-group-")) {
                sfzFile = sfzFile + "group=" + valueTree.getProperty("tags").toString().replace("voice-group-", "") + " ";
            }
        }
        
        if(valueTree.hasProperty("silencedByTags")) {
            sfzFile = sfzFile + "off_by=" + valueTree.getProperty("silencedByTags").toString().replace("voice-group-", "") + " ";
        }
        if(valueTree.hasProperty("silencingMode")) {
            sfzFile = sfzFile + "off_mode=" + valueTree.getProperty("silencingMode").toString() + " ";
        }
        
        
        if(valueTree.hasProperty("previousNote")) {
            sfzFile = sfzFile + "sw_previous=" + valueTree.getProperty("previousNote").toString() + " ";
        }
        if(valueTree.hasProperty("trigger")) {
            sfzFile = sfzFile + "trigger=" + valueTree.getProperty("trigger").toString() + " ";
        }
        if(valueTree.hasProperty("tuning")) {
            sfzFile = sfzFile + "tune=" + juce::String((int)((float)valueTree.getProperty("tuning") * 100.0f)) + " ";
        }
        if(valueTree.hasProperty("volume")) {
            sfzFile = sfzFile + "volume=" + juce::String(linearOrDbStringToDb(valueTree.getProperty("volume").toString())) + " ";
        }
    };
    
    parseSampleAndGroupProperties(sfz, groupsValueTree, headerLevelGlobal);
    sfz << "\n";
    
    // Iterate through the groups and add their samples
     for (auto groupValueTree : groupsValueTree) {
         if(!groupValueTree.hasType("group")) {
             continue;
         }
         sfz << "<group>";
         parseSampleAndGroupProperties(sfz, groupValueTree, headerLevelGroup);
         sfz << "\n";
        
         for (auto sampleValueTree : groupValueTree) {
             if(!sampleValueTree.hasType(juce::Identifier("sample"))) {
                 continue;
             }
             
             sfz << "<region>";
             parseSampleAndGroupProperties(sfz, sampleValueTree, headerLevelRegion);
             sfz << "\n";
         }
     }
    return sfz;
}


double DSPresetConverter::linearOrDbStringToDb(const juce::String inputString) {
    if (inputString.contains("dB")) {
        return juce::jlimit(
             (double) -100,
             (double) 24,
             inputString.upToFirstOccurrenceOf("dB", false, true).getDoubleValue());
        
    } else {
        return juce::Decibels::gainToDecibels(inputString.getDoubleValue());
    }
}


// Finds each sample and makes its path absolute. Looks, in order, in the folder the EXS file recorded
// for it (_exsFilePath), the working directory, "<exs dir>/<instrument name>/", "<exs dir>/Samples/",
// beside the .exs, and finally by file name anywhere under the folder two levels above the .exs (as
// Logic's own search for moved samples does). Keeps going past a missing sample; returns false if any
// were missing.
bool DSPresetConverter::huntForSamples(juce::File inputDirectory, juce::String sampleSetName) {
    juce::ValueTree groupsValueTree = valueTree.getChildWithName("groups");
    if(groupsValueTree == juce::ValueTree()) {
        return true;
    }

    // name -> file under the search root, built on first use
    std::map<juce::String, juce::File> index;
    bool indexBuilt = false;
    const auto findByName = [&](const juce::String& fileName) -> juce::File {
        if (!indexBuilt) {
            indexBuilt = true;
            const juce::File root = inputDirectory.getParentDirectory().getParentDirectory();
            constexpr int maxDepth = 6;
            constexpr size_t maxFiles = 200000;   // never index a whole drive
            std::function<void(const juce::File&, int)> walk = [&](const juce::File& dir, int depth) {
                juce::Array<juce::File> subdirs;
                for (const auto& entry : juce::RangedDirectoryIterator(dir, false, "*", juce::File::findFilesAndDirectories)) {
                    if (index.size() >= maxFiles) {
                        return;
                    }
                    if (entry.isDirectory()) {
                        subdirs.add(entry.getFile());
                    } else {
                        index.emplace(entry.getFile().getFileName().toLowerCase(), entry.getFile());
                    }
                }
                if (depth < maxDepth) {
                    for (const auto& d : subdirs) {
                        walk(d, depth + 1);
                    }
                }
            };
            walk(root, 0);
        }
        auto it = index.find(fileName.toLowerCase());
        return it != index.end() ? it->second : juce::File();
    };

    bool allFound = true;
    auto parseSampleAndGroupProperties = [&](juce::ValueTree &vt) {
        if(!vt.hasProperty("path")) {
            return;
        }
        const juce::String path = vt.getProperty("path").toString();
        const juce::String recordedFolder = vt.getProperty("_exsFilePath").toString();

        juce::Array<juce::File> candidates;
        if (juce::File::isAbsolutePath(recordedFolder)) {
            candidates.add(juce::File(recordedFolder).getChildFile(path));
        }
        candidates.add(juce::File::getCurrentWorkingDirectory().getChildFile(path));
        candidates.add(inputDirectory.getChildFile(sampleSetName).getChildFile(path));
        candidates.add(inputDirectory.getChildFile("Samples").getChildFile(path));
        candidates.add(inputDirectory.getChildFile(path));

        for (const auto& candidate : candidates) {
            if (candidate.existsAsFile()) {
                vt.setProperty("path", candidate.getFullPathName(), nullptr);
                logPathChange(vt.getProperty("path").toString());
                return;
            }
        }

        const juce::File found = findByName(juce::File::createFileWithoutCheckingPath(path).getFileName());
        if (found != juce::File()) {
            vt.setProperty("path", found.getFullPathName(), nullptr);
            logPathChange(vt.getProperty("path").toString());
            return;
        }

        std::cerr << "Sample file \"" << path << "\" not found." << std::endl;
        allFound = false;
    };

    parseSampleAndGroupProperties(groupsValueTree);
    for (auto groupValueTree : groupsValueTree) {
        if(!groupValueTree.hasType("group")) {
            continue;
        }
        parseSampleAndGroupProperties(groupValueTree);
        for (auto sampleValueTree : groupValueTree) {
            if(sampleValueTree.hasType(juce::Identifier("sample"))) {
                parseSampleAndGroupProperties(sampleValueTree);
            }
        }
    }
    return allFound;
}

// This function needs to do more than just copy samples to the new directory, it needs to
// also make sure that the paths in the XML are updated to reflect the new location.
// It also needs to burn the loop crossfades into the wave files.
bool DSPresetConverter::copySamplesOverToNewDirectory(juce::File rootOutputDirectory, juce::String sampleSetName, bool skipAudioProcessing, int overrideBitrate, bool unifyDirectories, bool truncateLongFilenames, bool trimOnly) {
    juce::ValueTree groupsValueTree = valueTree.getChildWithName("groups");
    if(groupsValueTree == juce::ValueTree()) {
        return true;
    }

    const int maxStemLength = 48;

    auto sanitizeStem = [](juce::String stem) {
        while(stem.endsWithChar('.')) {
            stem = stem.dropLastCharacters(1);
        }
        if(stem.isEmpty()) {
            stem = "sample";
        }
        return stem;
    };

    auto truncateStemIfNeeded = [truncateLongFilenames, maxStemLength, &sanitizeStem](const juce::String& inputStem) {
        juce::String stem = sanitizeStem(inputStem);

        if(!truncateLongFilenames || stem.length() <= maxStemLength) {
            return stem;
        }

        const int hashLength = 4;
        const int maxPrefixLength = juce::jmax(1, maxStemLength - hashLength - 1);
        juce::String hash = juce::String::toHexString((juce::uint32) stem.hashCode());
        if(hash.length() < hashLength) {
            hash = hash.paddedLeft('0', hashLength);
        }
        hash = hash.substring(0, hashLength).toLowerCase();
        return sanitizeStem(stem.substring(0, maxPrefixLength) + "_" + hash);
    };

    auto checksumForFile = [](const juce::File& file, uint64_t& checksumOut) -> bool {
        checksumOut = 1469598103934665603ULL; // FNV-1a 64-bit offset basis
        if(!file.existsAsFile()) {
            return false;
        }

        std::unique_ptr<juce::FileInputStream> stream(file.createInputStream());
        if(stream == nullptr || !stream->openedOk()) {
            return false;
        }

        constexpr int bufferSize = 64 * 1024;
        juce::HeapBlock<char> buffer(bufferSize);
        while(!stream->isExhausted()) {
            int bytesRead = stream->read(buffer.getData(), bufferSize);
            if(bytesRead < 0) {
                return false;
            }
            for(int i = 0; i < bytesRead; ++i) {
                checksumOut ^= static_cast<uint8_t>(buffer[i]);
                checksumOut *= 1099511628211ULL; // FNV-1a 64-bit prime
            }
        }

        return true;
    };

    auto filesHaveMatchingChecksum = [&checksumForFile](const juce::File& left, const juce::File& right) {
        if(!left.existsAsFile() || !right.existsAsFile()) {
            return false;
        }
        if(left.getSize() != right.getSize()) {
            return false;
        }

        uint64_t leftChecksum = 0;
        uint64_t rightChecksum = 0;
        if(!checksumForFile(left, leftChecksum) || !checksumForFile(right, rightChecksum)) {
            return false;
        }
        return leftChecksum == rightChecksum;
    };

    auto resolveDestinationFile = [&truncateStemIfNeeded, &filesHaveMatchingChecksum](juce::File sampleOutputDir, const juce::File& sourceFile, bool unifyDirectories, bool& reuseExistingFile) {
        reuseExistingFile = false;

        const juce::String extension = sourceFile.getFileExtension();
        const juce::String truncatedStem = truncateStemIfNeeded(sourceFile.getFileNameWithoutExtension());
        juce::String candidateName = truncatedStem + extension;
        juce::File candidateFile = sampleOutputDir.getChildFile(candidateName);

        if(!candidateFile.existsAsFile()) {
            return candidateFile;
        }

        if(unifyDirectories && filesHaveMatchingChecksum(sourceFile, candidateFile)) {
            reuseExistingFile = true;
            return candidateFile;
        }

        int counter = 2;
        while(true) {
            candidateName = truncatedStem + "_" + juce::String(counter) + extension;
            candidateFile = sampleOutputDir.getChildFile(candidateName);
            if(!candidateFile.existsAsFile()) {
                return candidateFile;
            }
            if(unifyDirectories && filesHaveMatchingChecksum(sourceFile, candidateFile)) {
                reuseExistingFile = true;
                return candidateFile;
            }
            counter++;
        }
    };
    
    int             globalStart = groupsValueTree.getProperty("start", 0);
    int             globalEnd = groupsValueTree.getProperty("end", -1);
    bool            globalLoopEnabled = groupsValueTree.getProperty("loopEnabled", false);
    int             globalLoopStart = groupsValueTree.getProperty("loopStart", 0);
    int             globalLoopEnd = groupsValueTree.getProperty("loopEnd", -1);
    int             globalLoopCrossfade = groupsValueTree.getProperty("loopCrossfade", 0);
    juce::String    globalLoopCrossfadeMode = groupsValueTree.getProperty("loopCrossfadeMode", "linear");
    
    auto processAudioFiles = [this, &resolveDestinationFile, &truncateStemIfNeeded](juce::ValueTree &valueTree, juce::File outputDirectory, juce::String sampleSetName, int start, int end,  bool loopEnabled, int loopStart, int loopEnd, int loopCrossfade, juce::String loopCrossfadeMode, bool simpleCopy, int overrideBitrate, bool unifyDirectories, bool trimOnly) {
        if(!valueTree.hasProperty("path")) {
            return true;
        }
        juce::String path = valueTree.getProperty("path").toString();
        
        juce::File sampleFile = juce::File(path);
        if(!sampleFile.existsAsFile()) {
            std::cerr << "Sample file \"" << path << "\" not found." << std::endl;
            return false;
        }

        juce::File sampleOutputDir = unifyDirectories
            ? outputDirectory.getChildFile("Samples")
            : outputDirectory.getChildFile("Samples").getChildFile(sampleSetName);

        if(!outputDirectory.getChildFile("Samples").exists()) {
            juce::Result result = outputDirectory.getChildFile("Samples").createDirectory();
        }
        
        if(!sampleOutputDir.exists()) {
            juce::Result result = sampleOutputDir.createDirectory();
        }
        
        if(simpleCopy) {
            bool reuseExistingFile = false;
            juce::File destFile = resolveDestinationFile(sampleOutputDir, sampleFile, unifyDirectories, reuseExistingFile);
            if(!reuseExistingFile) {
                bool result = sampleFile.copyFileTo(destFile);
                if(!result) {
                    std::cerr << "Sample file \"" << path << "\" could not be copied." << std::endl;
                    return false;
                }
            }
            
            // Update the path
            juce::String simpleFilePath = (unifyDirectories ? "Samples/" : "Samples/" + sampleSetName + "/") + destFile.getFileName();
            valueTree.setProperty("path", simpleFilePath, nullptr);
            logPathChange(simpleFilePath);
            return true;
        }

        std::unique_ptr<juce::AudioFormatReader> reader (audioFormatManager.createReaderFor (sampleFile));
    
        std::unique_ptr<juce::AudioFormat> audioFormat;
        if(sampleFile.hasFileExtension("wav")) {
            audioFormat = std::unique_ptr<juce::AudioFormat>(new juce::WavAudioFormat());
        } else if(sampleFile.hasFileExtension("aif") || sampleFile.hasFileExtension("aiff")) {
            audioFormat = std::unique_ptr<juce::AudioFormat>(new juce::AiffAudioFormat());
        } else if(sampleFile.hasFileExtension("flac")) {
            audioFormat = std::unique_ptr<juce::AudioFormat>(new juce::FlacAudioFormat());
        } else {
            std::cerr << "Sample file \"" << path << "\" is in an unrecognizable format." << std::endl;
            return false;
        }
        
        // Get the loop points from the input file
        int numChannels = reader->numChannels;
        int sourceSampleRate = reader->sampleRate;
        
        int bitsPerSample = reader->bitsPerSample;
        if(overrideBitrate == 16 || overrideBitrate == 24 || overrideBitrate == 32)
            bitsPerSample = overrideBitrate;
        int fileLength = (int) reader->lengthInSamples;
        int fileMetadataLoopStart = reader->metadataValues.getValue("Loop0Start", "0").getIntValue();
        int fileMetadataLoopEnd = reader->metadataValues.getValue("Loop0End", "0").getIntValue();

        if(fileMetadataLoopStart > 0 && loopStart == 0 && fileMetadataLoopEnd > 0 && loopEnd == -1) {
            loopEnabled = true;
            loopStart = fileMetadataLoopStart;
            loopEnd = fileMetadataLoopEnd;
        }
        
        if(loopEnabled && loopEnd == -1) {
            loopEnd = fileLength - 1;
        }

        if(start < 0 || start >= fileLength) {
            std::cerr << "Sample file \"" << path << "\" has a start point that is out of range." << std::endl;
            return false;
        }

        if(end >= fileLength) {
            std::cerr << "Sample file \"" << path << "\" has an end point that is out of range." << std::endl;
            return false;
        }

        if(loopEnabled && end < loopStart) {
            std::cerr << "Sample file \"" << path << "\" has an end point that is before the loop start point." << std::endl;
            return false;
        }

        if(loopEnabled && end < loopEnd) {
            std::cerr << "Sample file \"" << path << "\" has an end point that is before the loop end point." << std::endl;
            return false;
        }
        
        if(loopEnabled && start > loopStart) {
            std::cerr << "Sample file \"" << path << "\" has a start point that is after the loop start point." << std::endl;
            return false;
        }

        // Some EXS files can report out-of-range loop crossfades.
        // Clamp to a safe value rather than aborting the full batch conversion.
        if(loopEnabled) {
            if(loopCrossfade < 0) {
                loopCrossfade = 0;
            }

            int maxCrossfadeByLoopLength = juce::jmax(0, loopEnd - loopStart);
            if(loopCrossfade > maxCrossfadeByLoopLength) {
                loopCrossfade = maxCrossfadeByLoopLength;
            }

            int maxCrossfadeByStartOffset = juce::jmax(0, loopStart - start);
            if(loopCrossfade > maxCrossfadeByStartOffset) {
                loopCrossfade = maxCrossfadeByStartOffset;
            }
        }
        
        if(start > end) {
            std::cerr << "Sample file \"" << path << "\" has a start point that is after the end point." << std::endl;
            return false;
        }

        

        // Create buffer to read the original file
        juce::AudioBuffer<float> buffer (numChannels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true);

        // Create output buffer with the correct size
        int outputLength = loopEnabled ? (1 + loopEnd - start) : (end + 1 - start);
        if(outputLength < 0) {
            std::cerr << "Sample file \"" << path << "\" has an end point that is before the start point." << std::endl;
            return false;
        }
        juce::AudioBuffer<float> outputBuffer (numChannels, outputLength);

        // If the loop is enabled and trimOnly is false, crossfade it.
        if(loopEnabled && !trimOnly && loopCrossfade > 0) {
            if(loopEnd < 0) {
                loopEnd = fileLength - 1;
            }
            loopStart = juce::jlimit(0, fileLength - 1, loopStart);
            loopEnd = juce::jlimit(0, fileLength - 1, loopEnd);
            
            for (int channel = 0; channel < numChannels; ++channel)
            {
                int index = 0;
                int chunk1Length = loopStart - start;
                // Copy the beginning of the file up to loopStart
                outputBuffer.copyFrom (channel, index, buffer, channel, start, chunk1Length);
                index += chunk1Length;
                
                // Copy the portion of the loop before the crossfade starts
                int chunk2Length = loopEnd - loopStart - loopCrossfade + 1;
                outputBuffer.copyFrom (channel, index, buffer, channel, loopStart, chunk2Length);
                index += chunk2Length;
                
               // Perform the crossfade, matching DSWavePlayer::getSample's live-playback crossfade:
               // fade-out reads the loop's true tail (ending at loopEnd), fade-in reads the material
               // immediately preceding loopStart, and the curve shape respects loopCrossfadeMode.
               for (int i = 0; i < loopCrossfade; ++i)
               {
                   float t = (float) i / (float) loopCrossfade;
                   float fadeOut = 1.0f - t;
                   float fadeIn = t;
                   if(loopCrossfadeMode != "linear") {
                       fadeOut = std::cos (t * juce::MathConstants<float>::halfPi);
                       fadeIn = std::sin (t * juce::MathConstants<float>::halfPi);
                   }
                   
                   float sampleToFadeOut = buffer.getSample (channel, (loopEnd - loopCrossfade + 1) + i);
                   float sampleToFadeIn = buffer.getSample (channel, (loopStart - loopCrossfade) + i);
                   
                   outputBuffer.setSample (channel, index + i,
                                           sampleToFadeOut * fadeOut +
                                           sampleToFadeIn * fadeIn);
               }
            }
        } else {
            int newFileLength = outputLength;
            for (int channel = 0; channel < numChannels; ++channel) {
                // Copy the trimmed region as-is. For looped samples this still removes unused tails.
                outputBuffer.copyFrom (channel, 0, buffer, channel, start, newFileLength);
                
            }
        }
        
        // Now that we've adjust 
        end -= start;
        if(loopEnabled) {
            loopStart -= start;
            loopEnd -= start;
        }
        end = juce::jmin(end, outputLength);
        start = 0;

        // Create the output directory
        if(!outputDirectory.getChildFile("Samples").exists())
            outputDirectory.getChildFile("Samples").createDirectory();
        
        if(!sampleOutputDir.exists())
            sampleOutputDir.createDirectory();
        
        if(!sampleOutputDir.exists()) {
            std::cerr << "Unable to create output directory." << std::endl;
            return false;
        }
        
        // Write the new buffer to the output file
        juce::File outputFile = sampleOutputDir.getChildFile(truncateStemIfNeeded(sampleFile.getFileNameWithoutExtension()) + "." + audioFormat->getFileExtensions()[0]);
        if(outputFile.existsAsFile()) {
            int counter = 2;
            juce::String baseStem = outputFile.getFileNameWithoutExtension();
            juce::String extensionWithDot = "." + audioFormat->getFileExtensions()[0];
            while(outputFile.existsAsFile()) {
                outputFile = sampleOutputDir.getChildFile(baseStem + "_" + juce::String(counter) + extensionWithDot);
                counter++;
            }
        }
        
        std::unordered_map<juce::String, juce::String> metadata;

        if(loopEnabled) {
            // Set Loop0Start and Loop0End in metadata
            metadata["Loop0Start"] = juce::String(loopStart);
            metadata["Loop0End"] = juce::String(loopEnd);
            metadata["Loop0Identifier"] = juce::String(0);
            metadata["Loop0Type"] = juce::String(0);
            metadata["NumSampleLoops"] = "1";
        }
        
        std::unique_ptr<juce::OutputStream> outputStream = std::make_unique<juce::FileOutputStream> (outputFile);
        juce::AudioFormatWriterOptions writerOptions = juce::AudioFormatWriterOptions{}
                                .withSampleRate (sourceSampleRate)
                                .withNumChannels ((int) numChannels)
                                .withBitsPerSample (bitsPerSample)
                                .withMetadataValues (metadata);
        
        std::unique_ptr<juce::AudioFormatWriter> writer (audioFormat->createWriterFor (outputStream, writerOptions));
        if (writer.get() != nullptr) {
            writer->writeFromAudioSampleBuffer (outputBuffer, 0, outputBuffer.getNumSamples());
            writer->flush();
        }

        // Update the properties in the value tree
        juce::String simpleFilePath = (unifyDirectories ? "Samples/" : "Samples/" + sampleSetName + "/") + outputFile.getFileName();
        valueTree.setProperty("path", simpleFilePath, nullptr);
        logPathChange(simpleFilePath);

        valueTree.setProperty("start", start, nullptr);
        valueTree.setProperty("end", end, nullptr);

        if(loopEnabled) {
            valueTree.setProperty("loopEnabled", loopEnabled, nullptr);
            valueTree.setProperty("loopStart", loopStart , nullptr);
            valueTree.setProperty("loopEnd", loopEnd, nullptr);
            if(trimOnly || loopCrossfade <= 0) {
                valueTree.setProperty("loopCrossfade", loopCrossfade, nullptr);
                valueTree.setProperty("loopCrossfadeMode", loopCrossfadeMode, nullptr);
            } else {
                valueTree.setProperty("loopCrossfade", 0, nullptr);
                valueTree.removeProperty("loopCrossfadeMode", nullptr);
            }
        }
        
        return true;        
    };

    if(!processAudioFiles(groupsValueTree, rootOutputDirectory, sampleSetName, globalStart, globalEnd, globalLoopEnabled, globalLoopStart, globalLoopEnd, globalLoopCrossfade, globalLoopCrossfadeMode,skipAudioProcessing, overrideBitrate, unifyDirectories, trimOnly))
        return false;

    // Iterate through the groups and add their samples
        for (auto groupValueTree : groupsValueTree) {
            if(!groupValueTree.hasType("group")) {
                continue;
            }

            int             groupStart = groupValueTree.getProperty("start", globalStart);
            int             groupEnd = groupValueTree.getProperty("end", globalEnd);
            bool            groupLoopEnabled = groupValueTree.getProperty("loopEnabled", globalLoopEnabled);
            int             groupLoopStart = groupValueTree.getProperty("loopStart", globalLoopStart);
            int             groupLoopEnd = groupValueTree.getProperty("loopEnd", globalLoopEnd);
            int             groupLoopCrossfade = groupValueTree.getProperty("loopCrossfade", globalLoopCrossfade);
            juce::String    groupLoopCrossfadeMode = groupValueTree.getProperty("loopCrossfadeMode", globalLoopCrossfadeMode);
            
            if(!processAudioFiles(groupValueTree, rootOutputDirectory, sampleSetName, groupStart, groupEnd, groupLoopEnabled, groupLoopStart, groupLoopEnd, groupLoopCrossfade, groupLoopCrossfadeMode, skipAudioProcessing, overrideBitrate, unifyDirectories, trimOnly))
                return false;
            
            for (auto sampleValueTree : groupValueTree) {
                if(!sampleValueTree.hasType(juce::Identifier("sample"))) {
                    continue;
                }

                int             sampleStart = sampleValueTree.getProperty("start", groupStart);
                int             sampleEnd = sampleValueTree.getProperty("end", groupEnd);
                bool            sampleLoopEnabled = sampleValueTree.getProperty("loopEnabled", groupLoopEnabled);
                int             sampleLoopStart = sampleValueTree.getProperty("loopStart", groupLoopStart);
                int             sampleLoopEnd = sampleValueTree.getProperty("loopEnd", groupLoopEnd);
                int             sampleLoopCrossfade = sampleValueTree.getProperty("loopCrossfade", groupLoopCrossfade);
                juce::String    sampleLoopCrossfadeMode = sampleValueTree.getProperty("loopCrossfadeMode", groupLoopCrossfadeMode);

                if(!processAudioFiles(sampleValueTree, rootOutputDirectory, sampleSetName, sampleStart, sampleEnd, sampleLoopEnabled, sampleLoopStart, sampleLoopEnd, sampleLoopCrossfade, sampleLoopCrossfadeMode, skipAudioProcessing, overrideBitrate, unifyDirectories, trimOnly)) {
                    std::cerr << "A problem was encountered when processing file \"" << sampleValueTree.getProperty("path").toString() << "\". Halting conversion process." << std::endl;
                    return false;
                }
            }
        }
    return true;
}

bool DSPresetConverter::stripMissingImageReferences(juce::File outputDirectory) {
    juce::ValueTree uiValueTree = valueTree.getChildWithName("ui");
    if(uiValueTree == juce::ValueTree()) {
        return true;
    }

    auto makePath = [outputDirectory](const juce::String& relativePath) {
        juce::String normalized = relativePath.replace("\\", "/");
        if(normalized.isEmpty()) {
            return juce::File();
        }
        return outputDirectory.getChildFile(normalized);
    };

    if(uiValueTree.hasProperty("bgImage")) {
        juce::String bgImagePath = uiValueTree.getProperty("bgImage").toString();
        juce::File bgImageFile = makePath(bgImagePath);
        if(!bgImageFile.existsAsFile()) {
            uiValueTree.removeProperty("bgImage", nullptr);
            logPathChange("Removed missing bgImage reference: \"" + bgImagePath + "\".");
        }
    }

    for(int i = uiValueTree.getNumChildren() - 1; i >= 0; --i) {
        juce::ValueTree child = uiValueTree.getChild(i);
        if(!child.hasType("image") || !child.hasProperty("path")) {
            continue;
        }

        juce::String imagePath = child.getProperty("path").toString();
        juce::File imageFile = makePath(imagePath);
        if(!imageFile.existsAsFile()) {
            uiValueTree.removeChild(i, nullptr);
            logPathChange("Removed missing image reference: \"" + imagePath + "\".");
        }
    }

    return true;
}

// Go through the value tree and convert the EXS loop crossfade value which are in milliseconds to the DecentSampler sample-based format
bool DSPresetConverter::convertEXSLoopCrossfadePoints() {
    juce::ValueTree groupsValueTree = valueTree.getChildWithName("groups");
    if(groupsValueTree == juce::ValueTree()) {
        return true;
    }
    
    int globalLoopCrossfadeMilliseconds = groupsValueTree.getProperty("loopCrossfadeMilliseconds", -1);
    groupsValueTree.removeProperty("loopCrossfadeMilliseconds", nullptr);
    
    auto updateLoopCrossfade = [this](juce::ValueTree &valueTree, int parentLoopCrossfadeMilliseconds) {
        int loopCrossfadeMilliseconds = valueTree.getProperty("loopCrossfadeMilliseconds", parentLoopCrossfadeMilliseconds);
        valueTree.removeProperty("loopCrossfadeMilliseconds", nullptr);
        if(loopCrossfadeMilliseconds == -1) {
            return true;
        }
        
        if(!valueTree.hasProperty("path")) {
            return true;
        }
        juce::String path = valueTree.getProperty("path").toString();
        
        // a sample that wasn't found, or can't be read, keeps no crossfade rather than stopping the import
        if(!juce::File::isAbsolutePath(path) || !juce::File(path).existsAsFile()) {
            return true;
        }
        std::unique_ptr<juce::AudioFormatReader> reader (audioFormatManager.createReaderFor (juce::File(path)));
        if (reader == nullptr) {
            return true;
        }
        const double sourceSampleRate = reader->sampleRate;
        
        valueTree.setProperty("loopCrossfade", (int) ((float) loopCrossfadeMilliseconds * 0.001 * sourceSampleRate), nullptr);
        
        return true;
    };
    
    if(!updateLoopCrossfade(groupsValueTree, 0))
        return false;
    
    // Iterate through the groups and add their samples
    for (auto groupValueTree : groupsValueTree) {
        if(!groupValueTree.hasType("group")) {
            continue;
        }
        
        int groupLoopCrossfadeMilliseconds = groupValueTree.getProperty("loopCrossfadeMilliseconds", globalLoopCrossfadeMilliseconds);
        groupValueTree.removeProperty("loopCrossfadeMilliseconds", nullptr);
        if(!updateLoopCrossfade(groupValueTree, groupLoopCrossfadeMilliseconds))
            return false;
        
        for (auto sampleValueTree : groupValueTree) {
            if(!sampleValueTree.hasType(juce::Identifier("sample"))) {
                continue;
            }
            
            int sampleLoopCrossfadeMilliseconds = sampleValueTree.getProperty("loopCrossfadeMilliseconds", groupLoopCrossfadeMilliseconds);
            sampleValueTree.removeProperty("loopCrossfadeMilliseconds", nullptr);
            
            if(!updateLoopCrossfade(sampleValueTree, sampleLoopCrossfadeMilliseconds)) {
                std::cerr << "Sample file \"" << sampleValueTree.getProperty("path").toString() << "\" not found." << std::endl;
                return false;
            }
        }
    }
        
    return true;
}

bool DSPresetConverter::convertPathsToDesiredDirectory(juce::File inputDirectory, juce::String desiredDirectoryName) {
    juce::ValueTree groupsValueTree = valueTree.getChildWithName("groups");
    if(groupsValueTree == juce::ValueTree()) {
        return true;
    }

    desiredDirectoryName = desiredDirectoryName.replace("\\", "/");

    if(desiredDirectoryName.startsWith("/")) {
        desiredDirectoryName = desiredDirectoryName.substring(1);
    }

    if(desiredDirectoryName.endsWith("/")) {
        desiredDirectoryName = desiredDirectoryName.dropLastCharacters(1);
    }
    
    auto convertPath = [this](juce::ValueTree &valueTree, juce::String desiredDirectoryName) {
        if(!valueTree.hasProperty("path")) {
            return true;
        }
        juce::String path = valueTree.getProperty("path").toString();
        
        juce::File sampleFile = juce::File(path);
        if(!sampleFile.existsAsFile()) {
            std::cerr << "Sample file \"" << path << "\" not found." << std::endl;
            return false;
        }
        
        juce::String simpleFilePath = desiredDirectoryName + "/" + sampleFile.getFileName();
        valueTree.setProperty("path", simpleFilePath, nullptr);
        logPathChange(simpleFilePath);
        return true;
    };
    
    if(!convertPath(groupsValueTree, desiredDirectoryName))
        return false;
    
    // Iterate through the groups and add their samples
    for (auto groupValueTree : groupsValueTree) {
        if(!groupValueTree.hasType("group")) {
            continue;
        }
        
        if(!convertPath(groupValueTree, desiredDirectoryName))
            return false;
        
        for (auto sampleValueTree : groupValueTree) {
            if(!sampleValueTree.hasType(juce::Identifier("sample"))) {
                continue;
            }
            
            if(!convertPath(sampleValueTree, desiredDirectoryName)) {
                std::cerr << "Sample file \"" << sampleValueTree.getProperty("path").toString() << "\" not found." << std::endl;
                return false;
            }
        }
    }
        
    return true;
}

bool DSPresetConverter::convertPathsToRelative(juce::File inputDirectory) {
    juce::ValueTree groupsValueTree = valueTree.getChildWithName("groups");
    if(groupsValueTree == juce::ValueTree()) {
        return true;
    }
    
    auto convertPath = [this](juce::ValueTree &valueTree, juce::File inputDirectory) {
        if(!valueTree.hasProperty("path")) {
            return true;
        }
        juce::String path = valueTree.getProperty("path").toString();
        
        juce::File sampleFile = juce::File(path);
        if(!sampleFile.existsAsFile()) {
            std::cerr << "Sample file \"" << path << "\" not found." << std::endl;
            return false;
        }
        
        juce::String simpleFilePath = sampleFile.getRelativePathFrom(inputDirectory);
        valueTree.setProperty("path", simpleFilePath, nullptr);
        logPathChange(simpleFilePath);
        return true;
    };
    
    if(!convertPath(groupsValueTree, inputDirectory))
        return false;
    
    // Iterate through the groups and add their samples
    for (auto groupValueTree : groupsValueTree) {
        if(!groupValueTree.hasType("group")) {
            continue;
        }
        
        if(!convertPath(groupValueTree, inputDirectory))
            return false;
        
        for (auto sampleValueTree : groupValueTree) {
            if(!sampleValueTree.hasType(juce::Identifier("sample"))) {
                continue;
            }
            
            if(!convertPath(sampleValueTree, inputDirectory)) {
                std::cerr << "Sample file \"" << sampleValueTree.getProperty("path").toString() << "\" not found." << std::endl;
                return false;
            }
        }
    }
        
    return true;
}
