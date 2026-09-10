#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile
import time
import zipfile

from versioning import PKGVER, VERSION

ROOT = Path(__file__).resolve().parents[1]


def run(cmd, *, cwd=None, env=None, check=True, capture=True):
    return subprocess.run(
        [str(x) for x in cmd], cwd=cwd, env=env, check=check, text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
    )


def median_ms(cmd, *, cwd=None, env=None, rounds=5):
    samples=[]
    for _ in range(rounds):
        start=time.perf_counter_ns()
        proc=run(cmd,cwd=cwd,env=env,check=True)
        end=time.perf_counter_ns()
        samples.append((end-start)/1_000_000.0)
    return statistics.median(samples), samples


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def validate_vsix(vsix: Path):
    require(vsix.is_file(), f"missing VSIX: {vsix}")
    with zipfile.ZipFile(vsix) as zf:
        names=set(zf.namelist())
        required={
            "extension/package.json",
            "extension/extension.js",
            "extension/syntaxes/punpun.tmLanguage.json",
            "extension/assets/punpun-icon-128.png",
        }
        missing=sorted(required-names)
        require(not missing, f"VSIX missing files: {missing}")
        package=json.loads(zf.read("extension/package.json"))
        languages=package.get("contributes",{}).get("languages",[])
        require(any(".pp" in item.get("extensions",[]) for item in languages), "VSIX does not register .pp")
        require(package.get("version") == VERSION, f"VSIX version is not {VERSION}")
    return "manifest, compiler-native LSP client, icon, grammar and .pp registration verified"


def list_tar(path: Path):
    proc=run(["tar","--zstd","-tf",path])
    return proc.stdout.splitlines()


def validate_arch_package(pkg: Path):
    require(pkg.is_file(), f"missing Arch package: {pkg}")
    names=list_tar(pkg)
    expected=["./.PKGINFO","./usr/bin/pp","./usr/bin/ppx","./usr/bin/punpun-lsp","./usr/lib/punpun/bin/ppc"]
    for name in expected:
        require(name in names, f"Arch package missing {name}")
    return "package payload and .PKGINFO verified; pacman execution unavailable on this host"


def validate_windows_source():
    wxs=ROOT/"installers/windows/wix/PunPun.wxs"
    bundle=ROOT/"installers/windows/wix/Bundle.wxs"
    build=ROOT/"installers/windows/build.ps1"
    for path in (wxs,bundle,build): require(path.is_file(),f"missing Windows installer source: {path}")
    # XML parse is a useful host-independent validation. WiX itself is not present on Linux.
    import xml.etree.ElementTree as ET
    ET.parse(wxs); ET.parse(bundle)
    text=wxs.read_text(encoding="utf-8")
    require(".pp" in text and "PATH" in text, "WiX source lacks file association or PATH configuration")
    return "WiX MSI/Burn sources parse as XML; not built or executed because WiX/Windows is unavailable"


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("release",type=Path)
    args=ap.parse_args()
    release=args.release.resolve()
    installer=release/f"PunPun-{VERSION}-Linux-x86_64-Installer.run"
    vsix=release/f"punpun-vscode-{VERSION}.vsix"
    arch=release/f"punpun-{PKGVER}-1-x86_64.pkg.tar.zst"
    sdk_tar=release/f"PunPun-{VERSION}-linux-x86_64.tar.zst"

    results=[]
    results.append(("VSIX", "PASS", validate_vsix(vsix)))
    require(sdk_tar.is_file(),f"missing SDK tar: {sdk_tar}")
    sdk_names=list_tar(sdk_tar)
    require(any(x.endswith("/bin/ppc") for x in sdk_names),"portable SDK lacks compiler")
    require(any(x.endswith("/bin/ppc-self") for x in sdk_names),"portable SDK lacks self-hosted compiler")
    require(any(x.endswith("/selfhost/ppc_self.pp") for x in sdk_names),"portable SDK lacks self-hosted source")
    require(any(x.endswith("/VERSION") for x in sdk_names),"portable SDK lacks canonical version marker")
    require(any(x.endswith("/spec/0.6/ownership.md") for x in sdk_names),"portable SDK lacks 0.6 language specification")
    results.append(("Portable Linux SDK", "PASS", "tar.zst contents and both compiler stages verified"))
    results.append(("Arch package", "PARTIAL", validate_arch_package(arch)))
    results.append(("Windows installers", "HOST-LIMITED", validate_windows_source()))

    with tempfile.TemporaryDirectory(prefix="punpun-release-validation-") as td:
        td=Path(td)
        home=td/"home"; prefix=td/"prefix"; home.mkdir(); prefix.mkdir()
        env=os.environ.copy(); env["HOME"]=str(home); env["PUNPUN_PREFIX"]=str(prefix)
        env["PATH"]=str(prefix/"bin")+os.pathsep+env.get("PATH","")
        install=run(["sh",installer],env=env)
        require("Installed PunPun" in install.stdout, f"installer did not report success: {install.stdout}\n{install.stderr}")
        pp=prefix/"bin/pp"; ppx=prefix/"bin/ppx"; uninstall=prefix/"bin/punpun-uninstall"
        ppc_self=prefix/"share/punpun/bin/ppc-self"
        for path in (pp,ppx,ppc_self,uninstall): require(path.is_file(),f"installer missing {path.name}")
        version=run([pp,"--version"],env=env).stdout.strip()
        require(VERSION in version,f"installed pp has wrong version: {version}")
        require(VERSION in run([ppx,"--version"],env=env).stdout,"installed ppx has wrong version")

        project=td/"project"; project.mkdir()
        run([pp,"init","releasecheck"],cwd=project,env=env)
        source=project/"src/main.pp"
        source.write_text('launch { say("one"); }\n',encoding="utf-8")
        first=run([pp,"run"],cwd=project,env=env).stdout
        require("one" in first,"installed pp run did not execute initial source")
        old_stat=source.stat()
        source.write_text('launch { say("two"); }\n',encoding="utf-8")
        os.utime(source,ns=(old_stat.st_atime_ns,old_stat.st_mtime_ns))
        second=run([pp,"run"],cwd=project,env=env).stdout
        require("two" in second and "one" not in second,"installed SDK reproduced stale executable bug")

        # Installed compiler diagnostics must also work outside the repository.
        source.write_text('launch { sdds; }\n',encoding="utf-8")
        bad=run([pp,"check"],cwd=project,env=env,check=False)
        require(bad.returncode != 0 and "E0300" in bad.stderr,"installed compiler did not report E0300")
        source.write_text('launch { say("benchmark"); }\n',encoding="utf-8")

        version_med,_=median_ms([pp,"--version"],env=env)
        check_med,_=median_ms([pp,"check"],cwd=project,env=env)
        # Prime build cache, then measure no-change build and run.
        run([pp,"build"],cwd=project,env=env)
        build_med,_=median_ms([pp,"build"],cwd=project,env=env)
        run_med,_=median_ms([pp,"run"],cwd=project,env=env)

        async_src=(
            'async fn delayed(value: i64, delay: i64) -> i64 { sleep_ms(delay); return value; }\n'
            'launch { let a = delayed(20, 40); let b = delayed(22, 40); say(await a + await b); }\n'
        )
        source.write_text(async_src,encoding="utf-8")
        async_out=run([pp,"run"],cwd=project,env=env).stdout
        require("42" in async_out,"installed async example failed")

        source.write_text('launch { say(42); }\n',encoding="utf-8")
        native_out=run([pp,"run","--backend=native","--no-cache"],cwd=project,env=env).stdout
        require(native_out.strip()=="42","installed native backend failed")
        bytecode_out=run([pp,"run","--backend=bytecode"],cwd=project,env=env).stdout
        require(bytecode_out.strip()=="42","installed bytecode backend failed")
        results.append(("Installed backends", "PASS", "C, native x86-64, and bytecode builds passed outside the source tree"))

        results.append(("Linux installer", "PASS", "self-extractor installed into isolated HOME and native smoke test passed"))
        results.append(("Installed stale-build regression", "PASS", "one -> two rebuilt correctly even with preserved mtime"))
        results.append(("Installed diagnostics", "PASS", "unknown identifier produced compiler E0300 outside repository"))
        results.append(("Installed async", "PASS", "native async/await example produced 42"))

        self_c=td/"selfhost-hello.c"
        run([ppc_self,prefix/"share/punpun/selfhost/examples/hello.pp",self_c],env=env)
        require("pp_self_greeting" in self_c.read_text(encoding="utf-8"),"installed self-hosted compiler emitted invalid C")
        results.append(("Installed self-hosted compiler", "PASS", "installed PunPun-written compiler translated a source program"))
        print(f"measured performance (not embedded in reproducible report): pp --version {version_med:.2f} ms; check {check_med:.2f} ms; no-change build {build_med:.2f} ms; run {run_med:.2f} ms")
        results.append(("Installed performance", "MEASURED", "timing smoke completed; measurements are intentionally not embedded in reproducible artifacts"))

        un=run([uninstall],env=env)
        require(not (prefix/"share/punpun").exists(),"uninstaller left SDK directory")
        require(not pp.exists(),"uninstaller left pp wrapper")
        results.append(("Linux uninstall", "PASS", "SDK removed; project/cache locations preserved"))

    # Sites build from source on this host.
    for site in ("docs-site","ppx-site"):
        proc=run(["python3","build.py"],cwd=ROOT/site)
        require((ROOT/site/"dist/index.html").is_file(),f"{site} did not emit index.html")
        results.append((site,"PASS","production static build completed"))

    # Actual controlled first-party package / async HTTP tests are part of the suite; run the focused acceptance here too.
    run(["python3","compiler/tests/run_tests.py","--ppc","./build/ppc",
         "--backend","c","--backend","native","--backend","bytecode"],cwd=ROOT)
    run(["python3","scripts/abi_check.py","--ppc","./build/ppc"],cwd=ROOT)
    results.append(("Compiler + runtime acceptance","PASS","all compiler backends and the runtime ABI gate passed"))

    report=[f"# PunPun {VERSION} release validation", "", "Generated on the available Linux x86-64 release host.", "", "| Check | Status | Detail |", "|---|---|---|"]
    for name,status,detail in results:
        report.append(f"| {name} | {status} | {detail.replace('|','/')} |")
    report += ["", "## Host limitations", "", "- WiX and Windows are not available on this host, so no MSI or Windows setup executable is manufactured or claimed tested.", "- `pacman`/`makepkg` are not installed in this container. The Arch package payload is generated and inspected, but installation/removal through pacman must be validated on an Arch/CachyOS host.", "- VS Code CLI is not installed in this container. The VSIX is structurally validated and LSP protocol tests run independently.", ""]
    (release/"RELEASE_VALIDATION.md").write_text("\n".join(report),encoding="utf-8")
    print("release validation passed")
    for row in results: print(f"{row[1]:<12} {row[0]}: {row[2]}")

if __name__ == "__main__":
    main()
