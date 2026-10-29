FROM gentoo/portage@sha256:33c8c36bb0b7c26cb0c5b4cdece4e47424cfd4da643746c76b5b00a7d1b12c21 AS portage
FROM gentoo/stage3@sha256:1ca7647d1ce968d02c2858ee4201e2173675fd3fc0754a8acd583d9288b75478 AS base
COPY --from=portage /var/db/repos/gentoo /var/db/repos/gentoo
RUN ln -sfn /var/db/repos/gentoo/profiles/default/linux/amd64/23.0/desktop /etc/portage/make.profile \
    && printf '\nABI_X86="64"\n' >> /etc/portage/make.conf
FROM base AS build
ENV FEATURES="-network-sandbox -pid-sandbox -ipc-sandbox -mount-sandbox"
RUN mkdir -p /etc/portage/package.use && printf 'x11-base/xorg-server xvfb\ndev-qt/qtbase -accessibility -cups -gtk -icu -opengl -libproxy -vulkan -wayland\n' > /etc/portage/package.use/loungecat-ci
# Prefer signed Gentoo binaries with matching USE flags; missing tool/flag
# variants are built by Portage from verified distfiles. LoungeCat itself is
# always built from the exact release source during the offline test job.
RUN emerge --getbinpkg --binpkg-respect-use=y --oneshot --verbose \
    dev-build/cmake dev-build/ninja dev-util/pkgconf dev-vcs/git \
    dev-qt/qtbase:6 dev-qt/qtsvg:6 media-libs/alsa-lib \
    dev-util/desktop-file-utils x11-misc/xdotool \
    media-fonts/dejavu
RUN emerge --getbinpkg --oneshot x11-base/xorg-server[xvfb] dev-util/pkgcheck
WORKDIR /src
