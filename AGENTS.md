# Notes for agents

HPL2-Redux (`litbeep/HPL2-Redux`) is a fork of Amnesia: The Dark Descent
Redux (`OSS-Cosmic/AmnesiaTheDarkDescent`, also published as
`flying-swallow/Redux-AmnesiaTheDarkDescent`): Frictional Games' HPL2 engine
with a Vulkan/Direct3D 12 renderer, Slang shaders, an optional ray-traced
renderer, and the editors building from source. Upstream
builds two games from it as separate *products* (`amnesia`, `amfp`). This
fork adds a third, `penumbra`: a reimagining of Frictional's Penumbra games
on HPL2, loading the player's own Penumbra install.

Read `docs/penumbra-levels.md` first: it is the current plan (Penumbra
levels booting in HPL2). Upstream's README and `BUILD.md` cover building and
running Amnesia.

## Where this is going

The aim is a standalone reimagining of the Penumbra series, not a straight
port (that is Antumbra) and not a mod for a newer Frictional game. Levels
are rebuilt, and remastered or expanded where it helps, from a kitbashed
building set made of Penumbra's own meshes. The visual target is close to
Amnesia: Rebirth and The Bunker.

What follows from that:

- Loading the original levels unchanged (the current plan) is the
  foundation and the baseline for comparisons, not the finished product.
- The project's own maps and data files are original work and may be
  committed. They refer to the install's meshes, textures and sounds by
  name and never contain or embed them.
- Better materials (higher resolution, roughness and other PBR maps) are
  either new original art or made on the player's machine from their
  install and cached in the user directory, like `.msh` caches. Never
  generate them into the repository.
- The renderer is already modern (Vulkan/D3D12, ray tracing). Most of the
  remaining visual gap is art and lighting, so treat fidelity as a problem
  for the art pipeline and tools rather than a reason to rewrite the
  renderer.
- Kitbashing needs the level editor to place install meshes directly
  (milestone 7 of the plan). Work that brings that closer counts for more
  than polishing the viewer.
- Prove ideas on one Overture level end to end before broadening to the
  whole trilogy.

## The sister project: Antumbra

Antumbra (`litbeep/antumbra`) is the faithful source port of Penumbra on
HPL1 (ScummVM's port of Frictional's 2010 Overture release, with an SDL3
platform layer). It is the reference for how Penumbra behaves and how its
data is read. When this fork needs to know what a Penumbra file means or
what a script function does, read Antumbra's code rather than guessing:

- Map loading: `third_party/scummvm/engines/hpl1/engine/impl/MeshLoaderCollada.cpp`
  (`LoadWorld`, `AddSceneObjects`: the `_ref_`, `_area_`, `_sound_`,
  `_ps_`, `_bb_`, `_beam_`, `_start_`, `_node_`, `_collider_`,
  `_charcollider_`, `_room`/`_portal` node conventions).
- Entities: `engine/resources/EntityLoader_Object.cpp` (HPL1 `.ent`:
  `MAIN`, `GRAPHICS`, `SUBMESH`, `ANIMATIONS`, `PHYSICS`, `JOINT`, `LIGHT`,
  `ATTACH_BILLBOARDS`, `GAME`).
- Game code: `third_party/scummvm/engines/hpl1/penumbra-overture/`: entity
  types (`Object`, `Item`, `SwingDoor`, `Lamp`, `Enemy`), area types
  (`script`, `link`, `save`, `ladder`, `damage`, `force`, `liquid`,
  `stick`), the 174 script functions (`GameScripts.cpp`), hiding in shadows
  (`cPlayerHidden`, `PlayerHelper.cpp`).
- Install layout and detection: `src/app/game_detection.cpp`,
  `src/app/level_catalog.cpp` (Overture, Black Plague, Requiem).

Antumbra can also run any Penumbra level with `--steps`, `--capture` and
`--state-hash`, which makes it the baseline to compare against.

## Layout

- `HPL2/core/`: the engine (shared by every product). `HPL2/tools/`: the
  editors and converters. `HPL2/extern/`: dependencies (git submodules).
- `amnesia/`: Amnesia's game code (`src/game`, the `Lux*` classes), the
  Slang shaders shared by all products (`amnesia/slang`), and Redux's
  resource overlay (`amnesia/resources`). `amfp/`: A Machine for Pigs.
- `penumbra/` (this fork): `src/game` the Penumbra game code, `resources`
  this product's own files (configs, generated textures, GUI skins, fonts),
  never the install's. The generated ones come from
  `scripts/gen_penumbra_resources.py`; the build stages the directory next
  to the executable.
- `premake5.lua`, `premake/*.lua`: the build. Products are declared in
  `premake/product.lua`; `premake/amfp.lua` is the pattern a product
  project follows (`premake/penumbra.lua` here).
- `scripts/`, `tests/` (C++ utest suites and Python tests), `docs/`.

## Ground rules

- GPLv3. Dependencies keep their licences.
- **No game assets in the repository, releases or CI artifacts, ever:**
  not Penumbra's, not Amnesia's, and nothing derived from them (converted
  maps or entities, `.msh` caches, screenshots or captures of the games).
  The game reads the player's Penumbra install at run time. Tests use
  Antumbra's test install (HPL1 engine test assets, CC BY-SA 3.0, downloaded
  at build time, also never committed).
- **Never write into the player's install.** HPL2 caches meshes as `.msh`
  next to their source file; for this product caches go to the user
  directory.
- **Clean room for Black Plague and Requiem.** Their game code was never
  released. Behaviour is recreated from observing the games and reading
  their data, never from decompiled code. GPL code from Frictional's
  releases (Overture via Antumbra, Amnesia, AMFP) may be used freely.
- **Keep the fork mergeable with upstream.** Product code goes in
  `penumbra/`. Changes to shared code (`HPL2/`, `amnesia/`, `premake/`,
  `scripts/`) are small and marked with a `PENUMBRA:` comment so they can be
  found when merging upstream. Changes useful to every product (bug fixes,
  generic hooks) should be offered upstream as pull requests.
- Don't change Amnesia's or AMFP's behaviour. Their builds must keep
  working; CI builds them.

## Building

Follow `BUILD.md`. In short:

- Linux: `./build-linux.sh [release|debug] [-with-test] [-- <premake args>]`
  (or `./build-linux-docker.sh`, the canonical container build).
- Windows: `.\build-windows.ps1 [-WithTest]`, or
  `premake5 vs2026` and MSBuild on `build-premake\Amnesia.sln`.
- This product: add `--product=penumbra` to the premake arguments, e.g.
  `./build-linux.sh release -- --product=penumbra`. Output goes to
  `build-premake/penumbra/<Config>/`.
- Shaders are Slang, compiled by a pinned `slangc` the build downloads.

## Testing

- Unit tests: build with `-with-test` (Linux) or `-WithTest` (Windows);
  Python tests: `python3 scripts/run_python_tests.py`. Conventions are in
  `tests/README.md` (utest, one runner per executable).
- Penumbra levels: run against Antumbra's test install
  (`cmake --build <antumbra build> --target antumbra_test_install` writes
  `build/test-install`, maps `maptest0_complex.dae` and
  `maptest0_anim.dae`, entrance `Start1`) and, locally, against a real
  Penumbra install. Never commit or upload what they produce.
- Penumbra: Overture's maps are listed by Antumbra's `--list-maps`; the
  common entrance is `link01`.

## Writing code here

- Match the surrounding file: Frictional's code uses tabs and Hungarian-ish
  names (`mpFoo`, `mfBar`, `afArg`); Redux's newer files use two-space
  indentation and their own naming. Don't reformat code you aren't
  changing.
- Commit messages follow upstream's conventional style (`feat:`, `fix:`,
  `docs:`, `build:`), with a body saying what changed and why. Commit or
  push only when asked.
- Text shown to players that Penumbra's language files lack goes in this
  product's own language file under `penumbra/resources`, never hard-coded.
