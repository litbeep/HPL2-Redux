#!/usr/bin/env python3
"""Validate original synthetic HPL1 area bounds without retail assets."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from penumbra_map_smoke import fixture, contents

TYPES = ("script", "link", "save", "ladder", "damage", "force", "liquid", "stick")


def create_areas(root, axis):
    fixture(root, axis)
    document = ET.parse(root / "fixture.dae")
    scene = document.find("./library_visual_scenes/visual_scene/node")
    # The room translates its rotated children; the loader must retain
    # that hierarchy rather than treating area positions as world coordinates.
    for index, area_type in enumerate(TYPES):
        node = ET.SubElement(scene, "node", id=f"area{index}", name=f"_area_{area_type}_named_area_{index}")
        ET.SubElement(node, "translate").text = "0 2 0"
        ET.SubElement(node, "rotate").text = "0 1 0 45"
        ET.SubElement(node, "scale").text = "1 2 3"
        ET.SubElement(node, "instance_geometry", url="#box")
    generic = ET.SubElement(scene, "node", id="generic", name="_area_Generic")
    ET.SubElement(generic, "instance_geometry", url="#box")
    document.write(root / "fixture.dae")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--log", required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="penumbra-area-smoke-") as temp:
        root = Path(temp)
        for axis, expected_size, expected_position in (("Y_UP", [2,4,6], [2,2,0]), ("Z_UP", [2,6,4], [-2,0,2])):
            create_areas(root, axis)
            before = contents(root)
            for renderer in ("standard", "raytraced"):
                result = subprocess.run([str(args.exe.resolve()), "--game", str(root), "--map", "fixture.dae",
                                         "--start", "Entry", "--renderer", renderer, "--frames", "24", "--debug-areas"],
                                        timeout=60, capture_output=True)
                output = args.log.read_text(errors="replace")
                assert result.returncode == 0 and "ERROR:" not in output, output[-8000:]
                areas = re.findall(r"Penumbra area '([^']+)': type '([^']*)', size ([^,]+), position ([^\r\n]+)", output)
                assert len(areas) == 9, areas
                for index, area_type in enumerate(TYPES):
                    name, found_type, size, position = areas[index]
                    assert (name,found_type) == (f"named_area_{index}",area_type), areas[index]
                    assert all(abs(float(a)-b)<0.001 for a,b in zip(size.split(":"),expected_size)), size
                    assert all(abs(float(a)-b)<0.001 for a,b in zip(position.split(":"),expected_position)), position
                assert areas[-1][0:2] == ("generic", ""), areas[-1]
                assert "Penumbra rendered 24 frames" in output
                assert contents(root) == before, "Area loader wrote into the source install"
                print(f"PASS {axis} {renderer}: eight area types, generic area, names, bounds, hierarchy, read-only install", flush=True)


if __name__ == "__main__":
    main()
