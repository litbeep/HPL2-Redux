#!/usr/bin/env python3
"""Load every level of a Penumbra install in the penumbra product and report problems.

Milestone 6 of docs/penumbra-levels.md. For each map in the install's maps
directory (entrance: its first _start_ node, as Antumbra lists them), this
runs a short background render (--frames) and a headless script run
(--script-check), then gathers the errors, warnings, script stubs and loader
counts from the log into one Markdown report.

The report names the install's files and quotes the log; it is for local
use and must not be committed (AGENTS.md). By default it is written next to
the log, in the product's user directory.

  python scripts/penumbra_level_sweep.py --exe build-premake/penumbra/Release/Penumbra.exe \
      --log "<Documents>/Penumbra Redux/penumbra.log" --game "<Penumbra Overture install>"
"""
import argparse
from collections import defaultdict
from pathlib import Path
import re
import subprocess
import sys
import time

START = re.compile(r'<node\s[^>]*name="_start_([^"]+)"')
WORLD = re.compile(r"Penumbra world: (\d+) static meshes, (\d+) lights, (\d+) starts, (\d+) deferred nodes, (\d+) errors")
ENVIRONMENT = re.compile(r"Penumbra environment: (\d+) bodies, (\d+) sounds, (\d+) particles, (\d+) billboards, (\d+) beams")
ENTITY = re.compile(r"^Penumbra entity '")
AREA = re.compile(r"^Penumbra area '")
STUB = re.compile(r"Penumbra script stub: (\w+)")
PARTIAL = re.compile(r"Penumbra script '(\w+)': setup retained")
# Numbers and quoted instance names vary between levels; drop them so that
# the same problem in several levels is counted once.
NUMBERS = re.compile(r"\b\d+(\.\d+)?\b")
# Messages understood and harmless, reported apart from the problems.
KNOWN = [
    (re.compile(r"Deleting resource '.*' that still has \d+ reference"),
     "Shutdown: surface-effect sounds preloaded by physics materials are never released, "
     "and the engine frees resources before physics. Upstream behaviour; Amnesia does the same."),
    (re.compile(r"listed in resources\.cfg does not exist"),
     "The install's resources.cfg lists directories it doesn't ship."),
    (re.compile(r"missing optional surface effect"),
     "materials.cfg names impact/scrape effects the install doesn't ship; HPL1 also plays nothing."),
    (re.compile(r"no authored collider for body"),
     "The .ent has physics settings for a body with no collider in its model; HPL1 skips it too."),
    (re.compile(r"(Material|Texture) 'lambert\d+(-fx)?' was not found"),
     "Maya's default material on helper geometry, which has no texture in HPL1 either."),
    (re.compile(r"Penumbra script stub: |setup retained; area behavior pending"),
     "Script functions not implemented yet: listed below by function."),
    (re.compile(r"has an unauthored body; skipped"),
     "A joint in the model names a body with no collider; HPL1 skips it too."),
    (re.compile(r"seem to have multitexturing|No tex coords for geometry"),
     "Collada geometry with a second UV set, or none; the first set is used, as in HPL1."),
    (re.compile(r"Penumbra world loaded with \d+ failed nodes"),
     "Summary line; each failed node is reported on its own."),
    # Defects in Overture's own data, which fail the same way in HPL1. Named
    # one by one so that anything new still shows up as a problem.
    (re.compile(r"modern_mine_cardboardbox\.(mat|jpg)"),
     "Install data: modern_mine_cardboardbox has no material or texture; drawn untextured."),
    (re.compile(r"sampledColor\.mat"),
     "Install data: mining_room_book.dae's texture points at the artist's own disk."),
    (re.compile(r"kitchen_whisketbottle\.ent"),
     "Install data: a map references kitchen_whisketbottle.ent (sic), named 'NAME'; the file is kitchen_whiskeybottle."),
    (re.compile(r"dust_sust01\.ps"),
     "Install data: a map references dust_sust01.ps, which doesn't exist."),
    (re.compile(r"material '' for 'beam' missing"),
     "Install data: a beam mesh in worm_nest has no material; drawn untextured."),
]


# SetupLink("area", "map.dae", "start", ...), SetupLinkLoadText(...) and
# ChangeMap("map.dae", "start", ...) with literal arguments.
LINK = re.compile(r'\b(SetupLink|SetupLinkLoadText)\s*\(\s*"[^"]*"\s*,\s*"([^"]+)"\s*,\s*"([^"]*)"')
CHANGE_MAP = re.compile(r'\bChangeMap\s*\(\s*"([^"]+)"\s*,\s*"([^"]*)"')
COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)


def links(game):
    """Each map's script links: {map: [(target map, start)]}, from literal calls."""
    maps_dir = data_root(game) / "maps"
    found = {}
    for script in sorted(maps_dir.glob("*.hps"), key=lambda p: p.name.lower()):
        text = COMMENT.sub("", script.read_text(errors="replace"))
        targets = [(m[1], m[2]) for m in LINK.findall(text)] + list(CHANGE_MAP.findall(text))
        found[script.with_suffix(".dae").name.lower()] = targets
    return found


def link_report(game):
    """Broken links, and levels the boat cabin's links never reach."""
    maps_dir = data_root(game) / "maps"
    starts = {p.name.lower(): set(START.findall(p.read_text(errors="replace"))) for p in maps_dir.glob("*.dae")}
    graph = links(game)
    broken, fallbacks = [], []
    for source, targets in graph.items():
        for target, start in targets:
            target = target.lower()
            if target not in starts:
                broken.append(f"`{source}` links to missing map `{target}`")
            elif start not in starts[target]:
                # HPL1 (cPlayer::PlaceAt) then uses the level's first entrance.
                fallbacks.append(f"`{source}` links to `{target}` at missing entrance `{start}`; "
                                 "the player arrives at its first entrance, as in HPL1")
    first = min(starts) if starts else None
    reached, pending = set(), [first] if first else []
    while pending:
        level = pending.pop()
        if level in reached or level not in starts:
            continue
        reached.add(level)
        pending += [target.lower() for target, _ in graph.get(level, [])]
    return broken, fallbacks, sorted(set(starts) - reached), first


def known(line):
    for pattern, reason in KNOWN:
        if pattern.search(line):
            return reason
    return None


def data_root(game):
    return game / "redist" if (game / "redist" / "resources.cfg").exists() else game


def levels(game, only):
    maps = sorted((data_root(game) / "maps").glob("*.dae"), key=lambda p: p.name.lower())
    for path in maps:
        if only and not any(o.lower() in path.name.lower() for o in only):
            continue
        starts = list(dict.fromkeys(START.findall(path.read_text(errors="replace"))))
        yield path.name, starts[0] if starts else None


def run(command, log, timeout):
    begin = time.monotonic()
    try:
        result = subprocess.run(command, capture_output=True, timeout=timeout)
        code = result.returncode
    except subprocess.TimeoutExpired:
        code = "timeout"
    seconds = time.monotonic() - begin
    output = log.read_text(errors="replace") if log.exists() else ""
    return code, seconds, output


def problems(output):
    found = []
    for line in output.splitlines():
        if line.startswith(("ERROR:", "WARNING:", "FATAL ERROR:")):
            found.append(line.strip())
    return found


def key(line):
    return NUMBERS.sub("#", line)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--log", required=True, type=Path, help="The product's log file (it reports its path on startup)")
    parser.add_argument("--game", required=True, type=Path, help="Penumbra install")
    parser.add_argument("--report", type=Path, help="Report path (default: level-sweep.md next to the log)")
    parser.add_argument("--renderer", default="standard", choices=("standard", "raytraced"))
    parser.add_argument("--frames", type=int, default=60)
    parser.add_argument("--script-steps", type=int, default=600)
    parser.add_argument("--only", nargs="*", help="Only maps whose names contain one of these")
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    exe, game = args.exe.resolve(), args.game.resolve()
    report = args.report or args.log.parent / "level-sweep.md"

    rows, by_problem, stubs, partial = [], defaultdict(set), defaultdict(set), defaultdict(set)
    by_known = defaultdict(lambda: [0, set()])
    for name, start in levels(game, args.only):
        if start is None:
            rows.append((name, "-", "no _start_ node", "", "", "", ""))
            continue
        base = [str(exe), "--game", str(game), "--map", name, "--start", start]
        code, seconds, output = run(base + ["--renderer", args.renderer, "--frames", str(args.frames)], args.log, args.timeout)
        world, environment = WORLD.search(output), ENVIRONMENT.search(output)
        view_all = problems(output)
        view = [line for line in view_all if not known(line)]
        entities, areas = len([l for l in output.splitlines() if ENTITY.match(l)]), len([l for l in output.splitlines() if AREA.match(l)])
        rendered = f"Penumbra rendered {args.frames} frames" in output
        scode, sseconds, soutput = run(base + ["--script-check", "--script-steps", str(args.script_steps)], args.log, args.timeout)
        script_all = problems(soutput)
        script = [line for line in script_all if not known(line)]
        for line in view_all + script_all:
            reason = known(line)
            if reason:
                by_known[reason][0] += 1
                by_known[reason][1].add(name)
        for line in view:
            by_problem[("view", key(line))].add(name)
        for line in script:
            by_problem[("script", key(line))].add(name)
        for function in STUB.findall(soutput):
            stubs[function].add(name)
        for function in PARTIAL.findall(soutput):
            partial[function].add(name)
        summary = (f"{world[1]} meshes, {world[2]} lights, {entities} entities, {areas} areas, {world[4]} deferred, {world[5]} failed"
                   if world else "world did not load")
        if environment:
            summary += f"; {environment[1]} bodies, {environment[2]} sounds, {environment[3]} particles"
        view_state = f"exit {code}, {seconds:.0f}s" + ("" if rendered else ", no frames")
        script_state = f"exit {scode}, {sseconds:.0f}s" + (", PASS" if "Penumbra script check: PASS" in soutput else ", FAIL")
        rows.append((name, start, summary, view_state, f"{len(view)}", script_state, f"{len(script)}"))
        print(f"{name}: {view_state}, {len(view)} view problems; scripts {script_state}, {len(script)} problems", flush=True)

    lines = ["# Penumbra level sweep", "",
             f"Install: `{game}`  ", f"Renderer: {args.renderer}, {args.frames} frames; scripts: {args.script_steps} ticks", "",
             "Local report: it names the install's files. Never commit it.", "",
             "| Map | Start | Loaded | View | View problems | Scripts | Script problems |",
             "|---|---|---|---|---|---|---|"]
    lines += ["| " + " | ".join(row) + " |" for row in rows]
    broken, fallbacks, unreached, first = link_report(game)
    lines += ["", "## Travel between levels", "",
              "From literal SetupLink, SetupLinkLoadText and ChangeMap calls in the scripts.", ""]
    lines += [f"- Broken: {item}" for item in broken] or ["- Every link leads to an existing map."]
    lines += [f"- {item}" for item in fallbacks]
    lines += ([f"- Not reachable from `{first}`: " + ", ".join(f"`{m}`" for m in unreached)] if unreached
              else [f"- Every level is reachable from `{first}`."])
    lines += ["", "## Problems by frequency", "",
              "Numbers are replaced by `#` so that one problem in several levels is counted once.", ""]
    for (kind, text), maps in sorted(by_problem.items(), key=lambda item: (-len(item[1]), item[0])):
        sample = ", ".join(sorted(maps)[:4]) + (", ..." if len(maps) > 4 else "")
        lines.append(f"- **{len(maps)}** levels ({kind}): `{text}`  \n  {sample}")
    lines += ["", "## Known and harmless", "",
              "Not counted as problems above.", ""]
    for reason, (count, maps) in sorted(by_known.items(), key=lambda item: -item[1][0]):
        lines.append(f"- {count} messages in {len(maps)} levels: {reason}")
    lines += ["", "## Script functions still stubs", ""]
    for function, maps in sorted(stubs.items(), key=lambda item: (-len(item[1]), item[0])):
        lines.append(f"- `{function}`: {len(maps)} levels")
    lines += ["", "## Area setup retained, behaviour pending", ""]
    for function, maps in sorted(partial.items(), key=lambda item: (-len(item[1]), item[0])):
        lines.append(f"- `{function}`: {len(maps)} levels")
    report.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Report: {report}")
    failed = [row for row in rows if not row[3].startswith("exit 0") or not row[5].startswith("exit 0")]
    return 1 if failed or broken or unreached else 0


if __name__ == "__main__":
    sys.exit(main())
