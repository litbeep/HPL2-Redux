#!/usr/bin/env python3
"""GPU integration checks using original synthetic Collada, never retail assets.

Run after building Penumbra (close any other Penumbra instance first):
  python tests/penumbra_map_smoke.py --exe <Penumbra.exe> --log <penumbra.log>
An interactive window/device is required. Nothing is captured or converted.
"""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import wave
import copy
import xml.etree.ElementTree as ET


ALIASES = (
    "Diffuse", "DiffuseSpecular", "Bump", "BumpSpecular", "BumpColorSpecular",
    "Additive", "Alpha", "Modulative", "ModulativeX2", "Flat",
    "EnvironmentMapReflect", "Water",
)


def fixture(root, axis):
    (root / "resources.cfg").write_text('<Resources><Directory Path="/" /></Resources>')
    # A white 2x2 TGA created from scratch.
    (root / "white.tga").write_bytes(
        struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 2, 2, 24, 0)
        + b"\xff" * 12)
    for name in ALIASES:
        (root / f"{name}.mat").write_text(
            f'<Material><Main Type="{name}"/><TextureUnits>'
            '<Diffuse Type="2D" File="white.tga"/></TextureUnits></Material>')
    images = "".join(f'<image id="im{i}"><init_from>{name}.tga</init_from></image>'
                     for i, name in enumerate(ALIASES))
    effects = "".join(
        f'<effect id="fx{i}"><profile_COMMON>'
        f'<newparam sid="surface"><surface type="2D"><init_from>im{i}</init_from></surface></newparam>'
        '<newparam sid="sampler"><sampler2D><source>surface</source></sampler2D></newparam>'
        '<technique sid="standard"><lambert><diffuse><texture texture="sampler" texcoord="UV"/>'
        '</diffuse></lambert></technique></profile_COMMON></effect>' for i in range(len(ALIASES)))
    materials = "".join(f'<material id="mat{i}"><instance_effect url="#fx{i}"/></material>'
                        for i in range(len(ALIASES)))

    def instance(material=0):
        return ('<instance_geometry url="#quad"><bind_material><technique_common>'
                f'<instance_material symbol="material" target="#mat{material}"/>'
                '</technique_common></bind_material></instance_geometry>')

    nodes = "".join(f'<node id="mesh{i}" name="mesh{i}"><translate>{i*2} 0 0</translate>'
                    f'{instance(i)}</node>' for i in range(len(ALIASES)))
    # nodraw geometry and ref children must not leak into the static scene;
    # room/portal helper geometry must not be drawn either.
    nodes += f'<node id="hidden" name="hidden_nodraw">{instance()}</node>'
    nodes += f'<node id="ref" name="_ref_prop_1"><node id="refchild" name="refchild">{instance()}</node></node>'
    nodes += '<node id="collider" name="_collider_box"><instance_geometry url="#box"/></node>'
    nodes += f'<node id="portal" name="_portal1_room2">{instance()}</node>'
    # Original silent PCM fixture checks HPL1's numbered filename lookup.
    with wave.open(str(root / "tone1.wav"), "wb") as sound:
        sound.setparams((1, 2, 22050, 22050, "NONE", "not compressed"))
        sound.writeframes(bytes(44100))
    (root / "tone.snt").write_text('<SOUNDENTITY><MAIN MainSound="tone" StartSound="tone1.wav"/><PROPERTIES Loop="true" Stream="false" Use3D="false"/></SOUNDENTITY>')
    (root / "fixture.ps").write_text('<ParticleSystem><ParticleEmitter Name="fixture" Material="Additive.mat" MaterialNum="1" MaxParticleNum="8" Respawn="true" ParticlesPerSecond="4" MinLifeSpan="1" MaxLifeSpan="1"/></ParticleSystem>')
    (root / "fixture.bnt").write_text('<BILLBOARD><MAIN Type="point" Material="Additive.mat" UseOffset="false" IsHalo="false"/></BILLBOARD>')
    (root / "fixture.beam").write_text('<BEAM><MAIN Material="Additive.mat" Size="0.1 1"/></BEAM>')
    nodes += '<node id="sound" name="_sound_tone_test"/>'
    nodes += '<node id="particles" name="_ps_fixture_test"><translate>0 1 -3</translate></node>'
    nodes += '<node id="billboard" name="_bb_fixture_test"><translate>0 0 -3</translate></node>'
    nodes += '<node id="beam" name="_beam_fixture_test"><translate>0 0 -3</translate></node>'
    nodes += '<node id="beamend" name="_beamend_test"><translate>0 1 -3</translate></node>'
    (root / "fixture.dae").write_text(f'''<COLLADA version="1.4.1">
      <asset><up_axis>{axis}</up_axis></asset>
      <library_images>{images}</library_images>
      <library_effects>{effects}</library_effects>
      <library_materials>{materials}</library_materials>
      <library_lights><light id="light"><technique_common><point><color>1 1 1</color></point></technique_common></light></library_lights>
      <library_geometries><geometry id="quad"><mesh>
        <source id="pos"><float_array count="12">-1 -1 -3 1 -1 -3 1 1 -3 -1 1 -3</float_array>
          <technique_common><accessor count="4" stride="3"><param name="X"/><param name="Y"/><param name="Z"/></accessor></technique_common></source>
        <source id="norm"><float_array count="3">0 0 1</float_array>
          <technique_common><accessor count="1" stride="3"><param name="X"/><param name="Y"/><param name="Z"/></accessor></technique_common></source>
        <source id="uv"><float_array count="8">0 0 1 0 1 1 0 1</float_array>
          <technique_common><accessor count="4" stride="2"><param name="S"/><param name="T"/></accessor></technique_common></source>
        <vertices id="vertices"><input semantic="POSITION" source="#pos"/></vertices>
        <triangles count="2" material="material"><input semantic="VERTEX" source="#vertices" offset="0"/>
          <input semantic="NORMAL" source="#norm" offset="1"/><input semantic="TEXCOORD" source="#uv" offset="2"/>
          <p>0 0 0 1 0 1 2 0 2 0 0 0 2 0 2 3 0 3</p></triangles>
      </mesh></geometry><geometry id="box"><mesh>
        <source id="boxpos"><float_array count="24">-1 -1 -1 1 -1 -1 1 1 -1 -1 1 -1 -1 -1 1 1 -1 1 1 1 1 -1 1 1</float_array>
          <technique_common><accessor count="8" stride="3"><param name="X"/><param name="Y"/><param name="Z"/></accessor></technique_common></source>
        <vertices id="boxvertices"><input semantic="POSITION" source="#boxpos"/></vertices>
        <source id="boxnorm"><float_array count="3">0 1 0</float_array><technique_common>
          <accessor count="1" stride="3"><param name="X"/><param name="Y"/><param name="Z"/></accessor></technique_common></source>
        <source id="boxuv"><float_array count="2">0 0</float_array><technique_common>
          <accessor count="1" stride="2"><param name="S"/><param name="T"/></accessor></technique_common></source>
        <triangles count="12"><input semantic="VERTEX" source="#boxvertices" offset="0"/><input semantic="NORMAL" source="#boxnorm" offset="1"/><input semantic="TEXCOORD" source="#boxuv" offset="2"/>
          <p>{' '.join(f'{v} 0 0' for v in '0 2 1 0 3 2 4 5 6 4 6 7 0 1 5 0 5 4 3 7 6 3 6 2 0 4 7 0 7 3 1 2 6 1 6 5'.split())}</p></triangles>
      </mesh></geometry></library_geometries>
      <library_visual_scenes><visual_scene id="scene"><node id="room" name="_room1"><translate>2 0 0</translate>
        {nodes}
        <node id="start" name="_start_Entry"><translate>3 0 -4</translate></node>
        <node id="lamp" name="dynamic_lamp"><translate>0 2 0</translate><scale>8 1 0</scale><instance_light url="#light"/></node>
      </node></visual_scene></library_visual_scenes>
      <scene><instance_visual_scene url="#scene"/></scene></COLLADA>''')
    # The reference helper is now an actual entity; its child geometry must
    # still be excluded from the map's static meshes.
    model = ET.parse(root / "fixture.dae")
    model.find("library_geometries").remove(model.find("library_geometries/geometry[@id='box']"))
    model_scene = model.find("./library_visual_scenes/visual_scene")
    model_scene.clear()
    model_scene.set("id", "scene")
    node = ET.SubElement(model_scene, "node", id="prop", name="prop")
    node.append(ET.fromstring(instance()))
    model.write(root / "prop.dae")
    (root / "prop.ent").write_text('<ENTITY><MAIN Type="Object"/><GRAPHICS ModelFile="prop"/><GAME Fixture="kept"/></ENTITY>')


def contents(root):
    return {str(f.relative_to(root)): hashlib.sha256(f.read_bytes()).hexdigest()
            for f in root.rglob("*") if f.is_file()}


def run(exe, log, root, renderer, start="Entry", success=True):
    if log.exists():
        previous = log.stat().st_mtime_ns
    else:
        previous = None
    result = subprocess.run([str(exe), "--game", str(root), "--map", "fixture.dae",
                             "--start", start, "--renderer", renderer, "--frames", "8"],
                            cwd=exe.parent, timeout=60, capture_output=True)
    assert result.returncode == (0 if success else 1), result.stderr.decode(errors="replace")
    assert log.exists() and log.stat().st_mtime_ns != previous, "No new startup log"
    output = log.read_text(errors="replace")
    assert "Penumbra exited cleanly" in output, output[-4000:]
    if success:
        assert "ERROR:" not in output, output[-6000:]
        assert f"Penumbra world: {len(ALIASES)} static meshes, 1 lights, 1 starts, 0 deferred nodes, 0 errors" in output
        assert "Penumbra environment: 14 bodies, 1 sounds, 1 particles, 1 billboards, 1 beams" in output
        assert "Penumbra rendered 8 frames" in output
        match = re.search(r"Penumbra camera: ([^,]+),", output)
        return [float(x) for x in match.group(1).split(":")]
    assert "Couldn't load map or start" in output
    assert "Penumbra rendered" not in output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--log", required=True, type=Path)
    args = parser.parse_args()
    exe, log = args.exe.resolve(), args.log.resolve()
    with tempfile.TemporaryDirectory(prefix="penumbra-smoke-") as temp:
        root = Path(temp)
        for axis, expected in [("Y_UP", [5, 1.7, -4]), ("Z_UP", [-5, -2.3, 0])]:
            fixture(root, axis)
            before = contents(root)
            for renderer in ("standard", "raytraced"):
                position = run(exe, log, root, renderer)
                assert all(abs(a-b) < 1e-4 for a, b in zip(position, expected)), position
                assert contents(root) == before, "Viewer modified its source install"
                print(f"PASS {axis} {renderer}: aliases, hierarchy, helper filtering, frames, read-only install")
        run(exe, log, root, "standard", start="Missing", success=False)
        print("PASS missing entrance fails before entering the render loop")
        fixture(root, "Y_UP")
        document = ET.parse(root / "fixture.dae")
        scene = document.find("./library_visual_scenes/visual_scene/node")
        scene.find("node[@name='_start_Entry']/translate").text = "0 2 0"
        ET.SubElement(scene.find("node[@name='_collider_box']"), "scale").text = "10 0.5 10"
        for name, height in [("lower", 5), ("upper", 8)]:
            node = ET.SubElement(scene, "node", id=name, name=f"_ref_fixturebox_{name}")
            ET.SubElement(node, "translate").text = f"3 {height} 0"
        document.write(root / "fixture.dae")
        model = copy.deepcopy(document)
        geometries = model.find("library_geometries")
        geometries.remove(geometries.find("geometry[@id='quad']"))
        collider = copy.deepcopy(geometries.find("geometry[@id='box']"))
        for element in collider.iter():
            for key, value in list(element.attrib.items()):
                element.set(key, value.replace("box", "colliderbox"))
        geometries.append(collider)
        model_scene = model.find("./library_visual_scenes/visual_scene")
        model_scene.clear()
        model_scene.set("id", "scene")
        rendered = ET.SubElement(model_scene, "node", id="model", name="model")
        geometry = ET.SubElement(rendered, "instance_geometry", url="#box")
        material = ET.SubElement(ET.SubElement(geometry, "bind_material"), "technique_common")
        ET.SubElement(material, "instance_material", symbol="material", target="#mat0")
        helper = ET.SubElement(model_scene, "node", id="modelcollider", name="_collider_box")
        ET.SubElement(helper, "instance_geometry", url="#colliderbox")
        model.write(root / "fixturebox.dae")
        (root / "fixturebox.ent").write_text('<ENTITY><MAIN Type="Object"/><GRAPHICS ModelFile="fixturebox"/><PHYSICS Collides="true" HasPhysics="true" Mass="1" Material="Default"/></ENTITY>')
        (root / "materials.cfg").write_text('<Materials><Material Name="Default" Elasticity="0" KineticFriction="0.5" StaticFriction="0.5"/></Materials>')
        before = contents(root)
        result = subprocess.run([str(exe), "--game", str(root), "--map", "fixture.dae", "--start", "Entry", "--walk", "--renderer", "standard", "--frames", "1800"], timeout=60)
        output = log.read_text(errors="replace")
        assert result.returncode == 0 and "ERROR:" not in output, output[-8000:]
        feet = re.search(r"Penumbra player feet: ([^,]+), grounded 1", output)
        assert feet, "Character did not land on the collider floor"
        assert abs(float(feet.group(1).split(":")[1])-0.5) < 0.08, feet.group(1)
        heights = {}
        for name, position in re.findall(r"Penumbra rigid body '[^']*' of '(\w+)': ([^\r\n]+)", output):
            heights[name] = float(position.split(":")[1])
        assert abs(heights["lower"]-1.5) < 0.15 and abs(heights["upper"]-3.5) < 0.2, heights
        runtime = re.search(r"Penumbra runtime: (\d+) live particles, (\d+) active main sounds", output)
        assert runtime and int(runtime.group(1)) > 0 and int(runtime.group(2)) == 1, "Environment did not simulate/play"
        assert contents(root) == before, "Physics viewer modified its source install"
        print("PASS character lands; boxes stack; particles simulate; sound plays; source files unchanged")


if __name__ == "__main__":
    main()
