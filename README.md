# DSPresetConverter

Shared converter core for turning EXS24, SFZ, and SoundFont (SF2) instruments into DecentSampler `.dspreset`/`.dsbundle` files. This is the canonical, consolidated version of code that previously existed as separate, drifting copies in DecentSampler, EXS2DS, EXS2ALL, SFZ2DS, and SF22DS.

## Status

All five consumers (DecentSampler, SF22DS, EXS2DS, EXS2ALL, SFZ2DS) now build against this repo as a git submodule instead of maintaining local copies.

- Features unique to EXS2ALL (`unifyDirectories`, `truncateLongFilenames`, `trimOnly`, `stripMissingImageReferences`, `_exsFilePath` tracking) have been ported forward into this canonical copy.
- The loop-crossfade blend math matches DecentSampler's live playback engine, which is the now the ground truth for crossfade behavior.

## Basic Structure

- `Source/DSEXS24.{h,cpp}` — EXS24 file reader.
- `Source/DSSFZ.{h,cpp}` — SFZ file reader.
- `Source/DSSF2.h` — SoundFont (SF2) reader/extractor (`DSSF2ImporterCore`).
- `Source/DSPresetConverter.{h,cpp}` — Converts parsed EXS24/SFZ/SF2 data into a DecentSampler `ValueTree`/XML, plus sample copy/loop-processing/path-rewrite utilities.