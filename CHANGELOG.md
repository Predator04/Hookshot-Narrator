# Changelog

All notable changes to Longshot-Narrator. Dates are ISO (YYYY-MM-DD). This project follows
[Semantic Versioning](https://semver.org/) loosely: the major number changes when the add-on targets a different
version of the host project, because an existing install will not carry over.

## [2.0.1] — 2026-11-14

### Changed

- The project refers only to Longshot throughout — README, scripts and patch.
- `install.ps1` takes `-Longshot`, with `-Source` and `-Game` accepted as aliases.
- `setup-voices.ps1` looks for `longshot.exe`.

### Removed

- A fallback lookup for an older renderer DLL name, unreachable on every supported version.

## [2.0.0] — 2026-11-14

The add-on now targets **Longshot v3.0.0-rc.3** rather than the older host project it began on. An existing
install does not carry over: uninstall from the old source, then install into Longshot.

### Added

- `README` section explaining how the add-on actually works, and a table of every file the patch touches.

### Changed

- Patch regenerated against Longshot v3.0.0-rc.3, verified to apply **and reverse** on a pristine checkout.
- The patch file is now named `longshot-narrator.patch`.
- `install.ps1` reports which version your source is actually on when the patch does not fit. It prefers a `v…`
  tag when doing so: Longshot also carries moving tags named `rc` and `nightly`, and `git describe` picks those.

## [1.2.0] — 2026-11-14

### Added

- **`setup-voices.ps1`** — fetches Piper and both voice models into the built game folder in one command. Skips
  anything already present and the right size, so an interrupted download is resumed by running it again.
  `-Check` reports what is missing without downloading.
- A downloadable release archive, so the add-on no longer has to be cloned.

### Fixed

- The manual voice instructions said to "unzip it into a folder named `piper`", which reads as though the archive's
  files sit at its root. They do not — the archive carries its own `piper` folder, so following that literally
  produced `piper\piper\piper.exe`.

## [1.1.0] — 2026-11-14

### Added

- **Glow colour** setting: Rainbow (default), Gold, Green, Blue, Purple or Pink. A fixed colour holds one hue and
  lets the pulse and breathing carry the movement, which is calmer over a long session.

### Changed

- **Press-next glow** now defaults to *Both* rather than *Arrow*. With the halo removed, *Arrow* alone marks reply
  menus only, so a page merely waiting to be turned would carry no cue — the one thing a player who cannot read
  needs most. An existing choice is kept.

### Fixed

- **The choice chevron appeared far too late.** It was armed on `list_said && !speaking`, but `list_said` is set the
  moment speech *starts*, so the chevron was held back for the whole spoken line. On a long line that was the
  character's speech, then Link reading the highlighted choice, then synthesis, before any cue appeared.
- **The halo around the dialogue box's next arrow is gone.** It sat on top of the text, and the arrow it marked is
  drawn by the game and already visible.

## [1.0.0] — 2026-11-13

First release.

### Added

- Every dialogue box read aloud, page by page, with expression.
- Character voices matched to each character's model and kept for them.
- Link's voice for reply choices; only the highlighted choice is read.
- A chevron over a reply menu, and a press-next glow round the screen's edge.
- Values the game fills into text are spoken as part of the line.
- Lines already heard are not repeated within a conversation; talking to someone again starts afresh.
- The game's own voice acting is detected and left alone.
- Settings under the game's **Insert → Audio** menu.

[2.0.1]: https://github.com/Predator04/Longshot-Narrator/releases/tag/v2.0.1
[2.0.0]: https://github.com/Predator04/Longshot-Narrator/releases/tag/v2.0.0
[1.2.0]: https://github.com/Predator04/Longshot-Narrator/releases/tag/v1.2.0
[1.1.0]: https://github.com/Predator04/Longshot-Narrator/releases/tag/v1.1.0
[1.0.0]: https://github.com/Predator04/Longshot-Narrator/releases/tag/v1.0.0
