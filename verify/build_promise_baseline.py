#!/usr/bin/env python3
"""Relink the current release build with main@118d946's Promise.cpp.

Only the Promise implementation changes: use this before the Phase 45 test262
A/B run, or to show the four new async.dat cases failing on the old path.
"""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
build = root / "build"
subprocess.run(["make", "-j2", "all"], cwd=root, check=True)
old_source = build / "Promise-before.cpp"
with old_source.open("wb") as out:
    subprocess.run(["git", "show", "118d946:src/js/Promise.cpp"], cwd=root, stdout=out, check=True)
old_object = build / "Promise-before.o"
subprocess.run(["g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Iinclude",
                "-c", str(old_source), "-o", str(old_object)], cwd=root, check=True)
objects = sorted(path for path in build.rglob("*.o")
                 if path not in (old_object, build / "js" / "Promise.o"))
assert build / "main.o" in objects
output = build / "litebird-promise-before"
subprocess.run(["g++", "-std=c++20", "-O2", *map(str, objects), str(old_object),
                "-o", str(output)], cwd=root, check=True)
print("baseline binary:", output.relative_to(root))
