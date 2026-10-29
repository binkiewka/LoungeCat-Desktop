# LoungeCat

LoungeCat is an IRC client for Linux, built with C++20 and Qt 6 Widgets.

[Download](https://github.com/binkiewka/LoungeCat-Desktop/releases/latest) · [Build from source](#build-from-source) · [Screenshot gallery](docs/screenshots/README.md)

![LoungeCat with the Obsidian Veil theme](docs/screenshots/theme-obsidian-veil.png)

## Features

- Connect to multiple IRC networks, join channels and send private messages.
- View chats in a single pane, a horizontal or vertical split, or a four-pane grid, with separate drafts for each conversation.
- Search local chat history, save text logs, and configure highlights and notifications.
- Use TLS, SASL PLAIN, NickServ authentication, and HTTP or SOCKS5 proxies.
- Choose from seven built-in themes or create a custom theme; adjust fonts, colours and timestamps.
- Display link previews, upload images, and enable spellchecking and notification sounds.

### Long messages

To avoid flooding a channel with text, LoungeCat can upload a long message to
[0x0.st](https://0x0.st) and send a link with its line and character counts instead.
This is enabled by default for messages over **400 characters** or **five lines**.
In **Settings → Media & Spellcheck**, you can disable **Auto-upload long messages
to 0x0.st Pastebin** or change the character threshold. No API key is needed.

Automatic paste uploads are blocked in private messages, private channels and
channels whose privacy status is unknown; the draft is kept instead.

### Image uploads

Images are hosted on [ImgBB](https://imgbb.com); LoungeCat posts the resulting
image link to the current channel or private conversation.

1. Get an API key from [api.imgbb.com](https://api.imgbb.com/).
2. Enter it in **Settings → Media & Spellcheck → ImgBB API Key** and save.
3. Use the **Upload image** button to choose a file, or type
   `/uploadimage /path/to/image.png`. Use `/uploadimage` without a path to upload
   an image from the clipboard. `/img` is an alias.

Still images are supported; animated images are not. The upload limit is 8 MiB.
Both features upload content to third-party services, not to the IRC server;
anyone with the resulting link may be able to view it.

## Install

Download a package from [GitHub Releases](https://github.com/binkiewka/LoungeCat-Desktop/releases/latest). The commands below use version **2.0.1**; substitute the version you downloaded.

### AppImage

For x86_64 Linux with glibc 2.36 or newer. Tested on Debian 12 and Ubuntu 24.04.

```sh
chmod +x LoungeCat-2.0.1-x86_64.AppImage
./LoungeCat-2.0.1-x86_64.AppImage
```

If FUSE is unavailable, run it with `APPIMAGE_EXTRACT_AND_RUN=1`.

### Debian / Ubuntu

```sh
sudo apt install ./loungecat_2.0.1_amd64.deb
loungecat
```

### Fedora 44

```sh
sudo dnf install ./loungecat-2.0.1-1.fc44.x86_64.rpm
loungecat
```

### Gentoo

A source ebuild and local overlay are available for amd64. See the [Gentoo installation instructions](docs/PACKAGING.md#gentoo-amd64-overlay).

The [packaging guide](docs/PACKAGING.md) also covers checksums and runtime requirements. The AppImage uses X11/XWayland; the RPM is built specifically for Fedora 44.

## Build from source

Requires a C++20 compiler, CMake 3.24 or newer, and Qt 6.4 or newer.

On Debian 12 / Ubuntu 24.04, install the dependencies:

```sh
sudo apt install git build-essential cmake pkg-config qt6-base-dev libqt6svg6-dev libqt6sql6-sqlite qt6-qpa-plugins libasound2-dev
```

Then clone, build and run:

```sh
git clone https://github.com/binkiewka/LoungeCat-Desktop.git
cd LoungeCat-Desktop
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/release --parallel 2
./build/release/loungecat
```

See [BUILD.md](BUILD.md) for optional ALSA support, running tests and building packages.

## Screenshots

The four-pane layout lets you keep several channels visible at once:

![Four-pane layout with a different channel in each pane](docs/screenshots/grid-four-panes.png)

The [full gallery](docs/screenshots/README.md) shows all seven themes, settings and server setup.

## Settings and history

Settings, saved networks and chat history are stored in `~/.loungecat/`.
To use a different profile directory, set `LOUNGECAT_DATA_DIR` before launching:

```sh
LOUNGECAT_DATA_DIR="$HOME/.loungecat-work" loungecat
```

## License

GPL-3.0-or-later. See [LICENSE](LICENSE), [COPYRIGHT](COPYRIGHT) and [third-party notices](THIRD_PARTY_NOTICES.md).
