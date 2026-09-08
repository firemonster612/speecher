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
windows_sha256="$(awk '{print $1}' artifacts/windows-release/Speecher-Setup-x64.exe.sha256)"
python3 - "$tag" "$windows_sha256" <<'PY'
import json
import pathlib
import sys

tag, sha256 = sys.argv[1:]
path = pathlib.Path("artifacts/linux-release/update-manifest.json")
manifest = json.loads(path.read_text())
manifest["windows-x86_64"] = {
    "installer": (
        "https://github.com/firemonster612/speecher/releases/download/"
        f"{tag}/Speecher-Setup-x64.exe"
    ),
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
  if gh release view "$tag" >/dev/null 2>&1; then
    gh release edit "$tag" --prerelease --title "Nightly Build $version" --notes-file release-notes.md
  else
    gh release create "$tag" --prerelease --title "Nightly Build $version" --notes-file release-notes.md
  fi
  gh release upload "$tag" \
    artifacts/linux-release/Speecher-x86_64.AppImage \
    artifacts/linux-release/Speecher-x86_64.AppImage.zsync \
    artifacts/macos-release/speecher.dmg \
    artifacts/windows-release/Speecher-Setup-x64.exe \
    artifacts/windows-release/Speecher-Setup-x64.exe.sha256 \
    artifacts/linux-release/update-manifest.json --clobber
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
    artifacts/linux-release/Speecher-x86_64.AppImage \
    artifacts/linux-release/Speecher-x86_64.AppImage.zsync \
    artifacts/macos-release/speecher.dmg \
    artifacts/windows-release/Speecher-Setup-x64.exe \
    artifacts/windows-release/Speecher-Setup-x64.exe.sha256 \
    artifacts/linux-release/update-manifest.json --clobber
fi
published_at="$(git log -1 --format=%aD HEAD)"
python3 scripts/make-appcast.py \
  --channel "$channel" \
  --version "$version" \
  --build-number "$build_number" \
  --pub-date "$published_at" \
  --dmg-url "https://github.com/${GITHUB_REPOSITORY}/releases/download/${tag}/speecher.dmg" \
  --signature artifacts/macos-release/sparkle-signature.txt \
  --notes-file "$release_notes" \
  --output "appcast-${channel}.xml"
echo "channel=$channel" >> "$GITHUB_OUTPUT"
echo "version=$version" >> "$GITHUB_OUTPUT"
echo "build_number=$build_number" >> "$GITHUB_OUTPUT"
