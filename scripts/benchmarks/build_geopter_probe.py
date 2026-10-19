"""Build a standalone probe from an external checkout of the pinned Geopter.

Usage: python build_geopter_probe.py /path/to/Geopter /path/to/probe-build
The checkout must include 3rdparty/json (git submodule update --init 3rdparty/json).
Only generated files in the specified build directory are written.
"""
from pathlib import Path
import re
import subprocess
import sys
source, build = (Path(arg).resolve() for arg in sys.argv[1:3])
cmake = (source / "geopter/optical/src/CMakeLists.txt").read_text()
match = re.search(r"set\(OPTICAL_SRCS\s+(.*?)\)", cmake, re.S)
if not match:
    raise RuntimeError("Cannot find the optical source list in this Geopter revision")
files = match.group(1).split()
if not all(re.fullmatch(r"[a-z0-9_/]+\.cpp", file) for file in files):
    raise RuntimeError("Unexpected optical source list")
if not (source / "3rdparty/json/include/nlohmann/json.hpp").exists():
    raise RuntimeError("Initialize the pinned 3rdparty/json submodule first")
wrapper = build / "wrapper"
wrapper.mkdir(parents=True, exist_ok=True)
(wrapper / "probe.cpp").write_bytes(Path(__file__).with_name("geopter_probe.cpp").read_bytes())
(wrapper / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.24)
project(GeopterReference LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
add_library(geopter_reference STATIC
''' + "\n".join('  "${GEOPTER_SOURCE}/geopter/optical/src/' + f + '"' for f in files) + ''')
target_include_directories(geopter_reference PUBLIC
  "${GEOPTER_SOURCE}/geopter/optical/include"
  "${GEOPTER_SOURCE}/3rdparty/json/include"
  "${GEOPTER_SOURCE}/3rdparty/eigen-3.3.9")
add_executable(geopter_probe probe.cpp)
target_link_libraries(geopter_probe PRIVATE geopter_reference)
''')
subprocess.run(["cmake", "-S", str(wrapper), "-B", str(build),
                "-DGEOPTER_SOURCE=" + str(source), "-DCMAKE_BUILD_TYPE=Release"], check=True)
subprocess.run(["cmake", "--build", str(build), "--config", "Release", "--parallel", "4"], check=True)
