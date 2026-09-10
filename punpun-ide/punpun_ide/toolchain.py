from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
import json, os, platform, shutil, subprocess, tarfile, urllib.request, zipfile

RELEASE_API = "https://api.github.com/repos/pumpumlang/punpun/releases/latest"

@dataclass(frozen=True)
class CommandPlan:
    argv: list[str]
    cwd: Path
    description: str

@dataclass(frozen=True)
class ReleaseAsset:
    name: str
    url: str
    size: int = 0

class Toolchain:
    def __init__(self, app_dir: Path | None = None):
        self.app_dir = app_dir or (Path.home()/".punpun-ide")
        self.private_bin = self.app_dir/"toolchain"/"current"/"bin"

    def env(self) -> dict[str, str]:
        env = os.environ.copy()
        if self.private_bin.exists():
            env["PATH"] = str(self.private_bin) + os.pathsep + env.get("PATH", "")
        return env

    def find(self, *names: str) -> str | None:
        env_path = self.env().get("PATH")
        for name in names:
            p = shutil.which(name, path=env_path)
            if p: return p
        return None

    def ppc(self) -> str | None:
        return self.find("ppc", "ppc.exe")

    def pp(self) -> str | None:
        return self.find("pp", "punpun", "pp.exe", "punpun.exe")

    def version(self) -> str | None:
        exe = self.ppc() or self.pp()
        if not exe: return None
        for args in ([exe, "--version"], [exe, "version"]):
            try:
                out = subprocess.run(args, capture_output=True, text=True, timeout=3, env=self.env())
                text=(out.stdout or out.stderr).strip()
                if text: return text.splitlines()[0]
            except Exception: pass
        return None

    def punpun_check(self, source: Path) -> CommandPlan:
        exe=self.ppc() or self.pp()
        if not exe: raise FileNotFoundError("PunPun 1.3 toolchain is not installed")
        if Path(exe).name.lower().startswith("ppc"):
            argv=[exe,"check","--json",str(source)]
        else:
            argv=[exe,"check",str(source)]
        return CommandPlan(argv, source.parent, "Check PunPun")

    def run_plan(self, source: Path) -> CommandPlan:
        from .filetypes import kind_for
        kind=kind_for(source)
        if kind.id == "punpun":
            exe=self.ppc() or self.pp()
            if not exe: raise FileNotFoundError("PunPun 1.3 toolchain is not installed")
            return CommandPlan([exe,"run",str(source)], source.parent, "Run PunPun")
        build_dir=source.parent/".punpun-ide"/"build"; build_dir.mkdir(parents=True, exist_ok=True)
        out=build_dir/(source.stem + (".exe" if os.name=="nt" else ""))
        if kind.id == "c": compiler=self.find("clang","gcc","cc")
        elif kind.id == "cpp": compiler=self.find("clang++","g++","c++")
        else: raise ValueError(f"{source.name} is not runnable")
        if not compiler: raise FileNotFoundError(f"No {kind.label} compiler found")
        return CommandPlan([compiler,str(source),"-O0","-o",str(out)], source.parent, f"Build {kind.label}")

    def native_output_for(self, source: Path) -> Path:
        return source.parent/".punpun-ide"/"build"/(source.stem + (".exe" if os.name=="nt" else ""))

    def debug_plan(self, source: Path) -> tuple[CommandPlan, list[str] | None]:
        from .filetypes import kind_for
        kind=kind_for(source)
        if kind.id == "punpun":
            pp=self.pp()
            if pp:
                return CommandPlan([pp,"debug",str(source)],source.parent,"Debug PunPun"), None
            ppc=self.ppc()
            if not ppc: raise FileNotFoundError("PunPun 1.3 toolchain is not installed")
            out=source.parent/".punpun-ide"/"build"/("debug-"+source.stem+(".exe" if os.name=="nt" else ""))
            out.parent.mkdir(parents=True,exist_ok=True)
            dbg=self.find("gdb","lldb")
            return CommandPlan([ppc,"build","-g","--keep","-o",str(out),str(source)],source.parent,"Build PunPun debug executable"), ([dbg,str(out)] if dbg else None)
        if kind.id not in ("c","cpp"): raise ValueError(f"{source.name} is not debuggable")
        out=self.native_output_for(source); out.parent.mkdir(parents=True,exist_ok=True)
        compiler=self.find("clang","gcc","cc") if kind.id=="c" else self.find("clang++","g++","c++")
        if not compiler: raise FileNotFoundError(f"No {kind.label} compiler found")
        dbg=self.find("gdb","lldb")
        return CommandPlan([compiler,str(source),"-g","-O0","-o",str(out)],source.parent,f"Build {kind.label} debug executable"), ([dbg,str(out)] if dbg else None)

def fetch_latest_release(timeout: int=10) -> dict:
    req=urllib.request.Request(RELEASE_API,headers={"User-Agent":"PunPun-IDE/0.1"})
    with urllib.request.urlopen(req,timeout=timeout) as r:
        return json.load(r)

def select_release_asset(release: dict, system: str | None=None, machine: str | None=None) -> ReleaseAsset | None:
    system=(system or platform.system()).lower(); machine=(machine or platform.machine()).lower()
    if machine not in {"x86_64","amd64","x64"}: return None
    wanted = "windows-x86_64.zip" if system=="windows" else "linux-x86_64.tar.gz" if system=="linux" else None
    if not wanted: return None
    for a in release.get("assets",[]):
        if a.get("name","").endswith(wanted):
            return ReleaseAsset(a["name"],a["browser_download_url"],int(a.get("size",0)))
    return None

def safe_extract(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True,exist_ok=True)
    base=destination.resolve()
    def ok(name: str):
        target=(destination/name).resolve()
        if base not in target.parents and target != base: raise ValueError("Unsafe archive path")
    if archive.name.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            for n in z.namelist(): ok(n)
            z.extractall(destination)
    elif archive.name.endswith((".tar.gz",".tgz")):
        with tarfile.open(archive,"r:gz") as t:
            for m in t.getmembers(): ok(m.name)
            t.extractall(destination, filter="data")
    else: raise ValueError("Unsupported toolchain archive")

def install_release_asset(asset: ReleaseAsset, app_dir: Path, progress=None) -> Path:
    toolroot=app_dir/"toolchain"; staging=toolroot/"staging"; current=toolroot/"current"
    if staging.exists(): shutil.rmtree(staging)
    staging.mkdir(parents=True,exist_ok=True)
    archive=staging/asset.name
    req=urllib.request.Request(asset.url,headers={"User-Agent":"PunPun-IDE/0.1"})
    with urllib.request.urlopen(req,timeout=60) as r, archive.open("wb") as f:
        total=int(r.headers.get("Content-Length") or asset.size or 0); done=0
        while True:
            chunk=r.read(1024*256)
            if not chunk: break
            f.write(chunk); done+=len(chunk)
            if progress: progress(done,total)
    extracted=staging/"extracted"; safe_extract(archive,extracted)
    entries=[p for p in extracted.iterdir()]
    src=entries[0] if len(entries)==1 and entries[0].is_dir() else extracted
    if current.exists(): shutil.rmtree(current)
    shutil.copytree(src,current)
    return current
