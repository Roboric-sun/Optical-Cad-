"""Fetch immutable external reference sources; no library is shipped in our app."""
from pathlib import Path
import shutil
import sys
import tarfile
import tempfile
import urllib.request

destination = Path(sys.argv[1]).resolve()
if destination.exists(): raise RuntimeError("Reference destination must be new")
GEOPTER = "0edfbf52fcf0e3fc660e5ae33c354fd7a6be80d7"
JSON = "199dea11b17c533721b26249e2dcaee6ca1d51d3"
with tempfile.TemporaryDirectory(prefix="geopter-reference-") as temporary:
    base = Path(temporary)
    for name, revision, repo in (("geopter", GEOPTER, "heterophyllus/Geopter"), ("json", JSON, "nlohmann/json")):
        archive = base / (name + ".tar.gz")
        with urllib.request.urlopen(f"https://codeload.github.com/{repo}/tar.gz/{revision}", timeout=120) as response, archive.open("wb") as output:
            shutil.copyfileobj(response, output)
        directory = base / name; directory.mkdir()
        with tarfile.open(archive) as source: source.extractall(directory, filter="data")
    shutil.copytree(next((base / "geopter").iterdir()), destination)
    header = next((base / "json").glob("*/single_include/nlohmann/json.hpp"))
    include = destination / "3rdparty/json/include/nlohmann"; include.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(header, include / "json.hpp")
print(f"External Geopter {GEOPTER}, JSON {JSON}: {destination}")
