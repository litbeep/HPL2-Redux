# Penumbra levels booting in HPL2

Goal: load any Penumbra: Overture level straight from the player's install
in this fork, walk through it with working physics, lights, sounds, entities
and areas, and travel between levels through its link areas. Gameplay
(Penumbra's player states, inventory, enemies, menus) comes after this plan.

References: Antumbra (`litbeep/antumbra`) for how HPL1 reads Penumbra's
files (paths in `AGENTS.md`); in this repository `HPL2/core` for the engine
and `amnesia/src/game` for Amnesia's game code.

## Approach: load the install, don't convert it

Penumbra's levels are loaded **at run time from the install**, by HPL1-format
loaders added to HPL2, instead of being converted to HPL2's `.map` and `.ent`
formats ahead of time:

- Nothing derived from the assets is written or shipped, so the no-assets
  rule is easy to keep. Players point the game at their install, as with
  Antumbra.
- HPL2 already chooses its loaders by file type: world loaders by extension
  (`cWorldLoaderHandler::LoadWorld`), entity loaders by type name
  (`cResources::AddEntityLoader`), area loaders by type name. HPL2's Collada
  code is a descendant of HPL1's (same `cColladaNode`, `FillStructures`,
  and mesh colliders), so HPL1's world loader ports over with little change.
- Converting to `.map` for the level editor is still wanted for the
  reimagining's own edits. It comes later (milestone 7) and reuses these
  loaders: load the level, then save the world.

## How Penumbra levels map onto HPL2

A Penumbra level is one Collada scene (`maps/<name>.dae`) plus a script
(`maps/<name>.hps`). HPL1 reads meaning from node names
(`MeshLoaderCollada::AddSceneObjects` in Antumbra):

| Penumbra node or file | HPL2 target |
|---|---|
| Plain geometry (static meshes, Penumbra materials) | `cWorld::CreateMeshEntity`, static, in the world's static container |
| `static_<file>` nodes referencing an external `.dae` | Mesh from `cMeshManager`, static mesh entity |
| `_collider_`, `_charcollider_` | Static physics bodies (character-only for `charcollider`) |
| Collada point and spot lights; `dynamic_` prefix; `<file>_<name>` with a `.lnt` light file | `CreateLightPoint` / `CreateLightSpot`; `.lnt` read by a small loader (flicker and other settings) |
| `_ref_<file>_<name>` | `cWorld::CreateEntity` with the HPL1 `.ent` (milestone 3) |
| `_area_<type>_<name>` (box from the node's geometry) | `cWorld::CreateAreaEntity` plus `iAreaLoader` by type (`script`, `link`, `save`, `ladder`, `damage`, `force`, `liquid`, `stick`) |
| `_sound_<file>_<name>` | `CreateSoundEntity` (HPL1 `.snt`, see below) |
| `_ps_<file>_<name>` | `CreateParticleSystem` (HPL1 `.ps`, see below) |
| `_bb_`, `_beam_` (`.bnt`, `.beam` files) | `CreateBillboard`, `CreateBeam` |
| `_start_<name>` | `CreateStartPos` |
| `_node_<type>_<name>` (AI path nodes) | `cWorld::AddAINode` |
| `_room*`, `_portal*` (sectors and portals) | Dropped: HPL2 culls with box trees and occlusion queries. Sector ambient colour becomes an ambient light per room or a world setting |
| `maps/<name>.hps` | The product's script layer (milestone 5) |

Material, entity, sound and particle files are XML in both engines, and
HPL2's formats grew out of HPL1's, so each is read by a compatibility path
rather than converted:

- **Materials (`.mat`)**: same `Main` / `TextureUnits` layout. HPL1's types
  (`Diffuse`, `DiffuseSpec`, `Bump`, `BumpSpec`, `BumpColorSpec`,
  `BumpSpec2D`, `Additive`, `Alpha`, `Modulative`, `ModulativeX2`,
  `EnvMap_Reflect`, `Water`, `Flat`, ...) map onto HPL2's `soliddiffuse`,
  `translucent` (with blend mode), `water` and `decal`. Texture units
  `Diffuse`, `NMap`, `Specular`, `Illumination`, `CubeMap` carry over; HPL1's
  `Alpha` and `Refraction` units need handling. Add the type aliases in the
  material manager, marked `PENUMBRA:`.
- **Entities (`.ent`)**: different root layout (HPL1: `MAIN Type=...`,
  `GRAPHICS ModelFile=...` with bodies and joints inside the model `.dae`;
  HPL2: `ModelData` and `UserDefinedVariables EntityType=...`).
  `cWorld::CreateEntity` gets one marked change: when the root has a `MAIN`
  element, the loader is chosen by `"Penumbra" + MAIN/Type`.
- **Sounds (`.snt`) and particle systems (`.ps`)**: compare the two
  engines' readers element by element and read HPL1's layout where it
  differs. To be checked in milestone 2.
- **Physics materials (`materials.cfg`)** and the install's
  `resources.cfg`: read from the install.

## Milestones

Each milestone ends with something you can run. "Test install" is Antumbra's
(`antumbra_test_install`); "real install" is Penumbra: Overture.

### 0. The `penumbra` product

- `premake/product.lua`: add `penumbra`; `premake/penumbra.lua` modelled on
  `premake/amfp.lua`; `penumbra/src/game` with a minimal `main`.
- The executable takes `--game <install>` (the directory with
  `resources.cfg`, or its `redist` subdirectory, as Antumbra accepts) and
  `--map <file> --start <name>`.
- Resource directories: the install's `resources.cfg` read with the install
  as base path, then `penumbra/resources` (this product's files), then
  Redux's engine files. Mesh caches go to the user directory, never the
  install (`cMeshLoaderCollada` writes `.msh` next to the `.dae` today).
- Pick the AngelScript build (2.19.2 or AMFP's 2.24.1) that compiles
  Overture's `.hps` files; check with the real install.
- Engine files Redux takes from Amnesia's install, made by this product
  instead: `core_dissolve.tga` (noise), `colorgrading_base.png` (identity
  table), a default font and GUI skin, `main_init.cfg`-style configs.
- CI builds `--product=penumbra` alongside Amnesia and AMFP.

**Done when:** the executable starts, opens a window and shuts down cleanly
with the test install and with no Amnesia install present.

**Built.** Run with the test install on Mesa's software Vulkan (lavapipe)
under Xvfb, and on Windows with an RTX 4070 Ti and a real Overture install.

- `--product=penumbra` declares the `Penumbra` project and its own engine
  build, `HPL2_Penumbra` (define `PENUMBRA`), and nothing else: without it
  the generated Amnesia and AMFP projects are unchanged. The executable
  goes to `build-premake/penumbra/<Config>/` and works from its own
  directory wherever it is started from.
- AngelScript: AMFP's 2.24.1 with the `std::string` add-on. HPL1 registered
  `string` as a value type (`RegisterStdString`; Antumbra compiles
  Overture's scripts under 2.35.0 with the same add-on), while TDD's 2.19.2
  build registers a reference type (`string@`, `asCScriptString`), which
  Overture's scripts were never written against. Milestone 5 now compiles
  and runs the first five retail `.hps` scripts under 2.24.1; the test
  install has none. The engine's few AngelScript API
  differences (`SqScript.cpp`, `scripthelper.cpp`, `LowLevelSystemSDL.cpp`)
  are now selected by `ANGELSCRIPT_VERSION` instead of `AMFP`, which builds
  TDD and AMFP exactly as before.
- Lookup order is set by priority, not registration order: the install's
  directories (its `resources.cfg`, read as HPL1 does: `Path` relative to
  the data root, leading slash dropped, no subdirectories; names matched
  case-insensitively) at 20, the product's (`resources.cfg` next to the
  executable: `fonts/`, `gui/`) at 10, the engine's (`core/*`,
  `compiled_shaders/*`) at 0. The product's replacements for engine files
  that the engine loads while it starts (`core_dissolve.tga`) sit in
  `core/textures/` next to the executable, which the engine registers itself.
- The product's own files are named `penumbra_*` (`penumbra_default.skin`,
  `penumbra_default.fnt`): HPL1 installs ship a `gui_default.skin` in HPL1's
  format, which would shadow a product file of the same name.
  `scripts/gen_penumbra_resources.py` writes the textures, font (rasterised
  DejaVu Sans) and skin; the configs are written by hand.
- User directory: `Penumbra Redux/` under Frictional's personal root
  (`~/.frictionalgames/` on Linux), set in `config/main_init.cfg`: the log,
  `main_settings.cfg` once it exists, and `cache/meshes/` for `.msh` caches
  (`iMeshLoader::SetMeshCacheDir`; one flat directory, files named after the
  mesh plus a hash of its full path). Amnesia and AMFP set no cache directory
  and keep caching next to the source.

### 1. Static geometry on screen

- `cWorldLoaderPenumbraCollada` in `HPL2/core` (registered for `.dae` by
  this product only, so Amnesia's own `.dae` handling is untouched): port
  HPL1's `LoadWorld` and the geometry and light parts of `AddSceneObjects`.
- Material type aliases (above).
- A free camera in the product (fly, mouse look), placed at the `_start_`
  node given by `--start`.

**Done when:** `maptest0_complex.dae` and the first Overture level render
with their lights, on the standard renderer and the ray-traced one.

**Implemented and smoke-tested on Windows/D3D12.** Both renderers completed
24 frames and exited cleanly for the test map (36 static meshes, 3 lights,
1 start) and Overture's boat cabin (39 static meshes, 1 light, 1 start),
with zero loader errors. These are execution checks; visual fidelity still
needs comparison with Antumbra.

From the repository root, after building `--product=penumbra`:

```powershell
.\build-premake\penumbra\Release\Penumbra.exe --game "C:\Steam\steamapps\common\Penumbra Overture" --map level00_01_boat_cabin.dae --start link1
```

Use your own install path. Mouse look and WASD move the camera; Q/E move
down/up, Left Shift moves faster, and Escape exits. The camera starts 1.7
units above the entrance. `--renderer standard` selects the standard
renderer; `--renderer raytraced` selects ray tracing. Without an override,
the product config selects the renderer. `--frames 24` runs a bounded check
in the background: windowed, shown without taking focus, and without
grabbing the mouse. With `--travel all` (or a list of `map[:start]`), it
then changes map to each level in turn, `--frames` frames each. Missing entrances fail before the render loop.

The loader preserves room hierarchy and world transforms, hides portal and
other helper geometry, honors `nodraw`/`noshadow`, and loads point/spot
lights with existing `.lnt` properties. Penumbra's Z-up node positions and
rotation axes follow HPL1, matching its vertex conversion. Mesh caches stay
in the user directory; the world loader reads original XML without writing
HPL1 `.dae_cache` files.

Material aliases cover Overture's solid and blend types. `Flat` currently
uses a lit solid material, untextured Collada effects use neutral white,
and HPL1-specific alpha/refraction behavior still needs visual comparison
and further compatibility work. Entities, colliders, areas, sounds,
particles, billboards and scripts remain deferred: this is a static viewer.

`tests/penumbra_map_smoke.py` generates original synthetic fixtures and
checks both renderers, Y-up/Z-up entrances, material aliases, helper-node
exclusion, frame limits, missing entrances, and unchanged source files.
See `tests/README.md` for its command. Amnesia and AMFP engine libraries
also compile with the shared changes; their game executables were not
retested in this milestone.

### 2. Physics, sounds, particles, billboards

- Colliders and character colliders; `materials.cfg` physics materials.
- `.snt`, `.ps`, `.bnt`, `.beam`, `.lnt` readers where HPL2's differ.
- A character body with Amnesia's normal move state (no other game code yet)
  so the level can be walked.

**Done when:** walking `maptest0_complex.dae` collides correctly, its
physics boxes fall and stack, and the first Overture level plays its
ambient sounds and particles.

**Implemented and smoke-tested on Windows/D3D12.** The Collada world now creates a
Newton physics world, loads `materials.cfg`, and makes collision meshes for
ordinary geometry (including `nodraw`, excluding `nocollide`). Primitive
`_collider_` and character-only `_charcollider_` helpers are supported.
`nocharcollide`, `soundblock` and `nosoundblock` flags are preserved.

Add `--walk` to the milestone 1 command to use HPL2's character controller
with gravity, collision, steps and pushing. WASD moves, Left Shift runs,
Space jumps and the mouse looks. Without `--walk`, the free camera remains
available for inspection. This uses the engine controller underlying
Amnesia's normal movement; it does not bring in Amnesia's game states,
head bob, footsteps, inventory or interaction system.

On Windows, `.\run-penumbra.ps1` from the repository root launches the
Release build in the boat cabin with walking and the standard renderer.
The launcher defaults to `C:\Steam\steamapps\common\Penumbra Overture`;
use `-GameDir "D:\Games\Penumbra Overture"` for another install. Options
include `-Renderer raytraced`, `-FreeCamera`, `-Map <file> -Start <name>`,
`-Configuration Debug`, and `-Frames 24`. Run `.\run-penumbra.ps1 -Help`
for usage. It can also be invoked by its full path from another directory.

Simple HPL1 `Object` entities with one `PHYSICS` block load as rigid props,
including the test map's ten wood boxes. Authored model colliders can be
recovered from original `.dae` XML if a mesh cache lacks collider metadata.
Multipart bodies, joints, animations and other entity types remain for
milestone 3; there is no approximate box substituted for a missing collider.

Map-defined sounds, particles, billboards and beams now load. HPL1 `.snt`
`MAIN` sound names expand numbered variants (or `stream_` fallback);
`PROPERTIES` uses the existing reader. Particle emitter attributes match
the existing HPL2 reader. Billboard `.bnt` properties are read in the
Penumbra loader, and `.beam`/`.lnt` use the existing readers. The boat cabin
has four sound nodes and four billboard nodes; it has no map-defined
particle systems. Effects created by `.hps` scripts wait for milestone 5.

The test install omits its referenced boiler-room sound and several
surface impact effects. Missing optional effects are logged and skipped;
physics properties remain active. Full visual/audio equivalence and
`HaloSourceIsParent` for entity-attached billboards remain unverified.

Validation: the synthetic GPU suite passes Y-up and Z-up environment
fixtures on both renderers, missing-entrance handling, character landing,
rigid-box falling/stacking, live particles, active audio channels, and
unchanged source fixture hashes. Both renderers also complete 240-frame
walking runs on `maptest0_complex.dae` (45 bodies, including ten wood boxes)
and the boat cabin (34 bodies, four active main sound channels, four
billboards) with a grounded player and no errors. Missing optional files
still produce warnings. Amnesia and AMFP engine libraries compile; their
full game executables were not run. The Python suite passes 15 tests with
two skips, using Penumbra's compiled shaders for the binding checks.

### 3. Entities

- Engine-level `cEntityLoader_PenumbraObject`: HPL1's
  `cEntityLoader_Object` ported onto HPL2 (mesh with sub-mesh materials,
  bodies and joints from the model `.dae`, animations and events, attached
  lights and billboards, `GAME` attributes kept for the game code).
- Registered for the five Overture types (`Object`, `Item`, `SwingDoor`,
  `Lamp`, `Enemy`) as plain physical props for now; behaviour comes with
  the gameplay work.

**Done when:** every `_ref_` in the first five Overture levels loads with
its physics (doors swing, objects fall), and `maptest0_anim.dae`'s skinned
meshes animate.

**Implemented and validated on Windows/D3D12.** All 177 references in
Overture's first five levels (boat cabin, outside, mine entrance, shafts,
old storage) load without errors on both renderers, with 120 frames per
level. The loader reads original model colliders, creates grouped bodies
and hinge/ball/slider/screw joints, applies physics settings and joint
controllers, loads animations/events and material overrides, and attaches
model lights, sounds, particles, billboards and beams. The original entity
XML remains in the world's entity-file cache, including `GAME`; mesh
entities retain their source filename for the later gameplay layer.

Language resource directories come from the install's language file
(`English.lang` by default, configurable in the product's `main_init.cfg`).
Older `_charcollider_<instance>` nodes default to box shapes as in HPL1.
Missing authored bodies/joints are warned about and skipped as HPL1 does;
the boat's hanging lamp includes settings for a body with no collider.

Generated fixtures test all five entity types, a material override,
falling props and a moving hinge on both renderers, and check source-file
hashes to detect writes into the install. The existing map/environment
checks also pass. Antumbra's `maptest0_anim.dae` passes on both renderers:
all four skinned instances have active animation clocks and changing bone
positions between 8 and 120 frames, with source-file hashes unchanged.
Amnesia and AMFP's shared engine builds compile with the
changes. Entities remain physical props: picking up items, operating doors,
enemy behavior and script-driven state wait for later milestones.

### 4. Areas

- Area loaders for the eight Overture types, creating the areas with their
  sizes and transforms. Their settings come from the map scripts
  (`SetupLink`, `SetupSaveArea`, `SetupLadder`, `SetupDamageArea`,
  `SetupForceArea`, `SetupLiquidArea`, `SetupStickArea`), so behaviour
  waits for milestone 5; liquid and force areas can be checked by hand.

**Done when:** the debug view shows every area of a level in place, with
its type.

**Implemented.** The eight product area loaders retain names, types, sizes
and world transforms; generic `_area_<name>` nodes are retained too. The
Collada reader follows HPL1's bounds and naming rules, including underscores
inside area names, and excludes area helpers from rendering and collision.
Area behavior remains for milestone 5's script setup functions.

Launch with `--debug-areas`, or use `./run-penumbra.ps1 -DebugAreas` on
Windows. F3 toggles the overlay. It draws rotated wireframe bounds through
geometry and labels each visible area with its name and type. Overlay text
comes from the product's own language file.

All 129 areas in the first five Overture levels loaded without errors on
both renderers (24 frames per run). Generated Y-up and Z-up fixtures pass
on both renderers, checking all eight types, generic areas, name retention,
scaled dimensions, parent transforms and unchanged source-file hashes.
The initial overlay was visually inspected using original synthetic assets;
labels were then centered, split into two lines and given a shadow for
readability. The final Release build and Amnesia/AMFP shared engine builds
pass. Further desktop control and windowed checks are left to the user.

### 5. Map scripts

- The script layer: Overture's script functions registered under their
  original names and signatures, so the install's `.hps` files run
  unchanged. Start with what levels need to start and connect:
  `OnStart`/`OnLoad`/`OnExit`, the `Setup*` area functions, timers, local
  and global variables, collide callbacks, sounds, music, lights, fades.
- Keep a table of all 174 functions with their status (missing, stub,
  done) in `docs/`; the behaviour of each is in Antumbra's
  `GameScripts.cpp`.
- Link areas change the map (`ChangeMap`), entering at the named start.

**Done when:** the first Overture level's script runs without errors and
its link area takes the player to the next level.

**Script host implemented; interactive travel awaits a user-run check.**
The product owns an AngelScript 2.24.1 host with the HPL1 `string` value type.
It reads the original `.hps` at runtime and never writes compiled scripts
or other files into the install. `OnStart` runs on a map's first visit,
`OnLoad` on every load, `OnUpdate` at engine logic ticks, and `OnExit` before
unloading. Timers, local/global variables, messages, subtitles, basic carried
items, pickup/use/interact callbacks, entity activation, lights, sounds,
music, fog, fades and link setup have compatibility implementations.

The unchanged boat script compiles, runs its intro timers and pickup
callbacks, unlocks the exit after the required items, then invokes
`ChangeMap` to `level01_01_outside.dae` at `link1` in a headless check. The
first five retail scripts each compile and run 600 ticks without errors.
Generated fixtures cover lifecycle ordering, timer mutation and pause,
message acknowledgement, state, inventory, links, callback string arguments,
script compilation failures, execution budgets and invalid destination starts.

The viewer now runs those scripts too. Left mouse interacts/picks up,
right mouse examines, U uses a matching carried item, and Space/Enter
acknowledges messages. Physical interaction is a simple nudge of a prop or
hinged body, pending the full HPL1 dragging/player states. `ChangeMap` loads
the destination and validates its entrance, detaches the old walking body,
rebinds the viewport, places the camera/body at the new start, and unloads
the previous world. Rendering, physical interaction and this world-replacement
path have been compiled but not exercised interactively: desktop control and
game-window tests are disabled at the user's request.

The [API status table](penumbra-script-api.md) lists the **173** registered
functions found in this Antumbra checkout (59 engine + 114 game), rather
than the plan's expected 174. Stubs are explicit and warn once. Area setup
data is retained; ladder/damage/force/liquid/save/stick behavior remains
pending. Collision callbacks use point-in-area overlap for now, and global
ambient color is approximated with a legacy box light. Full inventory UI,
enemy AI, advanced post effects and save games remain later gameplay work.
Link audio/loading text and requested arrival fade durations remain pending;
arrival currently fades in over 0.5 seconds. Game music uses a single channel
without HPL1 priorities. Interaction targets use bounds without occlusion checks.

Run scripts without graphics, audio or windows:

```powershell
.\run-penumbra.ps1 -ScriptCheck -ScriptSteps 600
```

For regression checks (original generated fixtures, optional unchanged
retail boat script):

```powershell
python tests/penumbra_script_smoke.py --exe build-premake/penumbra/Release/Penumbra.exe --log "D:\Documents\Penumbra Redux\penumbra.log" --game "C:\Steam\steamapps\common\Penumbra Overture"
```

**Names and failures follow HPL1.** A `_ref_` entity is named after the
last part of its node name (`_ref_<resource>_<name>`, or the older
`_ref_<name>_<resource>`), which is the name scripts use. Scripts take the
entities and areas from the loaded world; each entity body's user data is its
mesh entity. A node that fails to load is logged and skipped, and an entity
that fails part-way is removed again; only a level with no geometry fails.
A script that does not compile, or a call that throws or runs too long, is
logged and counted, and play goes on. Headless checks and `--frames` runs
still exit with 1 when any script error occurred.

### 6. Every level loads

- A batch mode, `--map <file> --start <name> --frames N --quit`, logging
  every missing file, unknown node, material type, entity type, area type
  and script function.
- Run all 25 Overture levels (entrance `link01`) from the real install, and
  the test install in CI.

**Done when:** all Overture levels load with no errors in the log and can
be travelled through in order.

**Done on Windows/D3D12 with a retail Overture install.** All 25 levels load,
render on both renderers and run their scripts (600 headless ticks) with no
error in the log other than defects in Overture's own data, which HPL1 can't
load either. `--travel all` changes map through every level in one process,
the same world replacement a link makes, on both renderers. The scripts'
SetupLink and ChangeMap calls lead to existing maps, and every level can be
reached from the boat cabin.

`scripts/penumbra_level_sweep.py` runs the sweep and writes a report next to
the log (it names the install's files: never commit it). It groups problems
by frequency, files understood messages under "known and harmless" with the
reason, lists the script functions still stubs, and checks the links. The
first sweep found, and this milestone fixed:

- Crashes. A model with a node inside a node of the same name made a node
  its own child (`cMeshEntity::CreateNodes` matched by name; now by node).
  An entity model asked for without its extension was read from an `.anm`
  file of the same name. Newton crashes building a tree collision with one
  face, so a one-triangle level mesh gets its back face as a second.
- Scripts. HPL1's scripts use `T[]` arrays with initialisation lists (now
  registered, as Antumbra does) and `if(x);`, which later AngelScript
  versions reject; it is rewritten as `if(x){}`, which means the same.
- Data HPL1 tolerated: unquoted XML attribute values; animated textures
  named by their base name (`x.jpg` for `x01.jpg`, `x02.jpg`...);
  `PlayGuiSound` names with numbered variants (`name1.ogg`...), played at
  random as HPL1 does; links naming a missing entrance, which put the player
  at the level's first one, as HPL1 does.
- Level geometry whose material is missing is drawn untextured.

Unhandled crashes write the stack to the log (`penumbra/src/game/CrashLog.cpp`,
Windows). The data defects: `modern_mine_cardboardbox` has no material or
texture, `mining_room_book.dae`'s texture points at the artist's disk, a map
references `kitchen_whisketbottle.ent` (sic) and `dust_sust01.ps`, and some
models' physics settings or joints name bodies without colliders.

Still open, and left to the gameplay work: the 30 script functions that are
stubs (most used: `SetMapGameNameTrans`, `PreloadSound`, `AutoSave`,
`SetSkybox`, `SetDoorLocked`, `SetLampLit`) and the area behaviours.
Black Plague and Requiem have not been swept.

### 7. Later: editing and the other games

- Save a loaded level as `.map` plus HPL2 `.ent` files into the user's
  directory, so the reimagining's changes can be made in the level editor;
  Penumbra's entity and area types added to the editor's `EntityTypes.cfg`
  and `AreaTypes.cfg`. Converted files stay on the user's machine.
- Black Plague and Requiem levels: same formats; check their data for
  anything Overture's never used.

## Open questions

- Coordinate system and units: static Y-up/Z-up fixtures are checked in
  milestone 1; verify entity alignment when entities are implemented.
- The first five Overture `.hps` scripts compile under AngelScript 2.24.1
  with the HPL1 API registered; compatibility with every level is still
  milestone 6 work.
- How much of HPL1's `.snt`, `.ps` and `.lnt` formats differs from HPL2's.
- Sector ambient colour: which levels set it, and what replaces it.
- Whether the product should reuse Amnesia's game code (`Lux*`) as its base
  from milestone 2, or start smaller and pull pieces in. Amnesia's player,
  interaction states and map handler are close to Penumbra's; its menus,
  inventory and sanity are not.
