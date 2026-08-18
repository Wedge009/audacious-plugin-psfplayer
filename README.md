# audacious-plugin-psfplayer

An Audacious `InputPlugin` for PSF/PSF2 files, built on top of
[jpd002/Play-](https://github.com/jpd002/Play-)'s `PsfPlayer` core
(`PsfCore`) instead of the bundled OpenPSF decoder
(`audacious-plugins/src/psf/`).

## Why

Audacious's existing OpenPSF plugin and kode54's AOPSF (a personal-use
alternative built during the same investigation) both hang forever on
certain BIOS-dependent titles (e.g. Final Fantasy X's `.minipsf2` files),
due to a shared synchronous-softcall IOP dispatch architecture. Play!'s
more complete HLE BIOS avoids that entire bug family: an evaluation
render of the same file that hangs both other engines completed cleanly
in ~2.5 seconds, and 10 other known-problem tracks (from Final Fantasy
IV/X-2/XI) also rendered clean. Play! also carries a single, clean
2-clause BSD license — unlike AOPSF's unresolved BSD/MAME/GPL mix — which
matters here because, unlike the AOPSF plugin (a personal proof-of-concept
never intended for distribution), this one is meant to become something
real users can actually install.

## Status

Scaffolding only. `third_party/Play` is a git submodule pinned to the
commit the above evaluation was run against. No plugin code has been
written yet — the `InputPlugin` implementation (build system, the
threading bridge between Play!'s own background-thread-driven `CPsfVm`
and Audacious's synchronous `play()` convention, a VFSFile-backed stream
provider, and a from-scratch length/fade/end-of-track policy, since
`CPsfVm` itself never signals "song over") is designed but not yet built.

## Building the submodule

```
git submodule update --init --recursive
```

`third_party/Play` is upstream `jpd002/Play-` — no local patches. Only
its `tools/PsfPlayer/Source` subtree (the `PsfCore` static library target)
is needed; nothing in this repo builds Play!'s own emulator frontend/GUI.
