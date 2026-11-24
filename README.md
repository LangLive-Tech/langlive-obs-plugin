# LangLive OBS Plugin

English | [繁體中文](README.zh-TW.md)

An OBS Studio plugin for streaming to [LangLive (浪LIVE)](https://www.lang.live/). It helps
streamers go live with correct settings: log in, configure the live room, apply the recommended
encoder settings in one click, and start or stop the broadcast without leaving OBS.

> **Status:** in QA testing, not released yet.

## Features

- **Login**: sign in with a phone number and an SMS code, or scan a QR code with the LangLive
  app; third-party accounts are not in the first release. Only accounts enabled for OBS
  streaming can log in, and the backend can end a login at any time (the plugin then asks to log
  in again).
- **Go live**: set the cover, title, push notification text, events, and channel, choose a streaming
  scenario, then start or end the broadcast directly from the plugin. Upload Cover picks and crops an
  image and uploads it right away; a refused upload (e.g. the daily limit) keeps the previous cover.
- **Recommended settings**: saving the go-live settings applies the resolution, frame rate,
  bitrate, and keyframe interval for the selected scenario and the account's orientation
  (landscape / portrait).
- **Pre-stream check**: before a live from the plugin starts, and again as its stream starts, the
  plugin checks the stream settings and lists each problem that doesn't meet LangLive's
  requirements, with a one-click fix. Streams started with OBS's own Start Streaming are never
  checked or changed.
- **Diagnostics export**: bundle OBS and plugin logs, crash reports, and settings into a local
  zip file to share with LangLive support. Sensitive data is redacted, and nothing is uploaded
  automatically.
- **Updates**: Check for Updates shows new versions; a required update is also shown from any menu
  item. Downloads and verifies the installer, then closes OBS so the installer can run. Versions
  below the minimum supported version must be updated before logging in or going live.

### Streaming requirements

All scenarios use H.264 video, AAC audio, and a 2-second keyframe interval.

| Scenario | Landscape | Portrait | FPS | Video bitrate |
|---|---|---|---|---|
| Chat / general (recommended) | 1280 × 720 | 720 × 1280 | 25 | 3000 kbps |
| Performance / high quality | 1920 × 1080 | 1080 × 1920 | 30 | 6000 kbps |

## Supported platforms

| Host | macOS (Apple Silicon / Intel) | Windows x64 |
|---|---|---|
| OBS Studio 31+ | ✓ | ✓ |

## Installation

Until the first release, installers go to testers only; after it, they will be on the LangLive
website. Run the installer for your platform with OBS closed (the installer refuses while OBS runs):

- macOS: `langlive-obs-plugin-macos-universal-vX.Y.Z.pkg` (one universal package for Apple
  Silicon and Intel)
- Windows: `langlive-obs-plugin-windows-x64-vX.Y.Z.exe`

After installation, the plugin keeps itself up to date.

The macOS installer is signed with Developer ID and notarized by Apple, so it opens with a
double-click. The Windows installer is not code-signed yet: SmartScreen warns, and on Windows 11
with Smart App Control on, OBS cannot load the plugin at all.

## Architecture

The plugin is written in C++17 with Qt 6 and CMake, and is organized in four layers:

| Layer | Path | Responsibility |
|---|---|---|
| UI | `src/ui/` | The LangLive menu and dialogs (Qt). The UI only sends intents to the application layer and never calls the OBS API directly |
| Application | `src/app/` | `PluginController`: login, go-live, recommended settings, and the pre-stream check. It reaches OBS and local storage only through the ports in `ports.hpp` |
| Adapters | `src/obs/` | Implement the ports with the OBS API: read and apply stream settings, start and stop streaming, store the go-live settings, export diagnostics |
| Core | `core/` | Stream specs, validation, version comparison, log redaction |

`core/` and `src/app/` have no dependency on OBS or Qt, so they are unit-tested on their own.
The LangLive backend API client (`src/obs/http-langlive-api.cpp`) is one of the adapters. The login token is kept in
the system's secure storage: the login keychain on macOS, Credential Manager on Windows. Neither needs admin rights.
QA builds keep their own login and settings (a separate keychain item and a `qa/` config folder), so switching
between a QA build and a production build on one computer never mixes the two environments.

The client-side checks exist for user experience. The LangLive backend is the source of truth:
login, stream keys, quality limits, and the minimum supported version all come from its API.

Design principle: a live from the plugin is started and ended only through the plugin's UI (Go Live / End Live).
OBS cannot be stopped from stopping a stream, so when a plugin live's stream is stopped any other way (OBS's Stop
Streaming, a hotkey, OBS giving up reconnecting), the live is ended for good: it is closed on the backend, the stream
key is cleared from OBS, and a notice with only OK tells the streamer. The next live starts from the plugin again.
Streams started with OBS's own Start Streaming are not the plugin's: it never checks, changes, or ends them.

## Building from source

Requirements:

- CMake 3.28+
- macOS: Xcode 16+
- Windows: Visual Studio 2022 or later

The first configure downloads OBS's prebuilt dependencies, Qt 6, and the obs-studio source, which
can take several minutes.

```sh
# macOS (universal: arm64 + x86_64)
cmake --preset macos
cmake --build --preset macos

# Windows
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
```

To build the installers (output in `release/`):

```sh
# macOS: release/langlive-obs-plugin-macos-universal-vX.Y.Z.pkg
./scripts/macos/package-macos-installer.sh

# Windows (needs Inno Setup 6): release/langlive-obs-plugin-windows-x64-vX.Y.Z.exe
cmake --install build_x64 --config RelWithDebInfo --prefix install
pwsh scripts/windows/package-windows-installer.ps1
```

The installers built this way are unsigned. CI signs and notarizes the macOS installers for
version tags; Windows code signing is planned.

On Windows 11, Smart App Control blocks unsigned DLLs, so OBS cannot load a plugin you built
yourself while it is on. Turn it off on development machines (Windows Security > App & browser
control > Smart App Control).

A build from source uses a built-in mock backend (any phone number and code log in) and checks
for no updates: the backend and update URLs are empty by default, and CI sets them for the QA
and production builds. To use a real backend, configure with
`-DLANGLIVE_API_BASE_URL=https://...`, `-DLANGLIVE_UPDATE_MANIFEST_URL=https://.../latest.json`, and
`-DLANGLIVE_WEBSITE_URL=https://...` (empty: the production website). All build, CI, and installer
scripts used for the official releases are included in this repository.

## Running tests

Unit tests cover `core/` and the application layer in `src/app/`. They need neither OBS nor Qt,
so they build anywhere with CMake and a C++17 compiler:

```sh
cmake -S tests -B build_tests
cmake --build build_tests
ctest --test-dir build_tests --output-on-failure
```

The application layer talks to OBS only through the interfaces in `src/app/ports.hpp`; tests
replace them with in-memory fakes (`tests/app/fakes.hpp`).

UI tests run the plugin's Qt dialogs without OBS (offscreen, no display needed) and check their
behavior: which fields are locked while live, what a click opens, which notices appear. They use
the Qt and obs-studio headers from `.deps/`, so configure the plugin once first:

```sh
# macOS
cmake -S tests/ui -B build_ui_tests -G Ninja
cmake --build build_ui_tests
ctest --test-dir build_ui_tests --output-on-failure

# Windows (Visual Studio generator)
cmake -S tests/ui -B build_ui_tests
cmake --build build_ui_tests --config RelWithDebInfo
ctest --test-dir build_ui_tests -C RelWithDebInfo --output-on-failure
```

## Contributing

See [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md) for how to report issues and submit pull
requests.

## License

This plugin links against libobs and is licensed under the GNU General Public License v2.0 or
later. See [LICENSE](LICENSE).

### Source code for the official installers

Released versions are distributed through the LangLive website and the plugin's built-in updater
(GitHub Releases are planned). Whichever way you got it, the complete corresponding source code
for version `X.Y.Z` is the tag `vX.Y.Z` in this repository. Once a version is released, its tag is
never deleted or moved; before that, a tag may be published again while QA tests it.

### Third-party software

The installers include the license texts and notices of the third-party components they contain.

### Trademarks

The GPL covers the code only. The names "LangLive" and "浪LIVE" and the LangLive logos are
trademarks of LangLive and are not licensed under the GPL. If you distribute a modified version,
do not use them in a way that suggests it is an official LangLive release.
