#!/usr/bin/env python3
"""Compile-and-link capability probes for PunPun-supported toolchains."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


PROFILES = {
    "gcc": (["gcc"], ["g++"], False),
    "clang": (["clang"], ["clang++"], False),
    "zig": (["zig", "cc"], ["zig", "c++"], False),
    "mingw": (["x86_64-w64-mingw32-gcc"], ["x86_64-w64-mingw32-g++"], True),
}


def execute(command, cwd):
    process=subprocess.run(command,cwd=cwd,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    return {"ok":process.returncode==0,"command":command,"stdout":process.stdout[-2000:],"stderr":process.stderr[-2000:]}


def available(command):
    return bool(command and shutil.which(command[0]))


def probe(name, cc, cxx, cross):
    result={"profile":name,"available":available(cc) and available(cxx),"capabilities":{}}
    if not result["available"]: return result
    with tempfile.TemporaryDirectory(prefix=f"punpun-probe-{name}-") as td:
        root=Path(td)
        (root/"probe.c").write_text("#include <stdint.h>\nint main(void){return (int)(INT64_C(40)+2-42);}\n")
        (root/"probe.cpp").write_text("#include <cstdint>\nextern \"C\" std::int64_t value(){return 42;}\n")
        (root/"probe.S").write_text(".text\n.globl pp_probe_asm\npp_probe_asm:\n  ret\n.section .note.GNU-stack,\"\",@progbits\n")
        suffix=".exe" if cross else ""
        result["capabilities"]["c17_link"] = execute([*cc,"-std=c17",str(root/"probe.c"),"-o",str(root/("probe"+suffix))],root)
        result["capabilities"]["cxx20_object"] = execute([*cxx,"-std=c++20","-c",str(root/"probe.cpp"),"-o",str(root/"probe-cpp.o")],root)
        result["capabilities"]["assembly_object"] = execute([*cc,"-c",str(root/"probe.S"),"-o",str(root/"probe-asm.o")],root)
        if not cross and result["capabilities"]["c17_link"]["ok"]:
            result["capabilities"]["native_execution"] = execute([str(root/"probe")],root)
        for linker in ("lld","mold"):
            if shutil.which(linker) or shutil.which("ld."+linker):
                result["capabilities"]["linker_"+linker] = execute(
                    [*cc,f"-fuse-ld={linker}",str(root/"probe.c"),"-o",str(root/("probe-"+linker+suffix))],root)
    result["ok"] = all(item["ok"] for item in result["capabilities"].values())
    return result


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("profiles",nargs="*",choices=sorted(PROFILES),default=[])
    parser.add_argument("--output",type=Path)
    args=parser.parse_args()
    selected=args.profiles or list(PROFILES)
    results=[probe(name,*PROFILES[name]) for name in selected]
    payload={"schema":1,"results":results}
    text=json.dumps(payload,indent=2)+"\n"
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(text,encoding="utf-8")
    else: print(text,end="")
    if args.profiles and any(not item.get("ok",False) for item in results): raise SystemExit(1)


if __name__=="__main__": main()
