# Copyright 2024 LoungeCat
# Distributed under the terms of the MIT License

EAPI=8

inherit desktop xdg java-pkg-2

DESCRIPTION="Modern IRC Client for Linux"
HOMEPAGE="https://github.com/binkiewka/LoungeCat-Desktop"
SRC_URI="https://github.com/binkiewka/LoungeCat-Desktop/archive/v${PV}.tar.gz -> ${P}.tar.gz"

LICENSE="MIT"
SLOT="0"
KEYWORDS="~amd64"

RDEPEND="
    >=virtual/jre-17
    media-libs/alsa-lib
    x11-libs/libX11
    x11-libs/libXext
    x11-libs/libXi
    x11-libs/libXrender
    x11-libs/libXtst
"
DEPEND="
    >=virtual/jdk-17
    ${RDEPEND}
"

S="${WORKDIR}/LoungeCat-Desktop-${PV}"

src_prepare() {
    default
    # Make gradlew executable
    chmod +x gradlew || die
}

src_compile() {
    export JAVA_HOME=$(java-config -g JAVA_HOME)
    
    # Run the distribution task
    # This downloads dependencies and Gradle if needed
    ./gradlew :desktopApp:createDistributable --no-daemon --info || die "Gradle build failed"
}

src_install() {
    local app_dir="/opt/${PN}"
    dodir "${app_dir}"
    
    # Copy the built application
    cp -r desktopApp/build/compose/binaries/main/app/* "${ED}/${app_dir}/" || die "Install failed"
    
    # Create valid launcher symlink
    dosym "${app_dir}/LoungeCat" /usr/bin/loungecat
    
    # Install icon and desktop file
    newicon shared/src/commonMain/composeResources/drawable/icon.png loungecat.png
    make_desktop_entry loungecat "LoungeCat" loungecat "Network;IRCClient;" "MimeType=x-scheme-handler/irc;x-scheme-handler/ircs;"
}
