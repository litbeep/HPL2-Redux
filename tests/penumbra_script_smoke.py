#!/usr/bin/env python3
"""Headless AngelScript integration tests. Opens no windows and takes no desktop input."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile


def hashes(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob("*") if p.is_file()}


def run(exe, log, root, map_name="first.dae", actions=(), ticks=120, success=True):
    args=[str(exe),"--game",str(root),"--map",map_name,"--start","Entry",
          "--script-check","--script-steps",str(ticks)]
    for action in actions:
        args += ["--script-action",action]
    result=subprocess.run(args,capture_output=True,timeout=30)
    output=log.read_text(errors="replace")
    assert (result.returncode==0)==success, (result.returncode,output[-6000:])
    assert "Initializing Graphics Module" not in output, "Headless check initialized graphics"
    if success:
        assert "ERROR:" not in output and "Penumbra script check: PASS" in output, output
    return output


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe",required=True,type=Path)
    parser.add_argument("--log",required=True,type=Path)
    parser.add_argument("--game",type=Path,help="Optional Overture install for unchanged boat-script validation")
    args=parser.parse_args()
    exe=args.exe.resolve()
    with tempfile.TemporaryDirectory(prefix="penumbra-script-smoke-") as temp:
        root=Path(temp)
        (root/"resources.cfg").write_text('<Resources><Directory Path="/"/></Resources>')
        scene='<COLLADA><library_visual_scenes><visual_scene><node name="_start_Entry"/><node name="_area_link_exit"/></visual_scene></library_visual_scenes></COLLADA>'
        for name in ("first","second","bad","budget","missingstart","throws","after"):
            (root/f"{name}.dae").write_text(scene)
        (root/"first.hps").write_text('''
void Check(bool value) { if(!value) { int zero=0; int fail=1/zero; } }
void Timer(string name) {
 Check(name == "quoted\\\"timer");
 AddLocalVar("timer",1); DestroyTimer(name);
 AddMessage("Original fixture message"); SetMessagesOverCallback("Acknowledged");
}
void Acknowledged() { AddGlobalVar("ack",1); Print("ACK"); }
void OnStart() {
 CreateLocalVar("starts",0); AddLocalVar("starts",1);
 CreateLocalVar("timer",0); CreateGlobalVar("ack",0);
 GiveItem("fixturekey","unused.ent",-1);
 Check(HasItem("fixturekey"));
 CreateGameEntityVar("exit","value",4); AddGameEntityVar("exit","value",2);
 Check(GetGameEntityVar("exit","value")==6);
 Check(StringContains(IntToString(42),"42"));
 CreateTimer("quoted\\\"timer",0.05,"Timer",false);
 SetTimerPaused("quoted\\\"timer",true); AddTimerTime("quoted\\\"timer",0.05);
 Check(GetTimerTime("quoted\\\"timer")>0.09);
 SetupLink("exit","second.dae","Entry","","",0.05,0.05);
 Print("FIRST START");
}
void Resume() { SetTimerPaused("quoted\\\"timer",false); }
void OnLoad() { Print("FIRST LOAD"); }
void OnUpdate() { Check(GetLocalVar("starts")==1); }
void OnExit() { Check(GetLocalVar("timer")==1); Check(GetGlobalVar("ack")==1); Print("FIRST EXIT"); }
''')
        (root/"second.hps").write_text('''
void Check(bool value) { if(!value) { int zero=0; int fail=1/zero; } }
void OnStart() { Check(HasItem("fixturekey")); Check(GetGlobalVar("ack")==1); Print("SECOND START"); }
void OnLoad() { Print("SECOND LOAD"); }
void OnExit() { Print("SECOND EXIT"); }
''')
        (root/"bad.hps").write_text('void OnStart() { MissingFunction(); }')
        (root/"budget.hps").write_text('void OnStart() { while(true) { } }')
        (root/"throws.hps").write_text('void OnStart() { SetupLink("exit","after.dae","Entry","","",0,0); int zero=0; int fail=1/zero; }')
        (root/"after.hps").write_text('void OnStart() { Print("AFTER START"); }')
        (root/"missingstart.hps").write_text('void OnStart() { SetupLink("exit","second.dae","Missing","","",0,0); }')
        before=hashes(root)
        output=run(exe,args.log,root,actions=("10:invoke:Resume","30:dismiss","40:interact:exit"))
        for text in ("FIRST START","FIRST LOAD","ACK","FIRST EXIT","SECOND START","SECOND LOAD","SECOND EXIT","script transition complete: second.dae, start 'Entry'"):
            assert text in output, (text,output)
        print("PASS callbacks, paused/mutable timers, message acknowledgement, variables, inventory, links and lifecycle",flush=True)
        run(exe,args.log,root,"bad.dae",success=False)
        print("PASS script compilation errors fail the process",flush=True)
        output=run(exe,args.log,root,"budget.dae",success=False)
        assert "execution budget exceeded" in output,output
        print("PASS runaway scripts abort at the execution budget",flush=True)
        output=run(exe,args.log,root,"throws.dae",actions=("1:interact:exit",),success=False)
        assert output.count("ERROR:")==1 and "AFTER START" in output and "script transition complete: after.dae" in output,output
        print("PASS a failing script is reported without stopping later maps",flush=True)
        run(exe,args.log,root,"missingstart.dae",actions=("1:interact:exit",),success=False)
        print("PASS missing transition entrances fail",flush=True)
        assert before==hashes(root),"Script checks modified source files"
    if args.game:
        game=args.game.resolve()
        data=game/"redist" if (game/"redist/resources.cfg").exists() else game
        source_hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in (data/"maps").glob("*.hps")}
        command=[str(exe),"--game",str(game),"--map","level00_01_boat_cabin.dae","--start","link1","--script-check","--script-steps","1200"]
        for action in ("600:dismiss","610:pickup:flashlight","620:dismiss","630:pickup:notebook","640:dismiss","650:pickup:glowstick","660:dismiss","1100:interact:exitdoor"):
            command += ["--script-action",action]
        result=subprocess.run(command,capture_output=True,timeout=30)
        output=args.log.read_text(errors="replace")
        assert result.returncode==0 and "ERROR:" not in output,output
        assert "script transition complete: level01_01_outside.dae, start 'link1'" in output,output
        assert all(hashlib.sha256(p.read_bytes()).hexdigest()==value for p,value in source_hashes.items())
        print("PASS unchanged retail boat script, timed exit unlock, item callbacks and outside transition",flush=True)


if __name__=="__main__":
    main()
