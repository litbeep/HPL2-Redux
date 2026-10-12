#!/usr/bin/env python3
"""Extract the GPL HPL1 script ABI from an Antumbra source checkout."""
import argparse
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("antumbra", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
engine = args.antumbra / "third_party/scummvm/engines/hpl1"
rows = []
for source in (engine / "engine/game/ScriptFuncs.cpp", engine / "penumbra-overture/GameScripts.cpp"):
    for match in re.finditer(r"SCRIPT_DEFINE_FUNC(?:_\d+)?\(([^)]+)\)", source.read_text()):
        result, name, *types = [part.strip() for part in match[1].split(",")]
        declaration = f"{result} {name}({', '.join(types)})"
        rows.append((name, result, declaration))
assert len({row[0] for row in rows}) == len(rows)
output = root / "penumbra/src/game/ScriptApi.inc"
output.write_text("// Generated GPL HPL1 ABI; see scripts/gen_penumbra_script_api.py.\n" +
                  "\n".join(f'{{"{name}", "{result}", "{decl}"}},' for name, result, decl in rows) + "\n")
print(f"Generated {len(rows)} declarations")
