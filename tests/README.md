# Tests

Project-owned C/C++ tests have a vendored `sheredom/utest.h` foundation
in [`third_party/utest`](third_party/utest). The pinned header, genuine
upstream license, source URLs, commit, and SHA-256 values are recorded in
[`third_party/utest/README.md`](third_party/utest/README.md). No build-time
network access or dependency download is required.

The Python suite runs with `python3 scripts/run_python_tests.py`. Its
source-resource checks do not require a retail installation.

## Test projects

`premake/tests.lua` applies one local include helper to every C++ test project.
That only makes the vendored header available; it does not convert every suite
to utest. `BindlessPoolTests`, `FsrVulkanLoaderTests`, `HplMemoryManagerTests`,
`HplMemoryManagerDisabledFacadeTests`, `FileSearcherTests`, and
`ResourceCacheTests`, `FluidStudiosMemoryTests`, and
`FluidStudiosMemoryTestsNoBacktrace` use utest;
other suites may retain their existing runners. Third-party test suites are
out of scope.

- `TemporalCameraTests`
- `FsrUpscalerParamsTests`
- `BindlessPoolTests`
- `FileSearcherTests`
- `ResourceCacheTests`
- `FluidStudiosMemoryTests`
- `FluidStudiosMemoryTestsNoBacktrace`
- `HplMemoryManagerTests`
- `HplMemoryManagerDisabledFacadeTests`
- conditional `FsrShaderBlobTests`
- conditional `FsrVulkanLoaderTests`

This includes memory variants and both FSR projects without changing their
existing definitions, links, filters, or conditional behavior.

## Conventions

Each utest executable has exactly one runner: use `UTEST_MAIN()` in one
translation unit, or use `UTEST_STATE()` there and call
`utest_main(argc, argv)` from a custom `main`. Declarations and utest cases
must be at global scope, matching the runner's generated declarations;
putting them in an anonymous namespace can cause linker errors.

For the converted suites, run these from the repository root after building:

```sh
./build-premake/tests/Debug/BindlessPoolTests --list-tests
./build-premake/tests/Debug/BindlessPoolTests --filter=LRUCache.TailHitDoesNotOrphanSlots
./build-premake/tests/Debug/BindlessPoolTests --random-order=1234
./build-premake/tests/Debug/FsrVulkanLoaderTests --list-tests
./build-premake/tests/Debug/FsrVulkanLoaderTests --random-order=1234
./build-premake/tests/Debug/FileSearcherTests --list-tests
./build-premake/tests/Debug/FileSearcherTests --random-order=1234
./build-premake/tests/Debug/ResourceCacheTests --filter=ResourceCacheFixture.CheckCacheGenerations
```

Test names are `suite.name`; `--help` documents the other runner options.

`TemporalCameraTests` registers the `TemporalCamera`, `TemporalUpscalerPolicy`,
`DisplayDepthPolicy`, `TemporalReactiveMask`, and `WaterReflectionJitter` suites.
Its custom runner temporarily also executes the six legacy graphics math suites
(ray cone LOD, cube mip generation, block compression decode, BC block layout,
upload row pitch, and light grid culling), including when listing or filtering
utest cases. Both legacy failures and utest failures affect the exit status.

```sh
./build-premake/tests/Debug/TemporalCameraTests --list-tests
./build-premake/tests/Debug/TemporalCameraTests --filter='TemporalCamera.*'
./build-premake/tests/Debug/TemporalCameraTests --random-order=1234
```

Temporal migration validation (2026-09-12): Debug and Release (`NDEBUG`)
passed all 36 registered cases (7 camera, 7 upscaler policy, 6 display depth,
8 reactive mask, 8 reflection jitter), all six legacy suites, and the four
Python tests invoked by the existing Premake postbuild commands. Listing,
each of the five suite filters, the isolated multiplicative-tint case, and
random order with seed 1234 passed in both configurations.

Environment limitation: `premake5` was unavailable on `PATH`, and the existing
generated Makefile predated the vendored include path. Validation used that
Makefile's unchanged source list and postbuild commands with the include path
already specified by `premake/tests.lua` supplied explicitly:

```sh
make -C build-premake -f TemporalCameraTests.make config=debug CPPFLAGS=-I../tests/third_party/utest -j2
make -C build-premake -f TemporalCameraTests.make config=release CPPFLAGS=-I../tests/third_party/utest -j2
```

Fresh Premake generation was not verified in this environment.

The HPL memory standalone script also checks individual case filtering and
random order alongside its compiler and symbol audits. See
[`notes/hpl-memory-utest.md`](../notes/hpl-memory-utest.md) for the memory
fixture contracts and build-validation limitations.

The standalone Fluid Studios contract runner compiles `mmgr.c` as C with
`MMGR_TESTING`, then links the C++ adapter against the pinned local utest
header. It runs the debug/release × backtrace-on/off matrix, applies a timeout,
captures each executable's output in a per-case log, and runs an optional
AddressSanitizer/UndefinedBehaviorSanitizer probe when the compiler supports
it. Run it from the repository root:

```sh
tests/memory/run_fluid_studios_tests.sh
tests/memory/run_fluid_studios_tests.sh --list-tests
tests/memory/run_fluid_studios_tests.sh --filter='*FirstUse*'
tests/memory/run_fluid_studios_tests.sh --random-order=1234
```

The utest options are passed to every matrix executable, so a filter or a
fixed randomized order is checked consistently across all configurations.
Use `TEST_TIMEOUT=60` to increase the per-executable limit. A compile,
timeout, or test failure remains nonzero; failure output is printed before the
temporary logs are removed.

The allocator suite has 11 independently filterable fixture cases. First-use
concurrency runs in a fresh child executable before allocator initialization,
including when filtered or randomized. Each fixture owns a unique report
directory beneath `FLUID_STUDIOS_TEST_ROOT` (or the system temporary directory).
See [`notes/fluid-studios-utest.md`](../notes/fluid-studios-utest.md) for validation
results and platform limitations.

`UTEST_F_SETUP`, `UTEST_F`, and `UTEST_F_TEARDOWN` provide per-test fixtures.
Fixture types must contain plain, zero-initializable fields: pinned `UTEST_F`
zeroes each fixture with `memset`, so nontrivial C++ members with constructors
or destructors are unsafe. Keep RAII objects local to the test body. If setup
partly acquires resources, explicitly roll them back before a fatal assertion
or other setup failure; setup failure returns without calling teardown.
When setup succeeds, teardown runs after the body even if the body fails, so
use it to release or validate resources not owned by a test-body RAII object.
Fixtures must be independent.

The resource suites reset their fake filesystem before every case. Their 22
search scenarios and 3 cache scenarios preserve multi-step story transitions
within a case. Resource cache tests use real managed handles with CPU models
of aggregate keys; they do not validate real loaders or GPU transitions.

Use exact assertions for discrete values. `ASSERT_NEAR` and `EXPECT_NEAR` use
an absolute comparison (`|actual - expected| <= epsilon`) and reject NaN;
preserve the original predicate for relative or strict comparisons. During
future migrations preserve numeric tolerances exactly, including their
comparison semantics.

## Penumbra map viewer smoke checks

Build the Penumbra product, then close other Penumbra viewer instances.
The standalone GPU check creates original Collada/material/texture fixtures
in a temporary directory; it needs no retail assets. Pass the actual user
log path (the viewer logs it during startup):

```powershell
python tests/penumbra_map_smoke.py --exe build-premake/penumbra/Release/Penumbra.exe --log "D:\Documents\Penumbra Redux\penumbra.log"
```

It runs Y-up and Z-up maps through standard and ray-traced renderers,
checks material aliases, room hierarchy, helper exclusion, colliders,
HPL1 sound variants, particles, billboards and beams. It also verifies
bounded frame counts and a missing entrance failure, then checks that a
walking character lands on a floor and two rigid boxes fall and stack.
The longer physics run also requires live particles and an active sound
channel, using an original silent PCM fixture rather than retail audio.
Fixture file hashes detect writes into the source install. Ray tracing requires
supported GPU hardware. This test checks execution and loader results,
not image fidelity, and creates no game screenshots or converted assets.

Entity checks use generated models to load `Object`, `Item`, `Lamp`,
`Enemy` and `SwingDoor`, check falling bodies, a moving limited hinge,
HPL1 geometry-name material overrides and unchanged install file hashes:

```powershell
python tests/penumbra_entity_smoke.py --exe build-premake/penumbra/Release/Penumbra.exe --log "D:\Documents\Penumbra Redux\penumbra.log"
```

Optionally add `--test-install <Antumbra test-install directory>` to check
four animated skinned instances in `maptest0_anim.dae`, including active
animation clocks and changing bone positions on both renderers. The test
install is downloaded/built externally and must never be committed.

Area checks generate Y-up and Z-up maps with all eight types and a generic
area, and check names, scaled bounds, parent transforms, debug-enabled
rendering and unchanged source-file hashes on both renderers:

```powershell
python tests/penumbra_area_smoke.py --exe build-premake/penumbra/Release/Penumbra.exe --log "D:\Documents\Penumbra Redux\penumbra.log"
```

These integration scripts open game windows. Run them when desktop use is
appropriate; they are separate from the regular Python test runner.

## Penumbra headless script checks

This check opens no windows, initializes no graphics/audio, and uses no
desktop input. It exercises original AngelScript fixtures, timer callbacks,
variables, messages, inventory, map lifecycle and link transitions, as well
as compilation errors, runaway scripts and missing destination entrances:

```powershell
python tests/penumbra_script_smoke.py --exe build-premake/penumbra/Release/Penumbra.exe --log "D:\Documents\Penumbra Redux\penumbra.log"
```

Optionally pass `--game <Overture install>` to exercise the unchanged boat
script, simulated item pickup callbacks and the timed exit to the outside
map. This verifies script execution and the transition request, rather than
GPU rendering, physical interaction or live world replacement. Script-file
hashes detect writes into the install. Nothing derived from retail assets is
saved in the repository.

The runtime's `--script-check --script-steps N` executes fixed 60 Hz ticks.
`--script-action <tick:action:name>` schedules a headless event (`pickup`,
`interact`, `examine`, `dismiss` or `invoke`). The last two need no entity
name or use a zero-argument script function respectively. `--script-api-status
<absolute file>` writes the current function table. Regenerate the GPL ABI
declarations with `python scripts/gen_penumbra_script_api.py <Antumbra checkout>`
when deliberately updating the reference, then rebuild.
