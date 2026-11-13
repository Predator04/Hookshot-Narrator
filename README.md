# Hookshot-Narrator

**Read-aloud dialogue for [Hookshot](https://github.com/NotRexed/Hookshot)**: the PC version of *The Legend of Zelda:
Ocarina of Time* (Nintendo Switch 2) that you build from your own copy of the game.

Hookshot-Narrator is an add-on. It reads every dialogue box out loud with a natural voice, gives each character a
voice that fits them, and reads Link's reply choices in his own voice. It's made for players who can't read yet, and
for anyone who'd rather listen.

![The lodge guard's question with Link's choices, read aloud](docs/screenshots/choice-menu.webp)

> [!IMPORTANT]
> This repository has **no game files, no keys, no executable and no voice recordings**. You need Hookshot built from
> your own copy of the game. The voices are generated live on your PC.

## Features

- **Every dialogue box is read aloud** when it appears, page by page.
- **Character voices.** Each character gets a man's, woman's, boy's or girl's voice that matches their model, and keeps
  the same voice every time you talk to them.
- **Link's voice for the choices.** Reply menus ("About the lodge…", "Death Mountain?", "Bye.") are read in a dedicated
  Link voice. Moving the cursor reads the highlighted choice.
- **Expression.** Lines ending in `!` sound more excited, questions rise at the end, and `...` pauses.
- **Filled-in values.** Numbers the game fills in are read too ("Only **7** left to find!").
- **No repeats.** A question you just heard isn't read again when the conversation comes back to it.
- **Leaves the real voice acting alone.** Cutscenes the game voices itself are left to the actors.
- **Press-next glow.** When a page has been read, a soft rainbow glow pulses around the screen edge, so a player who
  can't read knows it's time to press for the next page.
- **Settings in the game's Insert menu**, under **Audio**.

![A number the game fills in, read as part of the line](docs/screenshots/filled-in-number.webp)

## Settings

Open the in-game menu (**Insert** or **F1**) and go to the **Audio** tab. Changes take effect immediately.

| Setting | Choices | Default | What it does |
|---|---|---|---|
| **Read aloud** | On / Off | On | Turns read-aloud on or off. |
| **Read-aloud voice** | Natural / Windows | Natural | *Natural* is the lifelike voice ([Piper](https://github.com/rhasspy/piper), runs on your PC). *Windows* is the voice built into Windows, used automatically if Piper isn't installed. |
| **Read-aloud speed** | 50% – 200% | 100% | How fast lines are read. |
| **Press-next glow** | On / Off | On | The rainbow edge glow once a page has been read. |
| **Link's voice** | Voice 1 – 6 | Voice 1 | Which voice reads Link's choices. Changing it plays a sample. |
| **Link's voice pitch** | Normal, +1 – +6 | +3 | How much higher Link's voice is, in semitones, so he sounds younger. Changing it plays a sample. |

The settings are saved in `hookshot_options.ini` with the rest of Hookshot's options. Starting the game with the
environment variable `HOS_VOICE=0` turns read-aloud off completely.

## Install

Hookshot-Narrator works on **Windows** with **Hookshot v2.1.0** built from source.

### 1. What you need

- Everything Hookshot's own [Build from source](https://github.com/NotRexed/Hookshot#build-from-source) needs
  (Python 3, llvm-mingw and the builder's tools), and your own copy of the game.
- [Git for Windows](https://git-scm.com).

### 2. Get Hookshot v2.1.0 and add Hookshot-Narrator

```powershell
git clone https://github.com/NotRexed/Hookshot
cd Hookshot
git checkout v2.1.0
```

Download this repository (**Code → Download ZIP**, then unzip it, or `git clone` it). Open PowerShell in its folder
and run:

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1 -Hookshot "C:\path\to\Hookshot"
```

It only changes Hookshot's source code. Nothing is downloaded, and no game files are touched. It tells you if
Hookshot-Narrator is already installed, or if your Hookshot version doesn't match.

### 3. Build Hookshot

Build it the way Hookshot's [Build from source](https://github.com/NotRexed/Hookshot#build-from-source) section
describes, for example:

```powershell
python -I nx2/make_game.py --nup "C:\path\your-game.nup" --out "C:\Games\Hookshot" --language en-US
```

### 4. Add the natural voices

Read-aloud works without this step by using the Windows voice. For the natural voices:

1. Download `piper_windows_amd64.zip` from [Piper's releases](https://github.com/rhasspy/piper/releases) (release
   2023.11.14-2). Unzip it into a folder named **`piper`** next to `hookshot.exe`, so that you have
   `C:\Games\Hookshot\piper\piper.exe`.
2. Download these two voices from [rhasspy/piper-voices](https://huggingface.co/rhasspy/piper-voices/tree/main/en/en_US),
   both the `.onnx` file and its `.onnx.json` file, into the same `piper` folder:

   | File | Used for | Size |
   |---|---|---|
   | `en_US-lessac-medium.onnx` + `.onnx.json` | the narrator (characters it doesn't know) | about 63 MB |
   | `en_US-libritts_r-medium.onnx` + `.onnx.json` | the character voices and Link's voice | about 79 MB |

   Check each voice's licence on its model card before you use it.

### 5. Play

Start the game and talk to anyone. The settings are under **Insert → Audio**.

> [!NOTE]
> The Hookshot Builder's **Update** button builds stock Hookshot again, which removes Hookshot-Narrator. To update,
> check out the new Hookshot version and run `install.ps1` again (when Hookshot-Narrator supports that version), then
> build.

## Using other voices, or your own

Any [Piper](https://github.com/rhasspy/piper) voice can be the narrator. Put its `.onnx` and `.onnx.json` files in
the `piper` folder and name them `en_US-lessac-medium.onnx` and `en_US-lessac-medium.onnx.json`, replacing the
originals. That voice then reads every character the add-on doesn't recognise. The character voices and Link's voice
come from the multi-speaker `en_US-libritts_r-medium` voice.

To use **your own voice**, record yourself and train a Piper voice from the recordings with Piper's
[training guide](https://github.com/rhasspy/piper/blob/master/TRAINING.md). Fine-tuning an existing voice needs far
less audio than training a new one. Then use it as above. Only train on a voice that's yours, or one whose owner
agreed.

## Uninstall

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1 -Hookshot "C:\path\to\Hookshot" -Uninstall
```

Then build Hookshot again. You can delete the `piper` folder next to `hookshot.exe`.

## If something goes wrong

- **No sound from read-aloud:** check that **Insert → Audio → Read aloud** is On. Every read-aloud step writes a line
  starting with `voice` to `hookshot_log.txt` next to `hookshot.exe`, including lines that couldn't be read and why.
- **The Windows voice instead of the natural one:** the log says `no piper\piper.exe beside the game` or
  `no piper\<voice file>`. Check step 4.
- **`install.ps1` says the patch doesn't fit:** your Hookshot isn't v2.1.0. Run `git checkout v2.1.0` in the Hookshot
  folder.

## How it works

Hookshot translates the game into a normal Windows program. Hookshot-Narrator reads the text of the dialogue box on
screen from the game's memory. It fills in the values the game inserts (numbers and names) and works out who is
speaking from the character's model. It then has Piper speak the line in a background process, so the game doesn't
slow down. When the game opens one of its own voiced lines, the add-on stays quiet. Most of the code is in
`host/oot_voice.c` (inside `hookshot-narrator.patch`); the other changes are small hooks and the menu rows.

## Credits and license

- [Hookshot](https://github.com/NotRexed/Hookshot) and its contributors. This add-on builds on their work.
- [Piper](https://github.com/rhasspy/piper) for the natural voices, and the authors of the voice models.

Hookshot-Narrator is licensed under the GPL-3.0, the same as Hookshot. See [LICENSE](LICENSE). It isn't affiliated
with Nintendo or with the Hookshot project.
