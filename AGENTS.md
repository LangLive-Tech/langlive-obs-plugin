# AGENTS.md

Guidance for AI coding agents working in this repository. Human contributors: see
[README.md](README.md) and [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md).

## Project

An OBS Studio plugin for streaming to LangLive (浪LIVE): login, go-live dialog, recommended encoder
settings, a pre-stream check, diagnostics export, and updates. C++17, Qt 6, CMake, built on
[obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate).

- Supported: OBS Studio 31+ on macOS (universal) and Windows x64
- Not supported: Linux, PRISM Live Studio. Do not add code paths for them
- Work is tracked in [docs/TODO.md](docs/TODO.md) (both languages). Check items off (`- [x]`) when you finish them
  instead of removing them, and add items you find

## Design principles

- A live from the plugin is started and ended only through the plugin's UI (Go Live / End Live). OBS has no veto on
  stopping a stream, so a plugin live whose stream stops any other way (OBS's Stop Streaming, a hotkey, OBS giving up
  reconnecting) is ended for good: closed on the backend (`streaming/close`), the stream key cleared from OBS
  (`ClearStreamKey`), and a notice with only OK. Never restart the stream or offer to cancel
- Streams started with OBS's own Start Streaming are not the plugin's: never check, change, or end them

## Layout and layering

| Path | Layer | May depend on |
|---|---|---|
| `core/` | Domain logic: stream specs, validation, versions, redaction | C++17 standard library only |
| `src/app/` | Application layer: `PluginController`, ports (`ports.hpp`) | `core/` only. No Qt, no libobs |
| `src/obs/` | Adapters implementing the ports with libobs / OBS frontend API | `core/`, `src/app/`, libobs, Qt Core |
| `src/ui/` | Qt menu and dialogs; `resources/` holds icons compiled in as a Qt resource | everything above; talks to OBS only through `PluginController` |
| `src/plugin-main.cpp` | Module entry, wiring, frontend event forwarding | everything |
| `tests/` | Unit tests for `core/` and `src/app/` (doctest) | no OBS, no Qt |
| `tests/ui/` | UI tests: the Qt dialogs run offscreen with fakes (doctest) | Qt and libobs headers from `.deps/`; stubs in `obs_stubs.cpp` |
| `data/locale/` | UI strings: `en-US.ini`, `zh-TW.ini`, `zh-CN.ini`; OBS in another language shows English | |
| `third_party/` | Vendored libraries, unmodified, each with its license and a README naming the version | |

Rules:

- Keep `core/` and `src/app/` free of Qt and libobs, so they stay unit-testable on any platform
- UI code never calls `obs_*` or `obs_frontend_*` directly; add a controller method and, if needed, a
  port method implemented in `src/obs/`
- New business rules go in `core/` with tests; new use cases go in `PluginController` with tests
- Namespaces: `langlive::obsplugin::core`, `langlive::obsplugin` (app), `langlive::obsplugin::obs`,
  `langlive::obsplugin::ui`. Core headers live under `core/include/langlive/obsplugin/core/`

## Build and test

```sh
# Unit tests (fast, no OBS or Qt; run these for every change to core/ or src/app/)
cmake -S tests -B build_tests -G Ninja
cmake --build build_tests
ctest --test-dir build_tests --output-on-failure

# UI tests (Qt dialogs headless; run for every change to src/ui/; needs .deps/ from a plugin configure).
# On Windows: no -G Ninja, add --config RelWithDebInfo to the build and -C RelWithDebInfo to ctest
cmake -S tests/ui -B build_ui_tests -G Ninja
cmake --build build_ui_tests
ctest --test-dir build_ui_tests --output-on-failure

# Plugin, macOS (first configure downloads OBS deps into .deps/, several minutes)
cmake --preset macos
cmake --build --preset macos

# Plugin, Windows
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo

# Installers (unsigned locally; CI signs macOS ones for version tags), output in release/
./scripts/macos/package-macos-installer.sh
cmake --install build_x64 --config RelWithDebInfo --prefix install   # Windows, then:
pwsh scripts/windows/package-windows-installer.ps1
```

- Until the backend APIs exist, `PluginController` mocks them (see `docs/TODO.md`); phone login always succeeds
- After adding or removing source files, re-run the configure step (`cmake --preset ...`); the Xcode build does not
  always pick up `CMakeLists.txt` changes and fails at link time
- Update testing: configure with `-DLANGLIVE_UPDATE_MANIFEST_URL=http://127.0.0.1:8765/latest.json` and run
  `scripts/dev/serve-update-manifest.py`. The manifest format is in [docs/UPDATE-MANIFEST.md](docs/UPDATE-MANIFEST.md).
  The value stays in the CMake cache; reconfigure with
  `-DLANGLIVE_UPDATE_MANIFEST_URL=` before building anything to ship
- Windows 11 Smart App Control blocks unsigned DLLs and installers; a self-signed certificate does not help. Turn it
  off on development machines
- `.deps/`, `build_macos/`, `build_x64/`, `build_tests/`, `build_ui_tests/` are local build output; never commit them
- UI tests find widgets by their visible text (`ui::Text("Key")`) or object name, and check modal dialogs with
  `WithModal` (`tests/ui/ui_test_support.hpp`). New UI behavior gets a UI test
- CI (`.gitlab-ci.yml`) runs on tagged shell-executor runners (`obs-mac`, `obs-windows`): gitleaks, the
  unit tests on macOS (`-Wall -Wextra -Wpedantic`) and Windows (MSVC), then the plugin builds with warnings as errors,
  packaged as the `.pkg` and the Inno Setup `.exe` (job artifacts, kept two weeks), and the UI tests on both
  (`ui-tests:*`, after the build that sets up `.deps/`; build them in-tree). Keep it green
- CI runner setup: the macOS runner needs Xcode with `sudo xcodebuild -runFirstLaunch` done after every Xcode update
  (CMake otherwise finds no compiler), plus CMake and Ninja from Homebrew. The Windows runner needs Visual Studio with
  the C++ workload and Inno Setup 6 installed for all users (`winget install JRSoftware.InnoSetup --scope machine`),
  because the runner service does not run as the account that installed it. Windows CI scripts must work in Windows
  PowerShell 5.1: read text files with `-Encoding UTF8`, and do not use `$PSScriptRoot` in `param()` defaults. The
  jobs keep `.deps/` in the runner's checkout (`GIT_CLEAN_FLAGS` for every job, since they share it), so only the
  first build downloads the OBS dependencies. Before configuring, the build jobs run `scripts/ci/prefetch-deps.cmake`:
  it removes archives in `.deps/` whose hash does not match `buildspec.json` (the configure step checks only while
  downloading) and, if the CI variable `OBS_DEPS_MIRROR` is set, downloads missing ones from that server, which holds
  them under the same paths as GitHub. Set the mirror in the GitLab project settings, never in the repository
- Release environments, one S3 prefix each (`S3_BUCKET` / `S3_REGION` project variables; S3 uploads use curl SigV4,
  no AWS CLI):
  - `dev`: every default-branch build replaces the snapshot in `dev/`, named `...-vX.Y.Z-SNAPSHOT` (version X.Y.Z
    plus the changes since; `scripts/ci/upload-s3.sh dev --snapshot-name`), built for QA without update checks
  - CI for a `vX.Y.Z` tag builds both: QA (`build:*`) and production (`build-prod:*`); `release` creates the tag's
    GitLab Release with all four installers in the package registry (QA ones named `...-vX.Y.Z-qa.*`;
    `scripts/ci/gitlab-release.sh`)
  - `test` (QA): `deploy:qa`, automatic, publishes the QA build to `test/<version>/` and `test/latest.json`
  - `prod`: `deploy:prod`, manual after QA, takes the production installers from the Release (checking their
    SHA-256), publishes `prod/<version>/` and `prod/latest.json`, served by the CDN at `PROD_DOWNLOAD_URL` whose root
    is the bucket's `prod/`, and marks the Release released (`scripts/ci/deploy-prod.sh`). QA tests the QA build;
    the production build differs only in the compiled URLs
- Builds compile in their environment's `QA_*` or `PROD_*` variables (`*_API_BASE_URL`, `*_WEBSITE_URL`,
  `*_UPDATE_MANIFEST_URL`), mapped onto `LANGLIVE_*` job variables. Never define `LANGLIVE_*` project variables: they
  would override the mapping. `scripts/ci/check-release-config.sh` refuses a tag that differs from `buildspec.json`'s
  version or a build without an API or manifest URL
- Version policy (`scripts/ci/publish-manifest.sh`): a new major version is a forced update (`min_supported_version`
  becomes it), minor and patch are optional; `UPDATE_MIN_SUPPORTED_VERSION` overrides; an older version than the
  published one is refused. QA fixes are new patch versions
- `TEST_*` keys write `dev/` and `test/`, the protected `PROD_*` keys `prod/` (only protected `v*` tags and the
  protected default branch get them). Every job that uses a pair names its environment (`TEST_*`: `dev`, `test`;
  `PROD_*`: `production`), so the pairs can be scoped to those environments and a job without one gets no S3 key.
  The variables' scopes are DevOps' to set in the project settings; do not change them. The manual
  `check:s3-access:test` / `check:s3-access:prod` jobs (`scripts/ci/check-s3-access.sh test|prod`) check that each
  key writes and reads its own prefix and is refused under the other's
- `sync:github` runs after `deploy:prod` on obs-mac (`scripts/ci/sync-github.sh`): publishes the version on the
  public GitHub mirror (`GITHUB_REPOSITORY`, owner/name; the protected `GITHUB_PUSH_TOKEN`, a fine-grained token
  with Contents read and write on that repository only) as one commit on GitHub's `main` with exactly the tag's
  tree, tagged `vX.Y.Z`, plus the GitHub Release with the production installers. This repository's history is not
  published, so GitHub's commit hashes differ from ours. Only released versions go to GitHub (it checks the GitLab
  Release), in version order; it never moves a tag or force-pushes `main`, and running it again skips what is there. The manual `sync:github:tag` job on `main`, played with
  `SYNC_TAG=vX.Y.Z`, publishes a version whose tag pipeline had no `sync:github`. The manual `check:github-access` job on `main`
  (`scripts/ci/check-github-access.sh`) checks the token can read and push (dry run) without changing anything
- `flush-cdn:prod` runs after `deploy:prod` on the company's shared `ll` runner, the only one that reaches the
  internal CDN flush API: no checkout (`GIT_STRATEGY: none`), no keys, the API URL from the `CDN_FLUSH_URL` variable,
  the paths from `deploy:prod`'s `cdn-flush-paths.txt` artifact (the API takes no `*`). It may fail (warning only). Never give a job on a shared runner an environment that S3 keys are scoped to
- QA builds (`-DLANGLIVE_QA_BUILD=ON`: the `build:*` jobs; never `build-prod:*`) read `qa-override.json` from the
  plugin's config folder, `{"update_manifest_url": "https://..."}`, on every update check (logged; the update UI
  looks the same as production). QA builds name the menu 浪LIVE(Test). `deploy:qa` publishes scenarios for it to
  `test/scenarios/` (`scripts/ci/publish-scenarios.sh`): optional, forced, uptodate, bad-sha, broken. Production builds contain none of it; keep QA-only features behind `LANGLIVE_QA_BUILD`
- macOS signing (version tags, both `build:macos` and `build-prod:macos`): `source scripts/ci/macos-signing.sh`
  unlocks the obs-mac runner's login keychain with the protected `MACOS_KEYCHAIN_PASSWORD` and exports the Developer
  ID identities; the package step signs the plugin in the payload and the `.pkg` with them
  (`create-package.cmake.in`); `scripts/ci/notarize-macos.sh` notarizes with the keychain's `langlive-notary`
  notarytool profile, staples, and checks Gatekeeper. A failure fails the build: never ship an unsigned macOS
  release. `MACOS_SIGN=1` on a main pipeline, or the manual `check:macos-signing` job, tries it. Certificates,
  passwords and the Apple ID never go in the repository. Re-importing a certificate or key on the runner needs the
  key partition list (`security set-key-partition-list`) and "Always Allow" again
- A version tag can be published again until production offers it or a newer one (`prod/latest.json`):
  `scripts/release/retag.sh vX.Y.Z [<commit>]` deletes its GitLab Release, package and tag and pushes it again; the
  pipeline replaces its `test/` and, after `deploy:prod`, `prod/` files. It refuses when production's version cannot
  be read, or the Release says released. Once released, a version is final: a fix is a new version
- A successful compile is not a runtime test. Say so explicitly when you have not run the plugin in OBS

## Code style

- Format with clang-format (`.clang-format`, version 16+): tabs, 120 columns. Run
  `clang-format -i` on changed C/C++ files
- Types and functions `PascalCase`, members `camelCase_` with trailing underscore, constants
  `kPascalCase`, files `kebab-case` in `src/`, `snake_case` in `core/` and `tests/`
- Comments explain why, not what. English only in code and comments

## OBS API pitfalls

- Reference counting differs per function. `obs_frontend_get_streaming_service()` returns a borrowed
  pointer (no release); `obs_frontend_get_streaming_output()` adds a reference (call
  `obs_output_release`). Release every `obs_data_t` you get, `bfree` every `char *` from OBS
- OBS has no pre-start veto. The pre-stream check runs on `OBS_FRONTEND_EVENT_STREAMING_STARTED` and
  stops the stream; never block inside a frontend event callback, queue UI with
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`
- Output settings live in different places per output mode: simple mode uses `basic.ini`
  `[SimpleOutput]`, advanced mode uses `streamEncoder.json`. Handle both
- `obs_module_config_path()` does not create the directory; create it before writing

## UI

- Dialogs use the LangLive website's dark theme on every OBS theme. Call `ApplyDialogStyle()` (`ui/style.hpp`) and
  use its color tokens (`{brand}`, `{surface}`, ...) and object names (`primaryButton`, `fieldLabel`, ...) instead of
  hard-coded colors
- Use `ShowMessage()` (`ui/message-dialog.hpp`) for notices, never `QMessageBox`, which follows the system look
- The go-live dialog is modeless and only hidden when closed, so its form survives until logout. On macOS, a closing
  native file picker or frameless dialog brings OBS's main window to the front; raise the dialog again afterwards
- Buttons that start or end a live must not be default buttons (`setAutoDefault(false)`)

## Strings and docs

- Every UI string goes through `ui::Text("Key")`; add the key to **all three** of `data/locale/en-US.ini`,
  `data/locale/zh-TW.ini`, and `data/locale/zh-CN.ini`. Do not use `\"` inside values
- Repository docs are bilingual: `X.md` (English) and `X.zh-TW.md` (Traditional Chinese), with a
  language switcher under the title. Docs live in `docs/`; only the READMEs stay at the root. Update both
  languages in the same change

## Git and publishing

- GitHub Flow: branch from `main` as `feature/<topic>` or `fix/<topic>`, merge by MR (squash). Never push
  to `main` directly. Version tags (`vX.Y.Z`, the only tags CI runs for) are created by
  maintainers only
- This repository is mirrored publicly to GitHub. Never put internal URLs, internal hostnames, Jira links,
  accounts, stream keys, or QA backend addresses in code, comments, docs, or commit messages. Reference
  Jira issues in the MR description instead
- Production defaults may be committed; QA values are injected at build time through CI variables
- Commit messages: imperative subject line (e.g. "Add unit tests for PluginController"), a blank line,
  then a body explaining what and why

## Licensing

- The plugin links libobs and is GPL-2.0-or-later (`LICENSE`). New dependencies must be compatible with
  GPL-2.0-or-later
- Do not add closed-source components or static libraries; secrets belong on the backend, not in the
  plugin
