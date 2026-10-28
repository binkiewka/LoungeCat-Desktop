# Screenshot gallery

These images show the **actual native LoungeCat Desktop 2.0 application**, captured
on 28 October 2026 using Qt 6.4.2. They are not generated mockups or retouched UI.
The gallery includes the restored original cat branding and chat watermark on
`master` (see the manifest's source revision), which postdate the v2.0.0 release.

- The unchanged Linux Release executable was run with a disposable profile and a
  synthetic IRC peer inside a network namespace with only loopback enabled.
- A capture-only driver used existing public widget controls to select themes,
  switch Settings tabs, choose pane conversations and open server setup. No
  production source, models, styles or rendering implementation were replaced.
- Qt's offscreen display backend and `QWidget::grab()` rendered the actual widgets.
  Window-manager borders are absent; desktop font/style details can vary.
- Conversations, nicknames and network names are fictional demo data. No normal
  user profile, real chat history, account credentials or public IRC network was used.
- All seven theme images show the same conversation at 1440 × 900. The four-pane
  layout is 1920 × 1080; Settings views are 850 × 580. Server setup shows untouched
  empty fields at its natural dialog size.

The [capture manifest](capture-manifest.json) records image dimensions/checksums,
the source revision and exact executable hash. Only screenshots and documentation
are published here; the temporary demo profiles and capture instrumentation are not
part of the application or its normal build.

## Included views

| Group | Views |
| --- | --- |
| Themes | Obsidian Veil, Dark, Light, High Voltage, Neon Night, Retro Warmth, Stormy Sea |
| Settings | Appearance, Chat & Logs, Notifications & Audio, Media & Spellcheck |
| Workspace | Four independent chat panes |
| Connection | New server setup |
