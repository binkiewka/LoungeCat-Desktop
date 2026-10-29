#!/usr/bin/env bash
set -euo pipefail
# The complete test container must be started with --network none. Package
# dependencies were installed with signature verification in the build image.
python3 - <<'CHECK'
from pathlib import Path
assert not any(line.split()[1]=='00000000' for line in Path('/proc/net/route').read_text().splitlines()[1:]), 'Expected offline container, without a default route'
CHECK
version=$(python3 -c 'import json; print(json.load(open("/packages/package-manifest.json"))["version"])')
mkdir -p /var/db/repos/loungecat /var/cache/distfiles /etc/portage/repos.conf \
    /etc/portage/package.accept_keywords /etc/portage/package.use /results /packages/test-support
cp -a /packages/overlay/. /var/db/repos/loungecat/
printf '[loungecat]\nlocation = /var/db/repos/loungecat\nauto-sync = no\n' > /etc/portage/repos.conf/loungecat.conf
printf 'net-irc/loungecat ~amd64\n' > /etc/portage/package.accept_keywords/loungecat
printf 'net-irc/loungecat alsa test\n' > /etc/portage/package.use/loungecat
cp "/sources/LoungeCat-${version}-source.tar.gz" /var/cache/distfiles/
pkgcheck scan --exit error --repo /var/db/repos/loungecat net-irc/loungecat | tee /results/pkgcheck.log
# Preserve Portage's ordinary sandbox/user checks. Docker provides the outer
# offline network namespace; unsharing new namespaces is unavailable in Docker.
export FEATURES="test -network-sandbox -pid-sandbox -ipc-sandbox -mount-sandbox"
export MAKEOPTS="-j${BUILD_JOBS:-2}"
emerge --oneshot --verbose --usepkg=n net-irc/loungecat 2>&1 | tee /results/emerge.log
work="/var/tmp/portage/net-irc/loungecat-${version}/work"
# Portage may remove work after install: package test success remains in its log.
test -x /usr/bin/loungecat
c++ -std=c++20 -fPIC /src/packaging/runtime-probe.cpp -o /packages/test-support/runtime-probe \
    $(pkg-config --cflags --libs Qt6Widgets Qt6Sql Qt6Network Qt6Svg)
cp /src/packaging/smoke.py /src/tests/fixtures/localhost-{cert,key}.pem /packages/test-support/
python3 - <<'INVENTORY'
from pathlib import Path
p=Path('/var/db/pkg'); Path('/packages/gentoo-build-dependencies.tsv').write_text(''.join(str(x.relative_to(p))+'\n' for x in sorted(p.glob('*/*')) if x.is_dir()))
INVENTORY
python3 /packages/test-support/smoke.py --format gentoo --dist /packages --output /results
