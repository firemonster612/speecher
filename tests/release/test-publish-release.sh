#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="${PUBLISH_TEST_DIR:-$(mktemp -d)}"
if [ -z "${PUBLISH_TEST_DIR:-}" ]; then
  trap 'rm -rf "$WORK"' EXIT
fi
mkdir -p "$WORK"

python3 - "$ROOT" "$WORK" <<'PY'
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET

root, work = map(lambda arg: Path(arg).resolve(), sys.argv[1:])
shim = work / "bin"
shim.mkdir(exist_ok=True)
(shim / "gh").write_text('''#!/usr/bin/env python3
import json
import os
from pathlib import Path
import shutil
import sys

command = [Path(sys.argv[0]).name, *sys.argv[1:]]
with open(os.environ["COMMAND_LOG"], "a") as log:
    log.write(json.dumps(command) + "\\n")
args = sys.argv[1:]
if command[0] == "git":
    if args == ["log", "-1", "--format=%aD", "HEAD"]:
        print("Tue, 8 Sep 2026 12:00:00 +0000")
    elif args not in (["tag", "--force", "nightly", os.environ["GITHUB_SHA"]],
                      ["push", "origin", "refs/tags/nightly", "--force"]):
        sys.exit("Unexpected git invocation: " + repr(args))
elif args[:2] == ["release", "upload"]:
    for name in args[3:]:
        if name.startswith("--"):
            continue
        path = Path(name)
        assert path.is_file(), name
        shutil.copyfile(path, Path("uploaded") / path.name)
elif args[:2] == ["release", "view"]:
    sys.exit(1 if os.environ["RELEASE_EXISTS"] == "no" else 0)
elif args[:2] in (["release", "create"], ["release", "edit"], ["release", "delete-asset"]):
    pass
elif args[0] == "api":
    if args[1].endswith("/releases/tags/nightly"):
        print("123")
    elif args[1].endswith("/releases/123/assets"):
        assert "--paginate" in args
        assets = json.loads(Path(os.environ["ASSET_LIST"]).read_text())
        assert "--slurp" in args
        print(json.dumps([assets[:20], assets[20:]]))
    else:
        sys.exit("Unexpected API request: " + repr(args))
else:
    sys.exit("Unexpected gh invocation: " + repr(args))
''')
(shim / "gh").chmod(0o755)
(shim / "git").write_bytes((shim / "gh").read_bytes())
(shim / "git").chmod(0o755)

rolling = ["Speecher-x86_64.AppImage", "speecher.dmg", "Speecher-Setup-x64.exe"]
def asset_name(build, base):
    return subprocess.check_output(["bash", str(root / "scripts/release-asset-name.sh"),
        "nightly", str(build), base], text=True).strip()

immutable = [asset_name(402, name) for name in rolling]
sidecars = ["Speecher-x86_64.AppImage.zsync", "Speecher-Setup-x64.exe.sha256"]

for scenario in ("nightly", "identical", "changed", "stale", "stub", "create", "stable"):
    channel = "stable" if scenario == "stable" else "nightly"
    ref = "v9.9.9" if channel == "stable" else "master"
    build_number = 390 if scenario == "stale" else 402
    version = "9.9.9" if channel == "stable" else f"9.9.9-nightly.{build_number}"
    case = work / scenario
    case.mkdir(exist_ok=True)
    scripts = case / "scripts"
    if not scripts.exists():
        scripts.symlink_to(root / "scripts", target_is_directory=True)
    (case / "uploaded").mkdir(exist_ok=True)
    artifacts = case / "artifacts"
    linux, macos, windows = [artifacts / f"{platform}-release" for platform in ("linux", "macos", "windows")]
    for directory in (linux, macos, windows):
        directory.mkdir(parents=True, exist_ok=True)
    for directory, name in zip((linux, macos, windows), rolling):
        (directory / name).write_text(f"fixture {name}\n")
    linux_sha256 = hashlib.sha256((linux / rolling[0]).read_bytes()).hexdigest()
    windows_sha256 = hashlib.sha256((windows / rolling[2]).read_bytes()).hexdigest()
    (linux / sidecars[0]).write_text("fixture zsync\n")
    (windows / sidecars[1]).write_text(f"{windows_sha256}  Speecher-Setup-x64.exe\n")
    (macos / "sparkle-signature.txt").write_text("sparkle:edSignature=fixture-signature\nlength=21\n")
    tag = "nightly" if channel == "nightly" else ref
    url = f"https://github.com/firemonster612/speecher/releases/download/{tag}/"
    manifest_path = linux / "update-manifest.json"
    manifest_path.write_text(json.dumps({"version": version, "buildNumber": build_number,
        "linux-x86_64": {"sha256": linux_sha256}}, indent=2) + "\n")
    assets = []
    builds = range(393, 403) if scenario == "stale" else range(390, 403)
    for build in builds:
        for base in rolling:
            if build == 402 and scenario in ("nightly", "stub"):
                continue
            digest = hashlib.sha256(f"fixture {base}\n".encode()).hexdigest()
            assets.append({"name": asset_name(build, base), "digest": f"sha256:{digest}", "state": "uploaded"})
    if scenario == "stub":
        # An interrupted first upload of 402 left an empty DMG asset behind.
        assets.append({"name": immutable[1], "digest": None, "state": "open"})
    assets += [{"name": name, "digest": f"sha256:{hashlib.sha256(name.encode()).hexdigest()}", "state": "uploaded"}
        for name in rolling + sidecars + ["update-manifest.json", "unrelated-build999.txt"]]
    asset_list = case / "assets.json"
    asset_list.write_text(json.dumps(list(reversed(assets))))
    if scenario == "changed":
        (linux / rolling[0]).write_text("rebuilt AppImage with different bytes\n")
    original_manifest = manifest_path.read_bytes()
    log = case / "commands.jsonl"
    log.write_text("")
    output = case / "github-output.txt"
    output.write_text("")
    env = dict(os.environ, PATH=f"{shim}:{os.environ['PATH']}", COMMAND_LOG=str(log),
        ASSET_LIST=str(asset_list), RELEASE_EXISTS="no" if scenario == "create" else "yes", GITHUB_REF_NAME=ref, GITHUB_SHA="0123456789abcdef0123456789abcdef01234567",
        GITHUB_REPOSITORY="firemonster612/speecher", GITHUB_OUTPUT=str(output), GH_TOKEN="fake-token")
    result = subprocess.run(["bash", str(root / "scripts/publish-release.sh")], cwd=case, env=env, capture_output=True, text=True)
    commands = [json.loads(line) for line in log.read_text().splitlines()]
    listings = [command for command in commands if command[:2] == ["gh", "api"] and command[2].endswith("/assets")]
    mutations = [command for command in commands if command[:3] in (
        ["gh", "release", "upload"], ["gh", "release", "edit"], ["gh", "release", "create"],
        ["gh", "release", "delete-asset"]) or command[:2] in (["git", "tag"], ["git", "push"])]
    if scenario in ("changed", "stale"):
        assert result.returncode != 0, scenario
        assert not mutations, mutations
        assert len(listings) == 1, listings
        assert manifest_path.read_bytes() == original_manifest
        assert not (case / "appcast-nightly.xml").exists()
        if scenario == "changed":
            assert immutable[0] in result.stderr and "sha256" in result.stderr, result.stderr
        else:
            assert "Refusing to regress the nightly release from build 402 to 390" in result.stderr, result.stderr
        print(f"PASS {scenario}: rejected before any mutation: {result.stderr.strip()}")
        continue
    assert result.returncode == 0, result.stderr
    if channel == "nightly":
        assert len(listings) == (0 if scenario == "create" else 1), listings
        if listings:
            assert commands.index(listings[0]) < commands.index(mutations[0]), commands
        release_changes = [command for command in commands if command[:3] in (
            ["gh", "release", "create"], ["gh", "release", "edit"])]
        assert len(release_changes) == 1, release_changes
        assert release_changes[0][2] == ("create" if scenario == "create" else "edit"), release_changes
        assert [command[:2] for command in mutations[:2]] == [["git", "tag"], ["git", "push"]], mutations
        assert mutations[2] == release_changes[0], mutations
    uploads = [command for command in commands if command[:3] == ["gh", "release", "upload"]]
    batches = [[Path(arg).name for arg in command[4:] if not arg.startswith("--")] for command in uploads]
    names = [name for batch in batches for name in batch]
    expected = immutable if channel == "nightly" else rolling
    rerun = scenario == "identical"
    if channel == "nightly" and not rerun:
        assert "--clobber" not in uploads[0], uploads[0]
        assert set(names[:3]) == set(immutable), f"Immutable binaries must upload first: {batches}"
        assert set(batches[0]) == set(immutable), batches
        assert set(names[3:-1]) == set(rolling + sidecars), names
        assert batches[-1] == ["update-manifest.json"], "Manifest must upload in a separate final call"
    elif rerun:
        assert batches == [rolling + sidecars, ["update-manifest.json"]], batches
    else:
        assert names == [rolling[0], sidecars[0], rolling[1], rolling[2], sidecars[1], "update-manifest.json"], names
    assert len(names) == (9 if channel == "nightly" and not rerun else 6), names
    assert all("--clobber" in command for command in (uploads[1:] if channel == "nightly" and not rerun else uploads))
    assert [name for name in names if name.endswith(".zsync")] == [sidecars[0]]
    assert all(command[3] == tag for command in uploads)
    for original, published, directory in zip(rolling, expected, (linux, macos, windows)):
        if rerun:
            published = original
        assert (case / "uploaded" / published).read_bytes() == (directory / original).read_bytes()
    manifest = json.loads((case / "uploaded/update-manifest.json").read_text())
    assert manifest == {"version": version, "buildNumber": build_number,
        "linux-x86_64": {"appimage": url + expected[0], "sha256": linux_sha256},
        "windows-x86_64": {"installer": url + expected[2], "sha256": windows_sha256}}, manifest
    appcast_path = case / f"appcast-{channel}.xml"
    appcast = ET.parse(appcast_path)
    enclosure = appcast.find("./channel/item/enclosure")
    sparkle = "{http://www.andymatuschak.org/xml-namespaces/sparkle}"
    assert enclosure.attrib == {"url": url + expected[1], "length": "21",
        "type": "application/x-apple-diskimage", sparkle + "edSignature": "fixture-signature"}
    assert appcast.findtext(f"./channel/item/{sparkle}version") == "402"
    assert output.read_text() == f"channel={channel}\nversion={version}\nbuild_number=402\n"
    deletes = [command for command in commands if command[:3] == ["gh", "release", "delete-asset"]]
    if channel == "nightly":
        expected_deletes = {asset_name(build, base) for build in (390, 391, 392) for base in rolling} if scenario != "create" else set()
        stub_deletes = [command for command in deletes if command[4] == immutable[1]] if scenario == "stub" else []
        if scenario == "stub":
            assert len(stub_deletes) == 1 and commands.index(stub_deletes[0]) < commands.index(uploads[0]), "Stub must be deleted before the first upload"
            deletes = [command for command in deletes if command not in stub_deletes]
        assert len(deletes) == len(expected_deletes) and {command[4] for command in deletes} == expected_deletes, deletes
        assert all(command[3] == "nightly" and command[5:] == ["--yes"] for command in deletes + stub_deletes), deletes
        if deletes:
            assert commands.index(deletes[0]) > commands.index(uploads[-1])
        if scenario != "nightly":
            print(f"PASS {scenario}: uploads, release mutation, URLs, bytes, and pruning match expectations.")
            continue
        print("Nightly appcast:")
        print(appcast_path.read_text())
        print("Nightly manifest:")
        print((case / "uploaded/update-manifest.json").read_text(), end="")
        print("PASS nightly: immutable uploads first, rolling assets next, manifest last; URLs and bytes match; only builds 390..392 pruned.")
    else:
        assert not deletes, deletes
        assert not any(command[:2] == ["gh", "api"] for command in commands), commands
        assert not any(command[:2] in (["git", "tag"], ["git", "push"]) for command in commands), commands
        print("PASS stable: rolling names unchanged; matching manifest and appcast; no pruning or tag mutation.")
PY
