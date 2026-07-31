/*
  ==============================================================================

    DSSF2.h
    Created: 30 Jul 2026
    Author:  GitHub Copilot

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <map>
#include <vector>
#include "DSPresetConverter.h"

struct DSSF2PresetRecord {
    juce::String name;
    uint16_t preset = 0;
    uint16_t bank = 0;
    uint16_t bagIndex = 0;
};

struct DSSF2BagRecord {
    uint16_t genIndex = 0;
};

struct DSSF2GenRecord {
    uint16_t oper = 0;
    uint16_t amount = 0;
};

struct DSSF2InstRecord {
    juce::String name;
    uint16_t bagIndex = 0;
};

struct DSSF2SampleRecord {
    juce::String name;
    uint32_t start = 0;
    uint32_t end = 0;
    uint32_t startLoop = 0;
    uint32_t endLoop = 0;
    uint32_t sampleRate = 44100;
    uint8_t originalPitch = 60;
    int8_t pitchCorrection = 0;
};

class DSSF2ImporterCore {
public:
    bool getPresetRecords(const juce::File& sf2File, std::vector<DSSF2PresetRecord>& recordsOut, juce::String& errorOut) {
        recordsOut.clear();
        errorOut = {};

        juce::MemoryBlock fileData;
        if (!sf2File.loadFileAsData(fileData)) {
            errorOut = "Unable to read SF2 file from disk.";
            return false;
        }

        const auto* bytes = static_cast<const uint8_t*>(fileData.getData());
        const size_t totalSize = static_cast<size_t>(fileData.getSize());
        if (totalSize < 12 || readTag(bytes) != "RIFF" || readTag(bytes + 8) != "sfbk") {
            errorOut = "Invalid SF2 file header.";
            return false;
        }

        juce::MemoryBlock pdtaPhdr;
        size_t pos = 12;
        while (pos + 8 <= totalSize) {
            const juce::String chunkId = readTag(bytes + pos);
            const uint32_t chunkSize = readU32LE(bytes + pos + 4);
            const size_t chunkDataStart = pos + 8;
            if (chunkDataStart + chunkSize > totalSize) {
                break;
            }

            if (chunkId == "LIST" && chunkSize >= 4) {
                const juce::String listType = readTag(bytes + chunkDataStart);
                if (listType == "pdta") {
                    size_t subPos = chunkDataStart + 4;
                    const size_t subEnd = chunkDataStart + chunkSize;
                    while (subPos + 8 <= subEnd) {
                        const juce::String subId = readTag(bytes + subPos);
                        const uint32_t subSize = readU32LE(bytes + subPos + 4);
                        const size_t subDataStart = subPos + 8;
                        if (subDataStart + subSize > subEnd) {
                            break;
                        }

                        if (subId == "phdr") {
                            pdtaPhdr.append(bytes + subDataStart, subSize);
                        }

                        subPos = subDataStart + subSize + (subSize & 1u);
                    }
                }
            }

            pos = chunkDataStart + chunkSize + (chunkSize & 1u);
        }

        recordsOut = parsePresetRecords(pdtaPhdr);

        if (recordsOut.empty()) {
            errorOut = "No presets were found in this SF2 file.";
            return false;
        }

        return true;
    }

    bool getPresetNames(const juce::File& sf2File, juce::StringArray& namesOut, juce::String& errorOut) {
        namesOut.clear();
        std::vector<DSSF2PresetRecord> records;
        if (!getPresetRecords(sf2File, records, errorOut)) {
            return false;
        }

        for (const auto& preset : records) {
            juce::String name = preset.name.isNotEmpty() ? preset.name : "Untitled Preset";
            namesOut.add(name + " (Bank " + juce::String(static_cast<int>(preset.bank)) + ", Program " + juce::String(static_cast<int>(preset.preset)) + ")");
        }

        return true;
    }

    bool parseAndExtractFirstPreset(const juce::File& sf2File, DSSF2Preset& presetOut, juce::String& errorOut) {
        return parseAndExtractPresetByIndex(sf2File, 0, presetOut, errorOut);
    }

    bool parseAndExtractFirstPreset(const juce::File& sf2File,
                                    const juce::File& extractedSamplesDirectory,
                                    DSSF2Preset& presetOut,
                                    juce::String& errorOut) {
        return parseAndExtractPresetByIndex(sf2File, 0, extractedSamplesDirectory, presetOut, errorOut);
    }

    bool parseAndExtractPresetByIndex(const juce::File& sf2File, int presetIndex, DSSF2Preset& presetOut, juce::String& errorOut) {
        return parseAndExtractPresetByIndex(sf2File,
                                            presetIndex,
                                            sf2File.getParentDirectory().getChildFile("Samples").getChildFile(sf2File.getFileNameWithoutExtension() + "_sf2"),
                                            presetOut,
                                            errorOut);
    }

    bool parseAndExtractPresetByIndex(const juce::File& sf2File,
                                      int presetIndex,
                                      const juce::File& extractedSamplesDirectory,
                                      DSSF2Preset& presetOut,
                                      juce::String& errorOut) {
        errorOut = {};
        presetOut.name = {};
        presetOut.regions.clear();

        juce::MemoryBlock fileData;
        if (!sf2File.loadFileAsData(fileData)) {
            errorOut = "Unable to read SF2 file from disk.";
            return false;
        }

        const auto* bytes = static_cast<const uint8_t*>(fileData.getData());
        const size_t totalSize = static_cast<size_t>(fileData.getSize());
        if (totalSize < 12 || readTag(bytes) != "RIFF" || readTag(bytes + 8) != "sfbk") {
            errorOut = "Invalid SF2 file header.";
            return false;
        }

        juce::MemoryBlock pdtaPhdr;
        juce::MemoryBlock pdtaPbag;
        juce::MemoryBlock pdtaPgen;
        juce::MemoryBlock pdtaInst;
        juce::MemoryBlock pdtaIbag;
        juce::MemoryBlock pdtaIgen;
        juce::MemoryBlock pdtaShdr;
        juce::MemoryBlock sdtaSmpl;

        size_t pos = 12;
        while (pos + 8 <= totalSize) {
            const juce::String chunkId = readTag(bytes + pos);
            const uint32_t chunkSize = readU32LE(bytes + pos + 4);
            const size_t chunkDataStart = pos + 8;
            if (chunkDataStart + chunkSize > totalSize) {
                break;
            }

            if (chunkId == "LIST" && chunkSize >= 4) {
                const juce::String listType = readTag(bytes + chunkDataStart);
                size_t subPos = chunkDataStart + 4;
                const size_t subEnd = chunkDataStart + chunkSize;
                while (subPos + 8 <= subEnd) {
                    const juce::String subId = readTag(bytes + subPos);
                    const uint32_t subSize = readU32LE(bytes + subPos + 4);
                    const size_t subDataStart = subPos + 8;
                    if (subDataStart + subSize > subEnd) {
                        break;
                    }

                    if (listType == "pdta") {
                        copyChunk(subId, bytes + subDataStart, subSize, pdtaPhdr, pdtaPbag, pdtaPgen, pdtaInst, pdtaIbag, pdtaIgen, pdtaShdr);
                    } else if (listType == "sdta") {
                        if (subId == "smpl") {
                            sdtaSmpl.append(bytes + subDataStart, subSize);
                        }
                    }

                    subPos = subDataStart + subSize + (subSize & 1u);
                }
            }

            pos = chunkDataStart + chunkSize + (chunkSize & 1u);
        }

        if (pdtaPhdr.getSize() < 76 || pdtaPbag.getSize() < 8 || pdtaPgen.getSize() < 4 ||
            pdtaInst.getSize() < 44 || pdtaIbag.getSize() < 8 || pdtaIgen.getSize() < 4 ||
            pdtaShdr.getSize() < 92 || sdtaSmpl.getSize() < 4) {
            errorOut = "SF2 is missing required pdta/sdta chunks.";
            return false;
        }

        const auto presets = parsePresetRecords(pdtaPhdr);
        const auto pBags = parseBagRecords(pdtaPbag);
        const auto pGens = parseGenRecords(pdtaPgen);
        const auto insts = parseInstRecords(pdtaInst);
        const auto iBags = parseBagRecords(pdtaIbag);
        const auto iGens = parseGenRecords(pdtaIgen);
        const auto samples = parseSampleRecords(pdtaShdr);
        const auto pcmData = parseSmplPcm(sdtaSmpl);

        if (presets.empty() || insts.empty() || samples.empty() || pcmData.empty()) {
            errorOut = "SF2 file has no usable presets/instruments/samples.";
            return false;
        }

        const int clampedPresetIndex = juce::jlimit(0, static_cast<int>(presets.size()) - 1, presetIndex);
        const DSSF2PresetRecord& selectedPreset = presets[static_cast<size_t>(clampedPresetIndex)];
        presetOut.name = selectedPreset.name;

        juce::File outputDirectory = extractedSamplesDirectory;
        outputDirectory.createDirectory();

        std::map<int, juce::String> samplePaths;
        std::map<int, int> sampleLengths;
        for (int sampleId = 0; sampleId < static_cast<int>(samples.size()); sampleId++) {
            const auto& s = samples[static_cast<size_t>(sampleId)];
            if (s.end <= s.start || s.end > pcmData.size()) {
                continue;
            }

            juce::String fileNameBase = sanitizeFileName(s.name) + "_" + juce::String(sampleId);
            juce::File outFile = outputDirectory.getChildFile(fileNameBase + ".wav");
            if ((outFile.existsAsFile() && outFile.getSize() > 0) || writeWavMono16(outFile, pcmData, s.start, s.end, s.sampleRate)) {
                samplePaths[sampleId] = outFile.getFullPathName();
                sampleLengths[sampleId] = static_cast<int>(s.end - s.start);
            }
        }

        if (samplePaths.empty()) {
            errorOut = "No usable sample audio could be extracted from SF2.";
            return false;
        }

        const int presetStartBag = selectedPreset.bagIndex;
        const int presetEndBag = (clampedPresetIndex + 1 < static_cast<int>(presets.size()))
            ? presets[static_cast<size_t>(clampedPresetIndex + 1)].bagIndex
            : static_cast<int>(pBags.size()) - 1;
        std::map<int, uint16_t> presetGlobalGens;

        struct PresetZone {
            int instrumentId = -1;
            std::map<int, uint16_t> gens;
        };
        std::vector<PresetZone> presetZones;

        for (int bagIdx = presetStartBag; bagIdx < presetEndBag; bagIdx++) {
            const int genStart = pBags[static_cast<size_t>(bagIdx)].genIndex;
            const int genEnd = (bagIdx + 1 < static_cast<int>(pBags.size())) ? pBags[static_cast<size_t>(bagIdx + 1)].genIndex : static_cast<int>(pGens.size());
            std::map<int, uint16_t> gens;
            int instrumentId = -1;
            for (int gi = genStart; gi < genEnd; gi++) {
                const auto& gen = pGens[static_cast<size_t>(gi)];
                gens[gen.oper] = gen.amount;
                if (gen.oper == 41) {
                    instrumentId = gen.amount;
                }
            }

            if (instrumentId < 0) {
                for (const auto& g : gens) {
                    presetGlobalGens[g.first] = g.second;
                }
            } else {
                presetZones.push_back({ instrumentId, gens });
            }
        }

        for (const auto& pz : presetZones) {
            if (pz.instrumentId < 0 || pz.instrumentId >= static_cast<int>(insts.size())) {
                continue;
            }

            const auto& inst = insts[static_cast<size_t>(pz.instrumentId)];
            const int instStartBag = inst.bagIndex;
            const int instEndBag = (pz.instrumentId + 1 < static_cast<int>(insts.size()))
                ? insts[static_cast<size_t>(pz.instrumentId + 1)].bagIndex
                : static_cast<int>(iBags.size()) - 1;

            std::map<int, uint16_t> instGlobalGens;
            struct InstZone {
                int sampleId = -1;
                std::map<int, uint16_t> gens;
            };
            std::vector<InstZone> instZones;

            for (int bagIdx = instStartBag; bagIdx < instEndBag; bagIdx++) {
                const int genStart = iBags[static_cast<size_t>(bagIdx)].genIndex;
                const int genEnd = (bagIdx + 1 < static_cast<int>(iBags.size())) ? iBags[static_cast<size_t>(bagIdx + 1)].genIndex : static_cast<int>(iGens.size());
                std::map<int, uint16_t> gens;
                int sampleId = -1;
                for (int gi = genStart; gi < genEnd; gi++) {
                    const auto& gen = iGens[static_cast<size_t>(gi)];
                    gens[gen.oper] = gen.amount;
                    if (gen.oper == 53) {
                        sampleId = gen.amount;
                    }
                }

                if (sampleId < 0) {
                    for (const auto& g : gens) {
                        instGlobalGens[g.first] = g.second;
                    }
                } else {
                    instZones.push_back({ sampleId, gens });
                }
            }

            for (const auto& iz : instZones) {
                if (samplePaths.find(iz.sampleId) == samplePaths.end()) {
                    continue;
                }

                const auto mergedPreset = mergeGens(presetGlobalGens, pz.gens);
                const auto mergedInst = mergeGens(instGlobalGens, iz.gens);

                DSSF2Region region;
                region.name = samples[static_cast<size_t>(iz.sampleId)].name;
                region.samplePath = samplePaths[iz.sampleId];

                auto keyRange = intersectRanges(getRange(mergedPreset, 43), getRange(mergedInst, 43));
                auto velRange = intersectRanges(getRange(mergedPreset, 44), getRange(mergedInst, 44));
                if (keyRange.first > keyRange.second || velRange.first > velRange.second) {
                    continue;
                }

                region.loNote = keyRange.first;
                region.hiNote = keyRange.second;
                region.loVel = velRange.first;
                region.hiVel = velRange.second;

                const auto& sampleRec = samples[static_cast<size_t>(iz.sampleId)];
                region.rootNote = sampleRec.originalPitch;
                if (hasGen(mergedInst, 58)) {
                    region.rootNote = static_cast<int>(getU8Amount(mergedInst, 58));
                }

                int coarseTune = getSignedAmount(mergedPreset, 51) + getSignedAmount(mergedInst, 51);
                int fineTune = getSignedAmount(mergedPreset, 52) + getSignedAmount(mergedInst, 52);
                region.tuning = (sampleRec.pitchCorrection / 100.0f) + static_cast<float>(coarseTune) + (static_cast<float>(fineTune) / 100.0f);

                float pan = static_cast<float>(getSignedAmount(mergedPreset, 17) + getSignedAmount(mergedInst, 17)) / 10.0f;
                region.pan = juce::jlimit(-100.0f, 100.0f, pan);

                float attenuationDb = static_cast<float>(getSignedAmount(mergedPreset, 48) + getSignedAmount(mergedInst, 48)) / 10.0f;
                region.volumeDb = -attenuationDb;

                if (hasGen(mergedInst, 56) && getSignedAmount(mergedInst, 56) == 0) {
                    region.pitchKeyTrack = false;
                }

                int startOffset = getSignedAmount(mergedInst, 0) + getSignedAmount(mergedInst, 4) * 32768;
                int endOffset = getSignedAmount(mergedInst, 1) + getSignedAmount(mergedInst, 12) * 32768;
                int sampleLength = sampleLengths[iz.sampleId];
                region.start = juce::jlimit(0, juce::jmax(0, sampleLength - 1), startOffset);
                region.end = juce::jlimit(region.start, juce::jmax(region.start, sampleLength - 1), sampleLength - 1 + endOffset);

                int sampleMode = getSignedAmount(mergedInst, 54);
                bool loopEnabled = (sampleMode & 1) != 0;
                if (loopEnabled && sampleRec.endLoop > sampleRec.startLoop && sampleRec.startLoop >= sampleRec.start) {
                    int loopStart = static_cast<int>(sampleRec.startLoop - sampleRec.start);
                    int loopEnd = static_cast<int>(sampleRec.endLoop - sampleRec.start - 1);
                    loopStart = juce::jlimit(region.start, region.end, loopStart);
                    loopEnd = juce::jlimit(loopStart + 1, region.end, loopEnd);
                    region.loopEnabled = loopEnd > loopStart;
                    region.loopStart = loopStart;
                    region.loopEnd = loopEnd;
                }

                presetOut.regions.add(region);
            }
        }

        if (presetOut.regions.isEmpty()) {
            errorOut = "SF2 parsed, but no playable regions were produced.";
            return false;
        }

        return true;
    }

private:
    static juce::String readTag(const uint8_t* ptr) {
        return juce::String::fromUTF8(reinterpret_cast<const char*>(ptr), 4);
    }

    static uint16_t readU16LE(const uint8_t* ptr) {
        return static_cast<uint16_t>(ptr[0] | (ptr[1] << 8));
    }

    static uint32_t readU32LE(const uint8_t* ptr) {
        return static_cast<uint32_t>(ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24));
    }

    static juce::String readPaddedName(const uint8_t* ptr, int length) {
        juce::String out;
        for (int i = 0; i < length; i++) {
            if (ptr[i] == 0) {
                break;
            }
            out << static_cast<juce::juce_wchar>(ptr[i]);
        }
        return out.trim();
    }

    static void copyChunk(const juce::String& id,
                          const uint8_t* src,
                          uint32_t size,
                          juce::MemoryBlock& phdr,
                          juce::MemoryBlock& pbag,
                          juce::MemoryBlock& pgen,
                          juce::MemoryBlock& inst,
                          juce::MemoryBlock& ibag,
                          juce::MemoryBlock& igen,
                          juce::MemoryBlock& shdr) {
        if (id == "phdr") {
            phdr.append(src, size);
        } else if (id == "pbag") {
            pbag.append(src, size);
        } else if (id == "pgen") {
            pgen.append(src, size);
        } else if (id == "inst") {
            inst.append(src, size);
        } else if (id == "ibag") {
            ibag.append(src, size);
        } else if (id == "igen") {
            igen.append(src, size);
        } else if (id == "shdr") {
            shdr.append(src, size);
        }
    }

    static std::vector<DSSF2PresetRecord> parsePresetRecords(const juce::MemoryBlock& block) {
        std::vector<DSSF2PresetRecord> out;
        const auto* bytes = static_cast<const uint8_t*>(block.getData());
        const size_t total = static_cast<size_t>(block.getSize());
        for (size_t i = 0; i + 38 <= total; i += 38) {
            DSSF2PresetRecord rec;
            rec.name = readPaddedName(bytes + i, 20);
            rec.preset = readU16LE(bytes + i + 20);
            rec.bank = readU16LE(bytes + i + 22);
            rec.bagIndex = readU16LE(bytes + i + 24);
            out.push_back(rec);
        }
        if (!out.empty()) {
            out.pop_back();
        }
        return out;
    }

    static std::vector<DSSF2BagRecord> parseBagRecords(const juce::MemoryBlock& block) {
        std::vector<DSSF2BagRecord> out;
        const auto* bytes = static_cast<const uint8_t*>(block.getData());
        const size_t total = static_cast<size_t>(block.getSize());
        for (size_t i = 0; i + 4 <= total; i += 4) {
            DSSF2BagRecord rec;
            rec.genIndex = readU16LE(bytes + i);
            out.push_back(rec);
        }
        return out;
    }

    static std::vector<DSSF2GenRecord> parseGenRecords(const juce::MemoryBlock& block) {
        std::vector<DSSF2GenRecord> out;
        const auto* bytes = static_cast<const uint8_t*>(block.getData());
        const size_t total = static_cast<size_t>(block.getSize());
        for (size_t i = 0; i + 4 <= total; i += 4) {
            DSSF2GenRecord rec;
            rec.oper = readU16LE(bytes + i);
            rec.amount = readU16LE(bytes + i + 2);
            out.push_back(rec);
        }
        return out;
    }

    static std::vector<DSSF2InstRecord> parseInstRecords(const juce::MemoryBlock& block) {
        std::vector<DSSF2InstRecord> out;
        const auto* bytes = static_cast<const uint8_t*>(block.getData());
        const size_t total = static_cast<size_t>(block.getSize());
        for (size_t i = 0; i + 22 <= total; i += 22) {
            DSSF2InstRecord rec;
            rec.name = readPaddedName(bytes + i, 20);
            rec.bagIndex = readU16LE(bytes + i + 20);
            out.push_back(rec);
        }
        if (!out.empty()) {
            out.pop_back();
        }
        return out;
    }

    static std::vector<DSSF2SampleRecord> parseSampleRecords(const juce::MemoryBlock& block) {
        std::vector<DSSF2SampleRecord> out;
        const auto* bytes = static_cast<const uint8_t*>(block.getData());
        const size_t total = static_cast<size_t>(block.getSize());
        for (size_t i = 0; i + 46 <= total; i += 46) {
            DSSF2SampleRecord rec;
            rec.name = readPaddedName(bytes + i, 20);
            rec.start = readU32LE(bytes + i + 20);
            rec.end = readU32LE(bytes + i + 24);
            rec.startLoop = readU32LE(bytes + i + 28);
            rec.endLoop = readU32LE(bytes + i + 32);
            rec.sampleRate = readU32LE(bytes + i + 36);
            rec.originalPitch = bytes[i + 40];
            rec.pitchCorrection = static_cast<int8_t>(bytes[i + 41]);
            out.push_back(rec);
        }
        if (!out.empty()) {
            out.pop_back();
        }
        return out;
    }

    static std::vector<int16_t> parseSmplPcm(const juce::MemoryBlock& block) {
        std::vector<int16_t> pcm;
        const auto* bytes = static_cast<const uint8_t*>(block.getData());
        const size_t total = static_cast<size_t>(block.getSize());
        pcm.reserve(total / 2);
        for (size_t i = 0; i + 2 <= total; i += 2) {
            pcm.push_back(static_cast<int16_t>(readU16LE(bytes + i)));
        }
        return pcm;
    }

    static std::map<int, uint16_t> mergeGens(const std::map<int, uint16_t>& a, const std::map<int, uint16_t>& b) {
        auto out = a;
        for (const auto& kv : b) {
            out[kv.first] = kv.second;
        }
        return out;
    }

    static bool hasGen(const std::map<int, uint16_t>& gens, int oper) {
        return gens.find(oper) != gens.end();
    }

    static int16_t getSignedAmount(const std::map<int, uint16_t>& gens, int oper) {
        const auto it = gens.find(oper);
        if (it == gens.end()) {
            return 0;
        }
        return static_cast<int16_t>(it->second);
    }

    static uint8_t getU8Amount(const std::map<int, uint16_t>& gens, int oper) {
        const auto it = gens.find(oper);
        if (it == gens.end()) {
            return 0;
        }
        return static_cast<uint8_t>(it->second & 0xFF);
    }

    static std::pair<int, int> getRange(const std::map<int, uint16_t>& gens, int oper) {
        const auto it = gens.find(oper);
        if (it == gens.end()) {
            return { 0, 127 };
        }
        int low = static_cast<int>(it->second & 0xFF);
        int high = static_cast<int>((it->second >> 8) & 0xFF);
        return { juce::jlimit(0, 127, low), juce::jlimit(0, 127, high) };
    }

    static std::pair<int, int> intersectRanges(const std::pair<int, int>& a, const std::pair<int, int>& b) {
        return { juce::jmax(a.first, b.first), juce::jmin(a.second, b.second) };
    }

    static juce::String sanitizeFileName(const juce::String& input) {
        juce::String out = input.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_ ");
        out = out.trim().replace(" ", "_");
        if (out.isEmpty()) {
            out = "sample";
        }
        return out;
    }

    static bool writeWavMono16(const juce::File& outputFile,
                               const std::vector<int16_t>& pcm,
                               uint32_t start,
                               uint32_t end,
                               uint32_t sampleRate) {
        if (end <= start || end > pcm.size()) {
            return false;
        }

        const int numSamples = static_cast<int>(end - start);
        juce::AudioBuffer<float> buffer(1, numSamples);
        for (int i = 0; i < numSamples; i++) {
            buffer.setSample(0, i, static_cast<float>(pcm[static_cast<size_t>(start) + static_cast<size_t>(i)]) / 32768.0f);
        }

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::OutputStream> outputStream = std::make_unique<juce::FileOutputStream>(outputFile);
        juce::AudioFormatWriterOptions writerOptions = juce::AudioFormatWriterOptions{}
            .withSampleRate(sampleRate > 0 ? sampleRate : 44100)
            .withNumChannels(1)
            .withBitsPerSample(16);
        std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(outputStream, writerOptions));
        if (writer == nullptr) {
            return false;
        }

        writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
        writer->flush();
        return true;
    }
};
