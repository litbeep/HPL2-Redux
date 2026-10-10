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

### 1. Static geometry on screen

- `cWorldLoaderPenumbraCollada` in `HPL2/core` (registered for `.dae` by
  this product only, so Amnesia's own `.dae` handling is untouched): port
  HPL1's `LoadWorld` and the geometry and light parts of `AddSceneObjects`.
- Material type aliases (above).
- A free camera in the product (fly, mouse look), placed at the `_start_`
  node given by `--start`.

**Done when:** `maptest0_complex.dae` and the first Overture level render
with their lights, on the standard renderer and the ray-traced one.

### 2. Physics, sounds, particles, billboards

- Colliders and character colliders; `materials.cfg` physics materials.
- `.snt`, `.ps`, `.bnt`, `.beam`, `.lnt` readers where HPL2's differ.
- A character body with Amnesia's normal move state (no other game code yet)
  so the level can be walked.

**Done when:** walking `maptest0_complex.dae` collides correctly, its
physics boxes fall and stack, and the first Overture level plays its
ambient sounds and particles.

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

### 4. Areas

- Area loaders for the eight Overture types, creating the areas with their
  sizes and transforms. Their settings come from the map scripts
  (`SetupLink`, `SetupSaveArea`, `SetupLadder`, `SetupDamageArea`,
  `SetupForceArea`, `SetupLiquidArea`, `SetupStickArea`), so behaviour
  waits for milestone 5; liquid and force areas can be checked by hand.

**Done when:** the debug view shows every area of a level in place, with
its type.

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

### 6. Every level loads

- A batch mode, `--map <file> --start <name> --frames N --quit`, logging
  every missing file, unknown node, material type, entity type, area type
  and script function.
- Run all 25 Overture levels (entrance `link01`) from the real install, and
  the test install in CI.

**Done when:** all Overture levels load with no errors in the log and can
be travelled through in order.

### 7. Later: editing and the other games

- Save a loaded level as `.map` plus HPL2 `.ent` files into the user's
  directory, so the reimagining's changes can be made in the level editor;
  Penumbra's entity and area types added to the editor's `EntityTypes.cfg`
  and `AreaTypes.cfg`. Converted files stay on the user's machine.
- Black Plague and Requiem levels: same formats; check their data for
  anything Overture's never used.

## Open questions

- Coordinate system and units: both engines read Collada's up axis; verify
  that levels and entities line up (milestone 1).
- Do Overture's `.hps` scripts compile under HPL2's AngelScript versions,
  or does the product need a newer AngelScript build?
- How much of HPL1's `.snt`, `.ps` and `.lnt` formats differs from HPL2's.
- Sector ambient colour: which levels set it, and what replaces it.
- Whether the product should reuse Amnesia's game code (`Lux*`) as its base
  from milestone 2, or start smaller and pull pieces in. Amnesia's player,
  interaction states and map handler are close to Penumbra's; its menus,
  inventory and sanity are not.
