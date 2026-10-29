#!/usr/bin/env bash
set -euo pipefail
tag=${GITHUB_REF_NAME:?Tag context required}
commit=${GITHUB_SHA:?Source commit required}
python3 packaging/verify-release.py --dist dist --tag "$tag" --commit "$commit"
mapfile -t filenames < <(python3 -c 'import json; print("\n".join(e["file"] for e in json.load(open("dist/package-manifest.json"))["artifacts"]))')
assets=(dist/SHA256SUMS dist/package-manifest.json dist/SOURCE_COMMIT)
for filename in "${filenames[@]}"; do assets+=("dist/$filename"); done
if gh release view "$tag" --json isDraft > dist/existing-release.json 2>/dev/null; then
    python3 - <<'PY'
import json
assert json.load(open('dist/existing-release.json'))['isDraft'], 'Never replace assets on an already published release'
PY
    # Only an unpublished draft for this immutable version tag may be repaired.
    gh release upload "$tag" "${assets[@]}" --clobber
else
    cat > dist/release-notes.md <<'EOF'
Native C++20 / Qt 6 LoungeCat Desktop for Linux x86_64.

Includes AppImage, Debian package, Fedora 44 x86_64 RPM, Gentoo amd64 ebuild and
complete overlay, matching source, checksums and per-platform build inventories.
AppImage/deb passed clean Debian 12 and Ubuntu 24.04 runtime tests. The RPM was
built on Fedora 44 and passed clean Fedora package/runtime tests. Gentoo built
from the exact matching source using Portage, ran local regression fixtures and
GUI/Settings/SQLite/verified TLS/install/uninstall checks in an offline container.
Native normal/ASan regression jobs passed before publication.

Keep a private backup and use a separate preview profile when moving from Kotlin
v1; full in-place upgrade/downgrade compatibility is not certified. See the README,
docs/MIGRATION-v2.md and docs/PACKAGING.md at this tag. No Android artifact is included.
EOF
    gh release create "$tag" --verify-tag --target "$commit" --draft \
        --title "LoungeCat Desktop ${tag#v}" --notes-file dist/release-notes.md "${assets[@]}"
fi
# No public half-uploaded release: publication is the final mutation.
gh release edit "$tag" --draft=false
