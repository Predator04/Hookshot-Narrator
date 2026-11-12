# Hookshot-Narrator

Hookshot-Narrator is a fork of [Hookshot](https://github.com/NotRexed/Hookshot) (v2.1.0). It adds read-aloud dialogue,
DualSense extras and a phone link. Everything else works the same as upstream Hookshot, and the same rules apply.
This repository contains no game files, keys, voice recordings or executables. You build it on your PC from your own
copy of the game.

## What it adds

### Read-aloud dialogue

- **Spoken dialogue.** The game's dialogue boxes are read aloud when they appear, either by a natural voice that runs
  on your PC ([Piper](https://github.com/rhasspy/piper)) or by the built-in Windows voice.
- **Character voices.** Each speaker gets a man, woman, boy or girl voice, chosen from the character's model, and keeps
  the same voice every time. Speakers it doesn't recognise use the default voice.
- **Voiced scenes stay as they are.** Lines the game already voices itself (the voiced cutscenes) aren't read again.
- **Press-next glow.** When a page has been read, a soft rainbow glow pulses around the edge of the screen until you
  press next.
- **Settings.** Insert menu → **Audio** has rows for Read aloud, Read-aloud voice (Natural or Windows), Read-aloud
  speed and Press-next glow.

### DualSense extras

These settings are in Insert menu → **Controls**.

- **Light bar** follows your hearts.
- **Hit rumble** when Link takes damage, with an optional heartbeat when hearts are low.
- **Player lights** (the five white LEDs) show magic or hearts, with a brightness setting, and can blink when magic
  is low.
- **Trigger resistance** on R2 while aiming in first person. It's experimental and off by default.

### Other

- **Phone link** (UDP port 43210). A phone companion can show hearts, rupees and the clock, and can use the phone's
  microphone to play the ocarina. The Android app isn't in this repository yet.
- **`--exefs-mods <folder>`** for `nx2/make_game.py` applies exefs `.ips` mods (`<mod>/exefs/*.ips`, matched by build
  id) while building. It was tested with *HUD of Time* and *Always-On Rupee Counter* from GameBanana, which you
  download from their own pages. They're not included here.
- **Upstream changes.** The fork also includes these upstream pull requests on top of v2.1.0: #1911, #2041, #1961 and
  #1946.

## Setting up the natural voice

The natural voice is optional. Without it, read-aloud uses the Windows voice.

1. Download the Windows build of [Piper](https://github.com/rhasspy/piper/releases) and extract it into a folder
   named `piper` next to `hookshot.exe`, so that `piper\piper.exe` exists.
2. Download these voices from [rhasspy/piper-voices](https://huggingface.co/rhasspy/piper-voices) and put both the
   `.onnx` file and its `.onnx.json` file for each into the same `piper` folder:
   - `en_US-lessac-medium` (the default voice)
   - `en_US-libritts_r-medium` (the multi-speaker voice used for character voices)

   Before you use a voice, check its licence on its model card.

## Building

Follow the upstream [Build from source](readme.md#build-from-source) steps. If you use exefs mods, also pass
`--exefs-mods <folder>`.

## Planned

- More expressive delivery for `!`, `?` and `...`.
- An optional "Generate voices" step that pre-renders every line on your own PC with a more expressive local model. It
  would only ever create files locally and would never download recorded audio.

## License

GPL-3.0, the same as Hookshot. See [LICENSE](LICENSE). All credit for Hookshot goes to its authors (see
[Credits](readme.md#credits)).
