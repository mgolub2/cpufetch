#!/usr/bin/env python3
"""
hw_snapshot.py — Collect hardware-related data from /proc and /sys (Linux),
with a Darwin (macOS) fallback via sysctl. Output is suitable for raw perusal
or feeding to an AI summarizer.

Usage examples:
  - Linux (default curated roots):
      python3 hw_snapshot.py --format ndjson > hw.ndjson
  - Darwin fallback (sysctl -a):
      python3 hw_snapshot.py --format json > mac_hw.json
  - Include additional roots and limit file sizes:
      python3 hw_snapshot.py --root /sys/devices/system/cpu --max-file-bytes 32768 --format text
  - Filter paths with glob patterns:
      python3 hw_snapshot.py --include-glob "*/dmi/*" --exclude-glob "*/serial*"

Notes:
  - Traversal avoids /proc/[pid] and follows a curated list of hardware paths.
  - Binary content is read in bytes and exposed as base64; text uses UTF-8 with replacement.
  - Size limits and include/exclude globs keep output manageable and safe.
"""

from __future__ import annotations

import argparse
import base64
import dataclasses
import fnmatch
import hashlib
import json
import os
import platform
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, Generator, Iterable, List, Optional


PROGRAM_VERSION = "0.1.0"


# Curated Linux hardware paths. These are concise yet broad enough to describe the
# machine's CPU, memory topology, firmware, buses, thermal, and DMI/SMBIOS.
LINUX_FILE_PATHS: List[str] = [
    "/proc/cpuinfo",
    "/proc/meminfo",
    "/proc/interrupts",
    "/proc/softirqs",
    "/proc/stat",
    "/proc/devices",
    "/proc/modules",
    "/proc/swaps",
    "/proc/version",
    "/proc/scsi/scsi",
    "/proc/bus/pci/devices",
]

LINUX_DIR_PATHS: List[str] = [
    "/sys/devices/system/cpu",
    "/sys/devices/system/node",
    "/sys/devices/system/memory",
    "/sys/bus/pci/devices",
    "/sys/class/dmi/id",
    "/sys/class/thermal",
    "/sys/class/powercap",
    "/sys/class/power_supply",
    "/sys/devices/platform",
    "/sys/firmware",
    "/sys/firmware/devicetree/base",
]


DEFAULT_INCLUDE_GLOBS: List[str] = []  # empty means include-all (subject to curation)
DEFAULT_EXCLUDE_GLOBS: List[str] = [
    "*/trace*/*",
    "*/debug/*",
    "*/device/resource*",
]


@dataclasses.dataclass
class SnapshotRecord:
    path: str
    kind: str  # "file" | "symlink" | "dir" | "command"
    size: Optional[int] = None
    mtime: Optional[float] = None
    link_target: Optional[str] = None
    content_text: Optional[str] = None
    content_base64: Optional[str] = None
    truncated: Optional[bool] = None
    sha256_hex: Optional[str] = None
    error: Optional[str] = None
    source: Optional[str] = None  # e.g., "linux", "darwin:sysctl", "darwin:system_profiler"


def is_linux() -> bool:
    return sys.platform.startswith("linux")


def is_darwin() -> bool:
    return sys.platform == "darwin"


def now_unix() -> float:
    return time.time()


def should_include(path: str, include_globs: List[str], exclude_globs: List[str]) -> bool:
    p = path
    if include_globs:
        if not any(fnmatch.fnmatch(p, g) for g in include_globs):
            return False
    if exclude_globs:
        if any(fnmatch.fnmatch(p, g) for g in exclude_globs):
            return False
    return True


def detect_binary(sample: bytes) -> bool:
    if not sample:
        return False
    if b"\x00" in sample:
        return True
    # Heuristic: if many non-printable bytes → treat as binary
    text_like = sum(1 for b in sample if b == 9 or b == 10 or b == 13 or 32 <= b <= 126)
    return (text_like / max(1, len(sample))) < 0.85


def read_file_record(
    path: Path,
    *,
    follow_symlinks: bool,
    max_file_bytes: int,
    remaining_budget: Optional[List[int]],  # single-element list as mutable budget
    include_globs: List[str],
    exclude_globs: List[str],
) -> SnapshotRecord:
    str_path = str(path)
    if not should_include(str_path, include_globs, exclude_globs):
        return SnapshotRecord(path=str_path, kind="file", error="excluded-by-filter")

    try:
        st = path.stat() if (follow_symlinks or not path.is_symlink()) else path.lstat()
    except FileNotFoundError:
        return SnapshotRecord(path=str_path, kind="file", error="not-found")
    except PermissionError:
        return SnapshotRecord(path=str_path, kind="file", error="permission-denied")
    except OSError as e:
        return SnapshotRecord(path=str_path, kind="file", error=f"stat-error:{e}")

    rec = SnapshotRecord(path=str_path, kind="file", size=getattr(st, "st_size", None), mtime=getattr(st, "st_mtime", None))

    if path.is_symlink():
        try:
            rec.kind = "symlink"
            rec.link_target = os.readlink(str_path)
        except OSError:
            pass
        if not follow_symlinks:
            return rec

    # Budget check
    if remaining_budget is not None and remaining_budget[0] <= 0:
        rec.error = "budget-exhausted"
        return rec

    to_read = max_file_bytes
    if remaining_budget is not None:
        to_read = min(to_read, max(0, remaining_budget[0]))

    try:
        with open(path, "rb", buffering=1024 * 4) as f:
            data = f.read(to_read + 1)
    except (IsADirectoryError, PermissionError) as e:
        rec.error = f"read-error:{e.__class__.__name__}"
        return rec
    except OSError as e:
        rec.error = f"read-error:{e}"
        return rec

    truncated = len(data) > to_read
    if truncated:
        data = data[:to_read]
    rec.truncated = truncated
    rec.sha256_hex = hashlib.sha256(data).hexdigest()

    if detect_binary(data):
        rec.content_base64 = base64.b64encode(data).decode("ascii")
    else:
        rec.content_text = data.decode("utf-8", errors="replace")

    if remaining_budget is not None:
        remaining_budget[0] -= len(data)

    return rec


def iterate_linux(
    *,
    roots: List[str],
    include_globs: List[str],
    exclude_globs: List[str],
    follow_symlinks: bool,
    max_file_bytes: int,
    max_total_bytes: Optional[int],
) -> Generator[SnapshotRecord, None, None]:
    remaining_budget = [max_total_bytes if max_total_bytes is not None else (1 << 60)]

    # Single files first
    for file_path in LINUX_FILE_PATHS:
        if roots and not any(str(file_path).startswith(r) for r in roots if Path(r).exists()):
            # If user specified roots, only include files under those roots
            continue
        p = Path(file_path)
        if p.exists() and p.is_file():
            yield read_file_record(
                p,
                follow_symlinks=follow_symlinks,
                max_file_bytes=max_file_bytes,
                remaining_budget=remaining_budget,
                include_globs=include_globs,
                exclude_globs=exclude_globs,
            )

    # Directories walk
    dirs_to_walk: List[str] = [d for d in LINUX_DIR_PATHS if Path(d).exists()]

    # If specific roots provided, intersect with curated list, but also allow
    # ad-hoc additional roots.
    extra_roots = []
    if roots:
        extra_roots = [r for r in roots if Path(r).exists() and r not in dirs_to_walk]
        dirs_to_walk = [d for d in dirs_to_walk if any(d.startswith(r) or r.startswith(d) for r in roots)] + extra_roots

    for d in dirs_to_walk:
        d_path = Path(d)
        for root, dirnames, filenames in os.walk(d_path, followlinks=follow_symlinks):
            # Prune noisy/debug subtrees early
            pruned: List[str] = []
            for dirname in list(dirnames):
                full = str(Path(root) / dirname)
                if not should_include(full, include_globs, exclude_globs):
                    pruned.append(dirname)
                    continue
                # Avoid deep debug/tracing directories that are huge or slow
                if dirname in {"tracing", "debug", "power"}:
                    pruned.append(dirname)
            for name in pruned:
                dirnames.remove(name)

            for filename in filenames:
                fpath = Path(root) / filename
                if not should_include(str(fpath), include_globs, exclude_globs):
                    continue
                # Skip huge binary device nodes, sockets, fifos
                try:
                    mode = fpath.lstat().st_mode
                except OSError:
                    continue
                if not (os.path.isfile(fpath) or os.path.islink(fpath)):
                    continue

                yield read_file_record(
                    fpath,
                    follow_symlinks=follow_symlinks,
                    max_file_bytes=max_file_bytes,
                    remaining_budget=remaining_budget,
                    include_globs=include_globs,
                    exclude_globs=exclude_globs,
                )


def run_cmd_capture(cmd: List[str], timeout_s: int) -> SnapshotRecord:
    label = " ".join(cmd)
    try:
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout_s,
            check=False,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        combined = proc.stdout
        err_tail = proc.stderr.strip()
        if err_tail:
            combined += "\n[stderr]\n" + err_tail
        return SnapshotRecord(
            path=label,
            kind="command",
            content_text=combined,
            size=len(combined.encode("utf-8", errors="replace")),
            mtime=now_unix(),
        )
    except subprocess.TimeoutExpired:
        return SnapshotRecord(path=label, kind="command", error="timeout")
    except Exception as e:  # noqa: BLE001
        return SnapshotRecord(path=label, kind="command", error=f"exec-error:{e}")


def iterate_darwin(*, include_profiler: bool, profiler_timeout: int) -> Generator[SnapshotRecord, None, None]:
    # sysctl -a provides most kernel-exposed hardware information
    rec = run_cmd_capture(["/usr/sbin/sysctl", "-a"], timeout_s=30)
    rec.source = "darwin:sysctl"
    yield rec

    if include_profiler:
        # system_profiler can be slow; restrict to hardware category and mini detail
        sp_cmd = [
            "/usr/sbin/system_profiler",
            "SPHardwareDataType",
            "-detailLevel",
            "mini",
        ]
        rec2 = run_cmd_capture(sp_cmd, timeout_s=profiler_timeout)
        rec2.source = "darwin:system_profiler"
        yield rec2


class OutputWriter:
    def __init__(self, fmt: str, pretty: bool):
        self.fmt = fmt
        self.pretty = pretty
        self.records: List[Dict] = []
        self.started = False

    def emit_meta(self) -> Dict:
        meta = {
            "program": "hw_snapshot",
            "version": PROGRAM_VERSION,
            "host": socket.gethostname(),
            "platform": platform.platform(),
            "python": sys.version.split(" ")[0],
            "time": now_unix(),
        }
        return meta

    def write(self, rec: SnapshotRecord) -> None:
        as_dict = dataclasses.asdict(rec)
        if self.fmt == "ndjson":
            if not self.started:
                print(json.dumps({"meta": self.emit_meta()}), flush=False)
                self.started = True
            print(json.dumps(as_dict), flush=False)
        elif self.fmt == "json":
            if not self.started:
                self.records.append({"meta": self.emit_meta()})
                self.started = True
            self.records.append(as_dict)
        elif self.fmt == "text":
            if not self.started:
                meta = self.emit_meta()
                print(f"# hw_snapshot {meta['version']} on {meta['host']} ({meta['platform']})")
                print(f"# time={meta['time']}")
                self.started = True
            header = f"===== PATH: {rec.path} (kind={rec.kind}) ====="
            print(header)
            if rec.error:
                print(f"[error] {rec.error}")
            elif rec.content_text is not None:
                print(rec.content_text.rstrip("\n"))
            elif rec.content_base64 is not None:
                print("[base64]" + rec.content_base64)
            else:
                print("[no-content]")
            print()  # blank line between entries
        else:
            raise SystemExit(f"Unsupported format: {self.fmt}")

    def close(self) -> None:
        if self.fmt == "json":
            dump_obj = {"records": self.records}
            if self.pretty:
                json.dump(dump_obj, sys.stdout, indent=2, ensure_ascii=False)
            else:
                json.dump(dump_obj, sys.stdout, separators=(",", ":"), ensure_ascii=False)
            print("")


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Collect hardware-related data from /proc and /sys or Darwin fallbacks.")
    parser.add_argument("--format", choices=["ndjson", "json", "text"], default="ndjson", help="Output format (default: ndjson)")
    parser.add_argument("--pretty", action="store_true", help="Pretty-print JSON output")
    parser.add_argument("--root", dest="roots", action="append", default=[], help="Additional root path to traverse (can be repeated)")
    parser.add_argument("--include-glob", dest="include_globs", action="append", default=DEFAULT_INCLUDE_GLOBS.copy(), help="Only include paths matching these patterns (can repeat)")
    parser.add_argument("--exclude-glob", dest="exclude_globs", action="append", default=DEFAULT_EXCLUDE_GLOBS.copy(), help="Exclude paths matching these patterns (can repeat)")
    parser.add_argument("--follow-symlinks", action="store_true", help="Follow symlinks during traversal")
    parser.add_argument("--max-file-bytes", type=int, default=64 * 1024, help="Per-file read cap in bytes (default: 65536)")
    parser.add_argument("--max-total-bytes", type=int, default=None, help="Global read cap across all files (default: unlimited)")
    parser.add_argument("--darwin-use-system-profiler", action="store_true", help="Include system_profiler on macOS (can be slow)")
    parser.add_argument("--darwin-profiler-timeout", type=int, default=30, help="Timeout for system_profiler (seconds)")
    parser.add_argument("--force-linux", action="store_true", help="Force Linux mode even on non-Linux (for testing)")
    parser.add_argument("--force-darwin", action="store_true", help="Force Darwin mode even on non-Darwin (for testing)")
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)

    fmt = args.format
    writer = OutputWriter(fmt=fmt, pretty=args.pretty)

    do_linux = is_linux() or args.force_linux
    do_darwin = is_darwin() or args.force_darwin

    if do_linux and not args.force_darwin:
        for rec in iterate_linux(
            roots=args.roots,
            include_globs=args.include_globs,
            exclude_globs=args.exclude_globs,
            follow_symlinks=args.follow_symlinks,
            max_file_bytes=args.max_file_bytes,
            max_total_bytes=args.max_total_bytes,
        ):
            rec.source = "linux"
            writer.write(rec)
        writer.close()
        return 0

    if do_darwin and not args.force_linux:
        for rec in iterate_darwin(include_profiler=args.darwin_use_system_profiler, profiler_timeout=args.darwin_profiler_timeout):
            writer.write(rec)
        writer.close()
        return 0

    # Fallback: neither detected — try Linux style if /proc or /sys exists, else Darwin sysctl
    if Path("/proc").exists() or Path("/sys").exists():
        for rec in iterate_linux(
            roots=args.roots,
            include_globs=args.include_globs,
            exclude_globs=args.exclude_globs,
            follow_symlinks=args.follow_symlinks,
            max_file_bytes=args.max_file_bytes,
            max_total_bytes=args.max_total_bytes,
        ):
            rec.source = "linux"
            writer.write(rec)
        writer.close()
        return 0
    else:
        for rec in iterate_darwin(include_profiler=args.darwin_use_system_profiler, profiler_timeout=args.darwin_profiler_timeout):
            writer.write(rec)
        writer.close()
        return 0


if __name__ == "__main__":
    raise SystemExit(main())


