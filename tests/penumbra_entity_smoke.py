#!/usr/bin/env python3
"""Exercise HPL1 entity types and hinges using generated, original assets."""
import argparse
import copy
from pathlib import Path
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET

from penumbra_map_smoke import fixture, contents


def create_entities(root):
    fixture(root, "Y_UP")
    document = ET.parse(root / "fixture.dae")
    scene = document.find("./library_visual_scenes/visual_scene/node")
    ET.SubElement(scene.find("node[@name='_collider_box']"), "scale").text = "20 0.5 20"
    model = copy.deepcopy(document)
    geometry = model.find("library_geometries")
    geometry.remove(geometry.find("geometry[@id='quad']"))
    box = geometry.find("geometry[@id='box']")
    box.set("name", "BoxShape")
    collider = copy.deepcopy(box)
    for element in collider.iter():
        for key, value in list(element.attrib.items()):
            element.set(key, value.replace("box", "collider"))
    geometry.append(collider)
    model_scene = model.find("./library_visual_scenes/visual_scene")
    model_scene.clear()
    model_scene.set("id", "scene")
    rendered = ET.SubElement(model_scene, "node", id="Box", name="Box")
    ET.SubElement(rendered, "translate").text = "1 0 0"
    instance = ET.SubElement(rendered, "instance_geometry", url="#box")
    bindings = ET.SubElement(ET.SubElement(instance, "bind_material"), "technique_common")
    ET.SubElement(bindings, "instance_material", symbol="material", target="#mat0")
    helper = ET.SubElement(model_scene, "node", id="collider", name="_collider_box")
    ET.SubElement(helper, "translate").text = "1 0 0"
    ET.SubElement(helper, "instance_geometry", url="#collider")
    model.write(root / "plain.dae")
    joint = ET.SubElement(model_scene, "node", id="hinge", name="_joint_hinge_Box_null_90_90_nocollide_Hinge")
    ET.SubElement(joint, "rotate").text = "1 0 0 90"
    model.write(root / "hinged.dae")
    for index, entity_type in enumerate(("Object", "Item", "Lamp", "Enemy", "SwingDoor")):
        name = entity_type.lower()
        hinged = entity_type == "SwingDoor"
        (root / f"{name}.ent").write_text(
            f'<ENTITY><MAIN Type="{entity_type}"/><GRAPHICS ModelFile="{"hinged" if hinged else "plain"}"/>'
            '<SUBMESH Name="BoxShape" MaterialFile="Diffuse"/>'
            f'<PHYSICS SubName="{"BoxShape" if hinged else ""}" Collides="true" HasPhysics="true" Mass="1" Material="Default"/>'
            '<JOINT Name="Hinge" MinValue="90" MaxValue="90"/>'
            '<GAME OriginalData="retained"/></ENTITY>')
        node = ET.SubElement(scene, "node", id=name, name=f"_ref_{name}_{name}")
        ET.SubElement(node, "translate").text = f"{index*4-8} 5 0"
    document.write(root / "fixture.dae")
    (root / "materials.cfg").write_text('<Materials><Material Name="Default" Elasticity="0" KineticFriction="0.5" StaticFriction="0.5"/></Materials>')


def run(exe, log, root, frames, renderer):
    result = subprocess.run([str(exe), "--game", str(root), "--map", "fixture.dae",
                             "--start", "Entry", "--renderer", renderer,
                             "--frames", str(frames)], timeout=60, capture_output=True)
    output = log.read_text(errors="replace")
    assert result.returncode == 0 and "ERROR:" not in output, output[-8000:]
    assert "Penumbra exited cleanly" in output
    for name in ("object", "item", "lamp", "enemy", "swingdoor"):
        assert f"Penumbra entity '{name}':" in output
    assert re.search(r"'swingdoor':.*1 bodies, 1 joints", output)
    angle = float(re.search(r"Penumbra joint 'swingdoor_Hinge': type \d+, angle ([^,]+)", output)[1])
    heights = {name: float(position.split(":")[1]) for name, position in
               re.findall(r"Penumbra rigid body '[^']*' of '(\w+)': ([^\r\n]+)", output)}
    return angle, heights


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--test-install", type=Path, help="Optional Antumbra CC test install for skinned animation checks")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="penumbra-entity-smoke-") as temp:
        root = Path(temp)
        create_entities(root)
        before = contents(root)
        for renderer in ("standard", "raytraced"):
            early, _ = run(args.exe.resolve(), args.log, root, 8, renderer)
            late, heights = run(args.exe.resolve(), args.log, root, 600, renderer)
            assert abs(late-early) > 0.1 and abs(late) <= 1.7, (early, late)
            for name in ("object", "item", "lamp", "enemy"):
                assert abs(heights[name]-1.5) < 0.15, heights
            assert contents(root) == before, "Entity loader modified the install"
            print(f"PASS {renderer}: five entity types, material override, falling props, moving hinge, read-only install", flush=True)
        # An entity that fails part-way (after its mesh entity exists) is
        # removed again, and the level still loads without it, as in HPL1.
        (root / "broken.ent").write_text('<ENTITY><MAIN Type="Object"/><GRAPHICS ModelFile="plain"/>'
                                         '<SUBMESH Name="NoSuchSubmesh" MaterialFile="Diffuse"/></ENTITY>')
        document = ET.parse(root / "fixture.dae")
        scene = document.find("./library_visual_scenes/visual_scene/node")
        ET.SubElement(ET.SubElement(scene, "node", id="broken", name="_ref_broken_broken"), "translate").text = "0 5 4"
        document.write(root / "fixture.dae")
        result = subprocess.run([str(args.exe.resolve()), "--game", str(root), "--map", "fixture.dae",
                                 "--start", "Entry", "--renderer", "standard", "--frames", "8"],
                                timeout=60, capture_output=True)
        output = args.log.read_text(errors="replace")
        assert result.returncode == 0 and "Penumbra exited cleanly" in output, output[-8000:]
        assert "Penumbra world loaded with 1 failed nodes" in output, output[-8000:]
        assert "Penumbra entity 'broken':" not in output
        for name in ("object", "item", "lamp", "enemy", "swingdoor"):
            assert f"Penumbra entity '{name}':" in output
        print("PASS a failed entity is removed and the level loads without it", flush=True)
    if args.test_install:
        before = contents(args.test_install)
        for renderer in ("standard", "raytraced"):
            samples = []
            for frames in (8, 120):
                result = subprocess.run([str(args.exe.resolve()), "--game", str(args.test_install.resolve()),
                                         "--map", "maptest0_anim.dae", "--start", "Start1",
                                         "--renderer", renderer, "--frames", str(frames)],
                                        timeout=60, capture_output=True)
                output = args.log.read_text(errors="replace")
                assert result.returncode == 0 and "ERROR:" not in output, output[-8000:]
                states = re.findall(r"Penumbra animation '([^']+)': time ([^,]+), active 1", output)
                assert len(states) == 4 and all(float(time) > 0 for _, time in states), states
                bones = dict(re.findall(r"Penumbra animated bone '([^']+)': ([^\r\n]+)", output))
                assert bones, "No skeletal animation diagnostics"
                samples.append(bones)
            assert samples[0].keys() == samples[1].keys()
            assert any(samples[0][bone] != samples[1][bone] for bone in samples[0]), "Animated bones did not move"
            assert contents(args.test_install) == before, "Animation loader modified the test install"
            print(f"PASS {renderer}: four skinned instances animate; source install unchanged", flush=True)


if __name__ == "__main__":
    main()
