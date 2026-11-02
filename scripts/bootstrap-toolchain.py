#!/usr/bin/env python3
"""Extract devkitPro from a digest-pinned OCI image without a Docker daemon."""
import hashlib
import json
import pathlib
import tarfile
import tempfile
import urllib.request

IMAGE = "devkitpro/devkita64"
DIGEST = "sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282"
ROOT = pathlib.Path(__file__).resolve().parents[1] / ".toolchain"
token = json.load(urllib.request.urlopen(
    f"https://auth.docker.io/token?service=registry.docker.io&scope=repository:{IMAGE}:pull"))["token"]

def fetch(path, accept=None):
    headers = {"Authorization": "Bearer " + token}
    if accept:
        headers["Accept"] = accept
    return urllib.request.urlopen(urllib.request.Request(
        f"https://registry-1.docker.io/v2/{IMAGE}/{path}", headers=headers), timeout=180)

raw = fetch("manifests/" + DIGEST, "application/vnd.docker.distribution.manifest.v2+json").read()
assert "sha256:" + hashlib.sha256(raw).hexdigest() == DIGEST
manifest = json.loads(raw)
if "manifests" in manifest:
    selected = next(m for m in manifest["manifests"]
                    if m.get("platform", {}).get("architecture") == "amd64"
                    and m["platform"].get("os") == "linux")
    raw = fetch("manifests/" + selected["digest"],
                "application/vnd.docker.distribution.manifest.v2+json").read()
    assert "sha256:" + hashlib.sha256(raw).hexdigest() == selected["digest"]
    manifest = json.loads(raw)
ROOT.mkdir(exist_ok=True)
for layer in manifest["layers"]:
    print("Extracting", layer["digest"], flush=True)
    with tempfile.TemporaryFile() as tmp:
        digest = hashlib.sha256()
        with fetch("blobs/" + layer["digest"]) as response:
            while block := response.read(1024 * 1024):
                digest.update(block)
                tmp.write(block)
        assert "sha256:" + digest.hexdigest() == layer["digest"]
        tmp.seek(0)
        with tarfile.open(fileobj=tmp, mode="r|*") as archive:
            for member in archive:
                name = member.name.removeprefix("./")
                if name.startswith("opt/devkitpro/"):
                    archive.extract(member, ROOT, filter="data")
print("DEVKITPRO=" + str(ROOT / "opt/devkitpro"))
