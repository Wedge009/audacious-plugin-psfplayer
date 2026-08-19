# audacious-plugin-psfplayer

An Audacious `InputPlugin` for PSF/PSF2 files, built on top of
[jpd002/Play-](https://github.com/jpd002/Play-)'s `PsfPlayer` core
(`PsfCore`) instead of the bundled
[OpenPSF decoder](https://github.com/audacious-media-player/audacious-plugins)
(`audacious-plugins/src/psf/`).

## Why

Audacious' existing OpenPSF plug-in hangs for ever on certain BIOS-dependent
titles (eg Final Fantasy X), due to a shared synchronous-softcall IOP dispatch
architecture. Play!'s more comprehensive HLE BIOS avoids that entire bug
family. Play! also carries a single, clean 2-clause BSD licence which means
this project can become something real users can actually install.

## Status

Working prototype, validated inside a real Audacious session.
`third_party/Play` is a git sub-module pinned to the commit the original
proof-of-concept evaluation was run against, with no local patches.

The `InputPlugin` (`src/plugin.cc`) is implemented, builds cleanly, and
has been validated end-to-end against real PSF2 files via a stand-alone test
harness that exercises the same code path `play()` does, without a live
Audacious host:

- `src/SoundQueue.*` — the bounded thread-safe queue bridging `CPsfVm`'s
  own push-model background thread to Audacious's pull-model `play()`
  loop. Back-pressure (`HasFreeBuffers()`) genuinely throttles emulation
  to real-time pace.
- `src/VmSession.*` — owns one `CPsfVm` per `play()` call. Deliberately
  does **not** copy `CPsfVmJs`'s mailbox-wrapped `LoadPsf()` pattern —
  `CMailBox::ReceiveCall()` has no exception handling at all, so a
  `SendCall()`-wrapped load that throws would crash the whole host
  process. Instead it uses `CPsfVm::Pause()`'s blocking mailbox guarantee
  to park the VM thread, then calls `LoadPsf()` directly on the caller's
  thread (matching the real `ui_qt` frontend's own sequencing), keeping
  exceptions catchable where a normal `try`/`catch` expects them.
- `src/PsfSetMaterializer.*` — `CPsfLoader::LoadPsf()`'s only public
  entry point always builds its own physical/archive stream provider
  internally; there's no way to inject a VFS-backed one without patching
  a private method. So this materialises the main file and everything
  its `_lib`/`_lib2..` chain transitively references (resolved the same
  way `CPsfBase` itself reads it) into a real temp directory via
  `VFSFile`, then hands that off to the stock physical loader unmodified.
  Cleaned up via RAII, including on load failure.
- `src/PlaybackTimer.*` — the `CPlaybackController` length/fade/
  end-of-track policy re-expressed against elapsed sample count instead
  of emulated video-frame ticks, since Audacious's own clock is
  sample-based.

Known limitations, not yet addressed:
- No real seek-to-time (no PSF engine here exposes one) — a forwards seek
  keeps running from wherever playback already is and fast-forwards
  (discarding audio) to the new target; a backwards seek restarts from
  zero first. Matches the other plug-ins' own approach. Backwards seeking
  far into a long track is inherently slow (bound by decode speed,
  measured at roughly 17x real-time) — a save-state-based seek was
  considered and deliberately not pursued (arbitrary seek targets mean
  diminishing returns for the added complexity).
- PSP support and `.rar`-archived PSF sets are out of scope for now (see
  `src/plugin.cc`'s and `PsfSetMaterializer.h`'s own comments).
- The hard-coded 44.1kHz output rate and RAR-archive support in Play!
  itself are deferred, optional future local patches — not blockers.

## Building

```
git submodule update --init --recursive
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

Requires the `audacious` pkg-config package (for `libaudcore`/headers)
plus PsfCore's own build-time dependencies (ICU, cURL, OpenSSL, BZip2,
zstd, xxHash — dev packages). See project memory for why the compile is
heavier than the final plug-in: the whole build links Play!'s full
emulator core, but `--gc-sections` discards everything the PSF-only path
doesn't reach.

Builds `psfplayer.so` (installs to the `audacious` plugin dir's `Input/`
subdirectory) plus two test binaries:
- `tests/test_soundqueue` — synthetic threading test for the bridge
  itself, no real PSF file needed. Run via `ctest` or directly.
- `tests/test_integration_manual <path> [seconds]` — real end-to-end
  smoke test against an actual PSF/PSF2 file. Not run by `ctest` (no
  sample file ships with this repository).

`third_party/Play` is upstream `jpd002/Play-` — no local patches. Only
its `tools/PsfPlayer/Source` sub-tree (the `PsfCore` static library target)
is needed; nothing in this repository builds Play!'s own emulator
front-end/GUI.

## Licence

2-clause BSD — see [LICENSE](LICENSE), which also reproduces Play!'s own
2-clause BSD notice (statically linked in via `third_party/Play`).
