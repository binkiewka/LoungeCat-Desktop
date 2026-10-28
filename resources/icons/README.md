# LoungeCat branding

`logo_transparent.png` is the original Kotlin desktop cat artwork, preserved
byte-for-byte. The native window icon, sidebar and 15%-opacity chat watermark
reuse it. `tray_icon.png` is also preserved from the Kotlin application.
The sidebar and watermark reverse the monochrome RGB colours on dark themes
(leaving alpha unchanged) so the black cat remains visible on black backgrounds.

`icon.png` is a 256 × 256 transparent PNG scaled from `logo_transparent.png`
with Qt's smooth, aspect-preserving image scaling. It is installed as
`hicolor/256x256/apps/loungecat.png` for the desktop launcher, deb and AppImage.
It replaces the earlier purple “LC” placeholder; no replacement cat artwork was
generated. `logo.png` is retained for asset/history compatibility, but its baked-in
checkerboard is not used as a transparent UI background.

See [third-party notices](../../THIRD_PARTY_NOTICES.md) for the retained Kotlin
copyright and MIT license notice.
