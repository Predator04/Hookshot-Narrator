# Contributing

Thanks for looking. This is a small project with a specific purpose — reading the game's dialogue aloud for people
who can't read it — so the most useful contributions are usually fixes, voice-table entries, and ports to new
Longshot versions.

## Reporting a problem

Open an issue and include **`longshot_log.txt`** from beside the game, or at least the lines starting with `voice`.
The add-on logs every step it takes and every line it decided not to read, with the reason, so that file usually
contains the answer.

Please also say:

- which Longshot version you built (`git describe --tags` in your Longshot folder, or the version shown in the
  game's menu)
- which Longshot-Narrator release you installed
- whether the natural voice or the Windows voice is being used

## Working on it

The add-on is distributed as a patch, but it is developed as ordinary source. The loop is:

```powershell
# 1. a clean checkout of the supported version
git clone https://git.magicbean.dev/MagicBeanDev/Longshot
cd Longshot
git checkout v3.0.0-rc.3

# 2. apply the patch, then edit the files in place
powershell -ExecutionPolicy Bypass -File ..\install.ps1 -Longshot .

# 3. build and try it
python -I nx2/make_game.py --nup "C:\path\your-game.nup" --out "C:\Games\Longshot" --language en-US

# 4. regenerate the patch from your changes
git diff v3.0.0-rc.3 > ..\longshot-narrator.patch
```

Two things that will bite you:

- **Line endings.** `host/oot_voice.c` and `host/oot_voice_cast.h` are added by the patch and must be **LF**.
  CRLF copies produce a patch that applies but does not round-trip, so uninstall fails.
- **Only narrator changes belong in the patch.** If you develop in a tree that carries other local modifications,
  generate the patch from a tree that has the narrator and nothing else.

Before opening a pull request, check the patch still round-trips on a pristine checkout:

```powershell
git apply --check longshot-narrator.patch            # applies
git apply longshot-narrator.patch
git apply --reverse --check longshot-narrator.patch  # and reverses
```

`install.ps1 -Uninstall` must leave the tree spotless — that is the contract.

## Adding or changing a character voice

Voices are assigned by the character's model name in `host/oot_voice_cast.h`. To find a character's name, talk to
them with the game running and look in `longshot_log.txt` for the `voice: cast …` line; it lists the event's cast,
and the first character entry is the one used.

A character with no entry gets the narrator voice rather than silence, so the table does not need to be complete
to be useful.

## Porting to a new Longshot version

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#porting-to-a-new-longshot-version). The short version: apply with
`--3way`, expect adjacency conflicts, check the renderer DLL's name, and confirm in the log that the dialogue
structure is still being found before concluding anything else is broken.

## Style

Match what is there. The code favours plain C, comments that explain *why* rather than restating the line, and
measured facts over guesses — if a number was determined by observation, the comment says so and gives the number.

## Licence

By contributing you agree your work is licensed under the GPL-3.0, the same as the project and the same as
Longshot.
