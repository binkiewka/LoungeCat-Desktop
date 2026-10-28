#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo"
test "$(uname -m)" = x86_64 || { echo 'This packaging pipeline currently targets x86_64.' >&2; exit 1; }
build="$repo/build/packages"
dist="$repo/dist"
mkdir -p "$build/tools" "$dist/test-support"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=OFF -DLOUNGECAT_SANITIZERS=none
cmake --build "$build" --parallel "${BUILD_JOBS:-2}"
version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build/CMakeCache.txt")
test -n "$version"
desktop-file-validate packaging/io.github.binkiewka.LoungeCat.desktop
cpack --config "$build/CPackConfig.cmake" -B "$dist" -G DEB
stage=$(mktemp -d "$build/AppDir.XXXXXX")
DESTDIR="$stage" cmake --install "$build"

# Asset IDs do not silently follow the moving continuous tag. Hash verification
# is required before extracting/executing either deployment tool.
python3 - "$build/tools" <<'PY'
from pathlib import Path
import hashlib,json,subprocess,sys
root=Path(sys.argv[1])
for name,spec in json.loads(Path('packaging/tools.lock.json').read_text()).items():
    target=root/(name+'.AppImage')
    if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest()!=spec['sha256']:
        subprocess.run(['curl','--fail','--location','--retry','3','--connect-timeout','20',
                        '-H','Accept: application/octet-stream',spec['url'],'-o',str(target)],check=True)
    if hashlib.sha256(target.read_bytes()).hexdigest()!=spec['sha256']:
        raise SystemExit('Deployment tool checksum mismatch: '+name)
    target.chmod(0o755)
    unpack=root/name
    if not unpack.exists():
        unpack.mkdir()
        subprocess.run([str(target),'--appimage-extract'],cwd=unpack,check=True,stdout=subprocess.DEVNULL)
PY
export PATH="$build/tools/linuxdeploy-plugin-qt/squashfs-root/usr/bin:$PATH"
export QMAKE=/usr/bin/qmake6
export EXTRA_PLATFORM_PLUGINS='libqoffscreen.so'
export VERSION="$version"
export OUTPUT="$dist/LoungeCat-${version}-x86_64.AppImage"
# Explicitly bundle OpenSSL as well as ELF-linked dependencies. Qt dlopens it.
"$build/tools/linuxdeploy/squashfs-root/AppRun" --appdir "$stage" \
    --desktop-file packaging/io.github.binkiewka.LoungeCat.desktop \
    --icon-file "$stage/usr/share/icons/hicolor/256x256/apps/loungecat.png" \
    --library /usr/lib/x86_64-linux-gnu/libssl.so.3 \
    --library /usr/lib/x86_64-linux-gnu/libcrypto.so.3 --plugin qt
python3 packaging/complete-appdir.py "$stage" "$build/tools/linuxdeploy/squashfs-root/AppRun"
python3 packaging/bundle-notices.py "$stage"
"$build/tools/linuxdeploy/squashfs-root/AppRun" --appdir "$stage" --output appimage

# A test-only probe is kept OUTSIDE both application packages. Runtime jobs have
# no Qt development packages or build tree; it loads each package's actual Qt.
c++ -std=c++20 -fPIC packaging/runtime-probe.cpp -o "$dist/test-support/runtime-probe" \
    $(pkg-config --cflags --libs Qt6Widgets Qt6Sql Qt6Network Qt6Svg)
cp packaging/smoke.py "$dist/test-support/"
cp tests/fixtures/localhost-{cert,key}.pem "$dist/test-support/"
git archive --format=tar.gz --prefix="LoungeCat-${version}-source/" \
    --output="$dist/LoungeCat-${version}-source.tar.gz" HEAD
git rev-parse HEAD > "$dist/SOURCE_COMMIT"
dpkg-query -W -f='${Package}\t${Version}\n' > "$dist/build-dependencies.tsv"
python3 - "$dist" "$version" <<'PY'
from pathlib import Path
import hashlib,json,sys
d=Path(sys.argv[1]); files=sorted([*d.glob('*.AppImage'),*d.glob('*.deb'),*d.glob('*-source.tar.gz')])
assert len(files)==3,files
(d/'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in files))
(d/'package-manifest.json').write_text(json.dumps({'version':sys.argv[2], 'architecture':'x86_64',
 'buildBaseline':'Debian 12 / glibc 2.36', 'sourceCommit':(d/'SOURCE_COMMIT').read_text().strip(),
 'artifacts':[{'file':p.name,'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in files]},indent=2)+'\n')
PY
