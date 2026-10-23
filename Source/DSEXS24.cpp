/*
  ==============================================================================

    DSEXS24.cpp
    Created: 16 Aug 2021 9:55:07pm
    Author:  David Hilowitz

    A port of the EXSFormat repo's reference reader (exsformat/exs.py). Every read is bounds-checked
    against its record, so a damaged file is refused ("corrupt") rather than read past its end.

  ==============================================================================
*/

#include "DSEXS24.h"
#include <cmath>
#include <cstring>

namespace
{
    constexpr size_t recordHeader = 84;
    constexpr int typeInstrument = 0, typeZone = 1, typeGroup = 2, typeSample = 3, typeParameters = 4;
    constexpr int flagBigSize = 0x80;       // the record's size carries an extra 0x8000
    constexpr size_t maxRecords = 1u << 20;

    // Instrument parameter ids, from David's controlled Logic saves
    constexpr int pBendUp = 3, pBendDown = 4, pVoices = 5, pVolume = 7, pTranspose = 14, pFineTune = 15,
                  pGlide = 20, pFilterResonance = 29, pFilterCutoff = 30, pFilterOn = 44, pFilterType = 243,
                  pAmpSustain = 81, pAmpAttackHigh = 83, pAmpDecay = 84, pAmpRelease = 85;

    struct Corrupt {};

    /** One record, header included; reads past its end throw Corrupt. */
    struct Record
    {
        const juce::uint8* p = nullptr;
        size_t n = 0;
        bool bigEndian = false;

        void need (size_t off, size_t len) const { if (off > n || len > n - off) throw Corrupt(); }

        int u8 (size_t off) const { need (off, 1); return p[off]; }
        int i8 (size_t off) const { need (off, 1); return (int) (juce::int8) p[off]; }

        juce::uint32 u32 (size_t off) const
        {
            need (off, 4);
            return bigEndian ? juce::ByteOrder::bigEndianInt (p + off) : juce::ByteOrder::littleEndianInt (p + off);
        }

        int i32 (size_t off) const { return (int) u32 (off); }

        int i16 (size_t off) const
        {
            need (off, 2);
            return (int) (juce::int16) (bigEndian ? juce::ByteOrder::bigEndianShort (p + off) : juce::ByteOrder::littleEndianShort (p + off));
        }

        float f32 (size_t off) const
        {
            const auto bits = u32 (off);
            float f;
            std::memcpy (&f, &bits, 4);
            return f;
        }

        /** A NUL-terminated string in a fixed field, cut short at the end of the record. */
        juce::String text (size_t off, size_t length) const
        {
            if (off >= n)
                return {};
            length = std::min (length, n - off);
            const auto* start = reinterpret_cast<const char*> (p + off);
            const auto* end = static_cast<const char*> (std::memchr (start, 0, length));
            const size_t len = end != nullptr ? (size_t) (end - start) : length;
            if (juce::CharPointer_UTF8::isValidString (start, (int) len))
                return juce::String::fromUTF8 (start, (int) len);
            return juce::String::createStringFromData (start, (int) len);
        }
    };
}

double DSEXS24::envelopeMs (int value)
{
    const double v = juce::jlimit (0, 127, value) / 127.0;
    return 10000.0 * v * v * v * v;
}

double DSEXS24::sustainLevel (int value)
{
    const double v = juce::jlimit (0, 127, value) / 127.0;
    return v * v;
}

bool DSEXS24::loadExs(juce::File file) {
    juce::MemoryBlock data;
    if (!file.existsAsFile() || file.getSize() > (64 << 20) || !file.loadFileAsData(data)) {
        DBG("Unable to open file <<" << file.getFullPathName() << ">>");
        error = "not_exs";
        return false;
    }
    return loadExsData(data.getData(), data.getSize());
}

bool DSEXS24::loadExsData(const void* rawData, size_t size) {
    zones.clear();
    groups.clear();
    samples.clear();
    sequences.clear();
    parameters.clear();
    settings = {};
    name = {};
    error = {};

    const auto* data = static_cast<const juce::uint8*> (rawData);

    if (size < recordHeader) {
        error = "not_exs";
        return false;
    }

    const auto magicIs = [data] (const char* m) { return std::memcmp (data + 16, m, 4) == 0; };
    if (magicIs ("TBOS") || magicIs ("JBOS")) {
        bigEndian = false;
    } else if (magicIs ("SOBT") || magicIs ("SOBJ")) {
        bigEndian = true;
    } else {
        error = "not_exs";
        return false;
    }

    try {
        size_t i = 0, count = 0;
        while (i + recordHeader <= size) {
            const Record header { data + i, recordHeader, bigEndian };
            const auto sig = header.u32 (0);
            size_t bodySize = header.u32 (4);
            const int type = (int) ((sig >> 24) & 0x0F);
            const int flags = (int) ((sig >> 24) & 0xF0);
            if ((flags & flagBigSize) != 0 && bodySize >= 0x8000) {
                bodySize -= 0x8000;
            }
            if (bodySize > size - i - recordHeader || ++count > maxRecords) {
                throw Corrupt();
            }

            const Record r { data + i, recordHeader + bodySize, bigEndian };

            if (type == typeInstrument && name.isEmpty()) {
                name = r.text (20, 64);
            } else if (type == typeZone) {
                DSEXS24Zone zone;
                zone.id = r.i32 (8);
                zone.name = r.text (20, 64);
                zone.flags = r.u8 (84);
                zone.oneShot = (zone.flags & 0x01) != 0;
                zone.pitch = (zone.flags & 0x02) == 0;
                zone.reverse = (zone.flags & 0x04) != 0;
                zone.velocityRangeOn = (zone.flags & 0x08) != 0;
                zone.key = (short) r.u8 (85);
                zone.fineTuning = (short) r.i8 (86);
                zone.pan = (short) r.i8 (87);
                zone.volume = (float) r.i8 (88);
                zone.keyLow = (short) r.u8 (90);
                zone.keyHigh = (short) r.u8 (91);
                zone.loVel = (short) r.u8 (93);
                zone.hiVel = (short) r.u8 (94);
                zone.sampleStart = (int) r.u32 (96);
                zone.sampleEnd = (int) r.u32 (100);
                zone.loopStart = (int) r.u32 (104);
                zone.loopEnd = (int) r.u32 (108);
                zone.loopCrossfadeMilliseconds = (int) r.u32 (112);
                zone.loopTune = (short) r.i8 (116);
                const int loopFlags = r.u8 (117);
                zone.loopEnabled = (loopFlags & 0x01) != 0;
                zone.loopEqualPower = (loopFlags & 0x02) != 0;
                zone.coarseTuning = (short) r.i8 (164);
                zone.groupIndex = r.i32 (172);
                zone.sampleIndex = r.i32 (176);
                zone.sampleFade = r.n >= 192 ? r.i32 (188) : 0;
                zone.offset = r.n >= 196 ? r.i32 (192) : 0;
                if (r.n >= 212) {
                    // the precise volume, written by newer Logic alongside the 8-bit one
                    const float v = r.f32 (208);
                    if (std::isfinite (v) && std::abs (v) < 200.0f) {
                        zone.volume = v;
                    }
                }
                zones.add (zone);
            } else if (type == typeGroup) {
                DSEXS24Group group;
                group.name = r.text (20, 64);
                group.volume = (short) r.i8 (84);
                group.pan = (short) r.i8 (85);
                group.polyphony = (short) r.u8 (86);
                group.velRangeLow = (short) r.u8 (89);
                group.velRangeHigh = (short) r.u8 (90);
                group.trigger = (short) (r.n > 157 ? r.u8 (157) : 0);
                group.output = (short) (r.n > 158 ? r.u8 (158) : 0);
                group.exsSequence = r.n >= 168 ? r.i32 (164) : -1;
                group.sequenceMode = r.n > 168 ? r.u8 (168) : 0;
                groups.add (group);
            } else if (type == typeSample) {
                DSEXS24Sample sample;
                sample.id = r.i32 (8);
                sample.name = r.text (20, 64);
                sample.length = (int) r.u32 (88);
                sample.sampleRate = r.i32 (92);
                sample.bitDepth = (short) r.u8 (96);
                sample.type = r.n >= 116 ? r.i32 (112) : 0;
                sample.filePath = r.text (164, 256);
                sample.fileName = r.n > 420 ? r.text (420, 256) : juce::String();
                if (sample.fileName.isEmpty()) {
                    sample.fileName = sample.name;
                }
                samples.add (sample);
            } else if (type == typeParameters && parameters.empty()) {
                const size_t n = r.u32 (recordHeader);
                if (n > (r.n - recordHeader - 4) / 3) {
                    throw Corrupt();
                }
                for (size_t k = 0; k < n; ++k) {
                    parameters[r.u8 (recordHeader + 4 + k)] = r.i16 (recordHeader + 4 + n + 2 * k);
                }
            }

            i += recordHeader + bodySize;
        }
    } catch (const Corrupt&) {
        zones.clear();
        groups.clear();
        samples.clear();
        parameters.clear();
        error = "corrupt";
        return false;
    }

    // The mapped parameters, in plain units
    const auto get = [this] (int id) -> std::optional<int> {
        auto it = parameters.find (id);
        return it != parameters.end() ? std::optional<int> (it->second) : std::nullopt;
    };
    settings.volumeDb = get (pVolume);
    settings.transpose = get (pTranspose);
    settings.fineTune = get (pFineTune);
    settings.voices = get (pVoices);
    settings.bendUp = get (pBendUp);
    settings.bendDown = get (pBendDown);
    settings.glideRaw = get (pGlide);
    settings.filterType = get (pFilterType);
    if (auto v = get (pAmpAttackHigh)) settings.ampAttackMs = envelopeMs (*v);
    if (auto v = get (pAmpDecay)) settings.ampDecayMs = envelopeMs (*v);
    if (auto v = get (pAmpSustain)) settings.ampSustain = sustainLevel (*v);
    if (auto v = get (pAmpRelease)) settings.ampReleaseMs = envelopeMs (*v);
    if (auto v = get (pFilterOn)) settings.filterOn = *v != 0;
    if (auto v = get (pFilterCutoff)) settings.filterCutoff = juce::jlimit (0, 1000, *v) / 1000.0;
    if (auto v = get (pFilterResonance)) settings.filterResonance = juce::jlimit (0, 1000, *v) / 1000.0;

    readSequences();
    convertSeqNumbers();

    return true;
}

void DSEXS24::convertSeqNumbers() {
    for (int groupIndex = 0; groupIndex < groups.size(); groupIndex++) {
        DSEXS24Group &group = groups.getReference(groupIndex);
        group.seqNumber = 0;
        group.seqLength = 0;
        group.seqChain = -1;
        for (int chain = 0; chain < sequences.size(); chain++) {
            const juce::Array<int>& sequence = sequences.getReference(chain);
            if(sequence.contains(groupIndex)) {
                group.seqNumber = sequence.indexOf(groupIndex) + 1;
                group.seqLength = sequence.size();
                group.seqChain = chain;
            }
        }
    }
}

void DSEXS24::readSequences() {

    // exs handles round robin samples by using groups that point to the next group, and so on
    //    until the sequence is reset by pointing to group -1;
    //    here we trace each of those chains for simple processing later

    sequences.clear();

    // Only round-robin members (mode 2) chain: groups in other modes carry stale "play after" values
    const auto inRoundRobin = [this] (int g) { return g >= 0 && g < groups.size() && groups.getReference (g).sequenceMode == 2; };

    for (int groupIndex = 0; groupIndex < groups.size(); groupIndex++) {
        if (!inRoundRobin(groupIndex)) {
            continue;
        }
        bool foundInSequence = false;
        for (const juce::Array<int>& sequence : sequences) {
            if(sequence.contains(groupIndex)) {
                foundInSequence = true;
                break;
            }
        }
        if(foundInSequence) {
            continue;
        }

        // trace back to the first group in the chain by looking for a group that points to this chain,
        // and repeating the process until we end up at a group that's not pointed to
        int gid = groupIndex;
        juce::Array<int> visited;
        bool cont = true;
        while (cont) {
            cont = false;
            for(int currentGroupIndex = 0; currentGroupIndex < groups.size(); currentGroupIndex++) {
                const DSEXS24Group& g = groups.getReference(currentGroupIndex);
                if (inRoundRobin(currentGroupIndex) && g.exsSequence == gid && currentGroupIndex != g.exsSequence && !visited.contains(gid)) {
                    visited.add(gid);
                    gid = currentGroupIndex;
                    cont = true;
                    break;
                }
            }
        }

        // now that we're at the start of the chain, simply follow it to the end
        juce::Array<int> sequence;
        while (inRoundRobin(gid) && !sequence.contains(gid)) {
            sequence.insert(0, gid);
            gid = groups.getReference(gid).exsSequence;
        }

        if (sequence.size() > 1) {
            sequences.add(sequence);
        }
    }
}
