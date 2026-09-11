#!/usr/bin/env python3
"""PunBrain Legal Overlay.

This program only displays data returned by the enabled Ninjabrain Bot HTTP API.
It does not read Minecraft memory, inspect entities, parse the screen, synthesize
input, triangulate independently, or transform coordinates into new predictions.
"""

from __future__ import annotations

import argparse
import json
import tkinter as tk
import urllib.error
import urllib.request
from typing import Any

API_ROOT = "http://127.0.0.1:52533/api/v1"
ENDPOINTS = (
    "stronghold",
    "all-advancements",
    "blind",
    "divine",
    "boat",
    "information-messages",
    "version",
    "ping",
)


def fetch(endpoint: str, timeout: float = 0.35) -> tuple[str, Any]:
    url = f"{API_ROOT}/{endpoint}"
    request = urllib.request.Request(url, headers={"User-Agent": "PunBrain-Legal-Overlay/0.2"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        raw = response.read().decode("utf-8", errors="replace")
    try:
        return raw, json.loads(raw)
    except json.JSONDecodeError:
        return raw, raw


def scalar(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if value is None:
        return "null"
    return str(value)


def format_stronghold(data: Any) -> str:
    if not isinstance(data, dict):
        return str(data)

    lines: list[str] = ["PUNBRAIN · NINJABRAIN API", ""]
    lines.append(f"resultType  {scalar(data.get('resultType'))}")

    player = data.get("playerPosition")
    if isinstance(player, dict) and player:
        lines.append("")
        lines.append("PLAYER")
        for key in ("xInOverworld", "zInOverworld", "horizontalAngle", "isInOverworld", "isInNether"):
            if key in player:
                lines.append(f"{key:<18} {scalar(player[key])}")

    predictions = data.get("predictions")
    lines.append("")
    lines.append("PREDICTIONS")
    if isinstance(predictions, list) and predictions:
        for index, prediction in enumerate(predictions, 1):
            if not isinstance(prediction, dict):
                lines.append(f"{index}. {prediction}")
                continue
            fields = []
            for key in ("chunkX", "chunkZ", "certainty", "overworldDistance"):
                if key in prediction:
                    fields.append(f"{key}={scalar(prediction[key])}")
            lines.append(f"{index}. " + "  ".join(fields))
    else:
        lines.append("(waiting for Ninjabrain prediction)")

    throws = data.get("eyeThrows")
    lines.append("")
    lines.append("EYE THROWS")
    if isinstance(throws, list) and throws:
        for index, throw in enumerate(throws, 1):
            if not isinstance(throw, dict):
                lines.append(f"{index}. {throw}")
                continue
            fields = []
            for key in (
                "xInOverworld",
                "zInOverworld",
                "angle",
                "angleWithoutCorrection",
                "correction",
                "correctionIncrements",
                "error",
                "type",
            ):
                if key in throw:
                    fields.append(f"{key}={scalar(throw[key])}")
            lines.append(f"{index}. " + "  ".join(fields))
    else:
        lines.append("(no eye throws supplied by Ninjabrain)")

    return "\n".join(lines)


def format_payload(endpoint: str, data: Any) -> str:
    if endpoint == "stronghold":
        return format_stronghold(data)
    if isinstance(data, (dict, list)):
        return json.dumps(data, indent=2, ensure_ascii=False)
    return str(data)


class Overlay:
    def __init__(self, root: tk.Tk, endpoint: str, interval_ms: int, borderless: bool, alpha: float):
        self.root = root
        self.endpoint = endpoint
        self.interval_ms = max(100, interval_ms)
        self.last_text = ""

        root.title("PunBrain Legal Overlay")
        root.configure(bg="#101419")
        root.attributes("-topmost", True)
        try:
            root.attributes("-alpha", alpha)
        except tk.TclError:
            pass
        if borderless:
            root.overrideredirect(True)

        outer = tk.Frame(root, bg="#101419", bd=1, relief="solid")
        outer.pack(fill="both", expand=True)

        header = tk.Frame(outer, bg="#171d24")
        header.pack(fill="x")

        self.title_label = tk.Label(
            header,
            text="PunBrain · Legal API Mirror",
            fg="#eaf2f8",
            bg="#171d24",
            font=("TkDefaultFont", 10, "bold"),
            padx=10,
            pady=6,
        )
        self.title_label.pack(side="left")

        self.status = tk.Label(
            header,
            text="connecting",
            fg="#b7c8d6",
            bg="#171d24",
            font=("TkDefaultFont", 9),
            padx=10,
        )
        self.status.pack(side="right")

        self.text = tk.Text(
            outer,
            width=74,
            height=22,
            bg="#101419",
            fg="#e2e9ef",
            insertbackground="#e2e9ef",
            relief="flat",
            borderwidth=0,
            padx=10,
            pady=8,
            font=("TkFixedFont", 9),
            wrap="none",
        )
        self.text.pack(fill="both", expand=True)
        self.text.configure(state="disabled")

        footer = tk.Label(
            outer,
            text="Displays Ninjabrain API output only · no Minecraft telemetry · no input automation",
            fg="#8fa4b4",
            bg="#101419",
            anchor="w",
            padx=10,
            pady=5,
            font=("TkDefaultFont", 8),
        )
        footer.pack(fill="x")

        root.bind("<Escape>", lambda _event: root.destroy())
        self.tick()

    def set_text(self, value: str) -> None:
        if value == self.last_text:
            return
        self.last_text = value
        self.text.configure(state="normal")
        self.text.delete("1.0", "end")
        self.text.insert("1.0", value)
        self.text.configure(state="disabled")

    def tick(self) -> None:
        try:
            _raw, data = fetch(self.endpoint)
            self.set_text(format_payload(self.endpoint, data))
            self.status.configure(text=f"live · {self.endpoint}", fg="#b7e3c0")
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            self.status.configure(text="Ninjabrain API unavailable", fg="#f0b6b6")
            self.set_text(
                "PunBrain Legal Overlay\n\n"
                "Ninjabrain Bot API is not reachable at 127.0.0.1:52533.\n"
                "Enable the HTTP API in Ninjabrain Bot, then keep this window open.\n\n"
                f"{type(exc).__name__}: {exc}"
            )
        self.root.after(self.interval_ms, self.tick)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Always-on-top mirror of Ninjabrain Bot API data")
    parser.add_argument("--endpoint", choices=ENDPOINTS, default="stronghold")
    parser.add_argument("--interval-ms", type=int, default=200)
    parser.add_argument("--alpha", type=float, default=0.92)
    parser.add_argument("--borderless", action="store_true")
    parser.add_argument("--geometry", default="760x430+24+24")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    alpha = min(1.0, max(0.35, args.alpha))
    root = tk.Tk()
    root.geometry(args.geometry)
    Overlay(root, args.endpoint, args.interval_ms, args.borderless, alpha)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
