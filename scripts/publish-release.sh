#!/usr/bin/env bash
set -euo pipefail

version="$(python3 -c 'import json; print(json.load(open("artifacts/linux-release/update-manifest.json"))["version"])')"
build_number="$(python3 -c 'import json; print(json.load(open("artifacts/linux-release/update-manifest.json"))["buildNumber"])')"
if [ "${GITHUB_REF_NAME}" = "master" ]; then
  tag=nightly
  channel=nightly
else
  tag="v$version"
  channel=stable
  test "$GITHUB_REF_NAME" = "$tag"
fi
appimage_name="$(bash scripts/release-asset-name.sh "$channel" "$build_number" Speecher-x86_64.AppImage)"
dmg_name="$(bash scripts/release-asset-name.sh "$channel" "$build_number" speecher.dmg)"
installer_name="$(bash scripts/release-asset-name.sh "$channel" "$build_number" Speecher-Setup-x64.exe)"
if [ "$channel" = nightly ]; then
  release_exists=false
  asset_pages="$(mktemp)"
  echo '[]' > "$asset_pages"
  if gh release view "$tag" >/dev/null 2>&1; then
    release_exists=true
    release_id="$(gh api "repos/${GITHUB_REPOSITORY}/releases/tags/$tag" --jq '.id')"
    # A file, not an argument: the full listing runs to tens of kilobytes.
    gh api "repos/${GITHUB_REPOSITORY}/releases/$release_id/assets" --paginate --slurp > "$asset_pages"
  fi
  # Check every immutable asset before changing the tag, release, or manifest.
  asset_plan="$(python3 - "$asset_pages" "$build_number" \
    artifacts/linux-release/Speecher-x86_64.AppImage "$appimage_name" \
    artifacts/macos-release/speecher.dmg "$dmg_name" \
    artifacts/windows-release/Speecher-Setup-x64.exe "$installer_name" <<'PYTHON'
import hashlib
import json
from pathlib import Path
import re
import sys

assets = {asset["name"]: asset for page in json.loads(Path(sys.argv[1]).read_text()) for asset in page}
build_number = int(sys.argv[2])
binaries = list(zip(sys.argv[3::2], sys.argv[4::2]))
base_names = {Path(path).name for path, _ in binaries}
build_assets = []
for name in assets:
    match = re.fullmatch(r"(.+)-build([0-9]+)(\.[^.]+)", name)
    if match and match[1] + match[3] in base_names:
        build_assets.append((int(match[2]), name))
newest_build = max((build for build, _ in build_assets), default=build_number)
if build_number < newest_build:
    sys.exit(f"Refusing to regress the nightly release from build {newest_build} to {build_number}")
for path, name in binaries:
    sha256 = hashlib.sha256()
    with open(path, "rb") as binary:
        for chunk in iter(lambda: binary.read(1024 * 1024), b""):
            sha256.update(chunk)
    digest = "sha256:" + sha256.hexdigest()
    remote = assets.get(name)
    if remote is None:
        print(f"upload\t{Path(path).with_name(name)}")
    elif remote.get("state") != "uploaded" or not remote.get("digest"):
        # A failed upload leaves an empty stub behind; it was never advertised
        # (the manifest goes up last), so replace it.
        print(f"stub\t{name}")
        print(f"upload\t{Path(path).with_name(name)}")
    elif remote["digest"] != digest:
        sys.exit(f"Refusing to replace nightly asset {name}: remote digest {remote['digest']!r} differs from "
                 f"local {digest}. A rebuilt nightly must not reuse a published build number; if this asset is "
                 f"unwanted, run `gh release delete-asset nightly {name}` and re-run.")
# Include the build about to be uploaded when retaining the newest ten builds.
keep = set(sorted({build_number} | {build for build, _ in build_assets}, reverse=True)[:10])
for build, name in build_assets:
    if build not in keep:
        print(f"delete\t{name}")
PYTHON
  )"
  missing_assets=()
  stub_assets=()
  stale_assets=()
  while IFS=$'\t' read -r action asset; do
    case "$action" in
      upload) missing_assets+=("$asset") ;;
      stub) stub_assets+=("$asset") ;;
      delete) stale_assets+=("$asset") ;;
    esac
  done <<< "$asset_plan"
fi
windows_sha256="$(awk '{print $1}' artifacts/windows-release/Speecher-Setup-x64.exe.sha256)"
python3 - "$tag" "$windows_sha256" "$appimage_name" "$installer_name" <<'PY'
import json
import pathlib
import sys

tag, sha256, appimage_name, installer_name = sys.argv[1:]
path = pathlib.Path("artifacts/linux-release/update-manifest.json")
manifest = json.loads(path.read_text())
asset_url = f"https://github.com/firemonster612/speecher/releases/download/{tag}/"
manifest["linux-x86_64"]["appimage"] = asset_url + appimage_name
manifest["windows-x86_64"] = {
    "installer": asset_url + installer_name,
    "sha256": sha256,
}
path.write_text(json.dumps(manifest, indent=2) + "\n")
PY
if [ "${GITHUB_REF_NAME}" = "master" ]; then
  git tag --force "$tag" "$GITHUB_SHA"
  git push origin "refs/tags/$tag" --force
  cat > release-notes.md <<EOF
Version: $version
Build number: $build_number
Commit: $GITHUB_SHA

This Nightly Build is untested. Use the Stable Release unless you want the latest master build.

Windows installer: Speecher-Setup-x64.exe

Update channels: https://github.com/${GITHUB_REPOSITORY}#installation--updates
EOF
  release_notes=release-notes.md
  if [ "$release_exists" = true ]; then
    gh release edit "$tag" --prerelease --title "Nightly Build $version" --notes-file release-notes.md
  else
    gh release create "$tag" --prerelease --title "Nightly Build $version" --notes-file release-notes.md
  fi
  cp artifacts/linux-release/Speecher-x86_64.AppImage "artifacts/linux-release/$appimage_name"
  cp artifacts/macos-release/speecher.dmg "artifacts/macos-release/$dmg_name"
  cp artifacts/windows-release/Speecher-Setup-x64.exe "artifacts/windows-release/$installer_name"
  for asset in "${stub_assets[@]}"; do
    gh release delete-asset "$tag" "$asset" --yes
  done
  if [ "${#missing_assets[@]}" -gt 0 ]; then
    gh release upload "$tag" "${missing_assets[@]}"
  fi
  gh release upload "$tag" \
    artifacts/linux-release/Speecher-x86_64.AppImage \
    artifacts/macos-release/speecher.dmg \
    artifacts/windows-release/Speecher-Setup-x64.exe \
    artifacts/linux-release/Speecher-x86_64.AppImage.zsync \
    artifacts/windows-release/Speecher-Setup-x64.exe.sha256 --clobber
  gh release upload "$tag" artifacts/linux-release/update-manifest.json --clobber

  for asset in "${stale_assets[@]}"; do
    gh release delete-asset "$tag" "$asset" --yes
  done
else
  if [ -f "docs/releases/$version.md" ]; then
    release_notes="docs/releases/$version.md"
    if ! awk 'NF && $1 !~ /^#/ { body = 1 } END { exit !body }' "$release_notes"; then
      echo "$release_notes has headings but no release-note body." >&2
      exit 1
    fi
  else
    release_notes=release-notes.md
    printf 'Release notes have not been written. Add docs/releases/%s.md before the next release.\n' "$version" > "$release_notes"
  fi
  if gh release view "$tag" >/dev/null 2>&1; then
    gh release edit "$tag" --title "Speecher $version" --notes-file "$release_notes"
  else
    gh release create "$tag" --title "Speecher $version" --notes-file "$release_notes"
  fi
  gh release upload "$tag" \
    "artifacts/linux-release/$appimage_name" \
    artifacts/linux-release/Speecher-x86_64.AppImage.zsync \
    "artifacts/macos-release/$dmg_name" \
    "artifacts/windows-release/$installer_name" \
    artifacts/windows-release/Speecher-Setup-x64.exe.sha256 \
    artifacts/linux-release/update-manifest.json --clobber
fi
published_at="$(git log -1 --format=%aD HEAD)"
python3 scripts/make-appcast.py \
  --channel "$channel" \
  --version "$version" \
  --build-number "$build_number" \
  --pub-date "$published_at" \
  --dmg-url "https://github.com/${GITHUB_REPOSITORY}/releases/download/${tag}/${dmg_name}" \
  --signature artifacts/macos-release/sparkle-signature.txt \
  --notes-file "$release_notes" \
  --output "appcast-${channel}.xml"
echo "channel=$channel" >> "$GITHUB_OUTPUT"
echo "version=$version" >> "$GITHUB_OUTPUT"
echo "build_number=$build_number" >> "$GITHUB_OUTPUT"
