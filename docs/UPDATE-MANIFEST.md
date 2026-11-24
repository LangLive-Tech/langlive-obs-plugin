# Update manifest (`latest.json`)

English | [繁體中文](UPDATE-MANIFEST.zh-TW.md)

The plugin finds new versions by downloading one JSON file, the update manifest. This page describes its format and
how the plugin uses it. Publishing it (upload, caching) is tracked in [TODO.md](TODO.md#release-publishing-s3--cloudfront).

## Example

```json
{
  "version": "0.2.0",
  "released_at": "2026-11-20T03:15:00Z",
  "min_supported_version": "0.1.0",
  "platforms": {
    "macos": {
      "url": "https://downloads.example.com/obs-plugin/prod/0.2.0/langlive-obs-plugin-macos-universal-v0.2.0.pkg",
      "sha256": "dfb1a0405d9aed6a28b95b61f0308ad23a96a867666d98ab34642824eb55de8e"
    },
    "windows-x64": {
      "url": "https://downloads.example.com/obs-plugin/prod/0.2.0/langlive-obs-plugin-windows-x64-v0.2.0.exe",
      "sha256": "8c1f0e5b0f4a2d7c3e9b6a1d2f4c8e0a7b3d5f9e1c2a4b6d8f0e2c4a6b8d0f1e"
    }
  }
}
```

## Fields

| Field | Required | Description |
|---|---|---|
| `version` | yes | Latest version, SemVer: `1.2.3`, `1.2.3-rc.1`. A leading `v` and build metadata (`+qa`) are accepted; build metadata is ignored when comparing |
| `released_at` | no | When this version was published, UTC, ISO 8601 (`2026-11-20T03:15:00Z`): when `deploy:qa` / `deploy:prod` first published it, not when it was built. Publishing the same version again keeps it. For display only, never for deciding an update; a plugin that reads it must treat a missing or invalid value as absent, not as an unusable manifest. Plugins up to 0.1.7 ignore it |
| `min_supported_version` | yes | Oldest version still allowed to log in. Older versions must update first. Use the oldest released version (e.g. `0.1.0`) when no update is forced |
| `platforms` | yes | One entry per installer. The plugin reads only its own key |
| `platforms.macos` | yes | The universal `.pkg` (Apple Silicon and Intel) |
| `platforms.windows-x64` | yes | The Inno Setup `.exe` |
| `platforms.*.url` | yes | Installer download URL. HTTPS only; the file name must end in `.pkg` (macOS) or `.exe` (Windows) |
| `platforms.*.sha256` | yes | SHA-256 of the installer: 64 hex digits (case does not matter). A download that does not match is deleted and never opened; the plugin says the installer failed verification |

Unknown fields are ignored, so fields can be added later without breaking released plugins. Keep the manifest under
1 MB.

## How the plugin decides

The running version is compared with the manifest:

| Condition | Result | What the streamer sees |
|---|---|---|
| running < `min_supported_version` | Required | Shown from any menu item. The update dialog has no "later", but can be closed. Logging in and going live are refused until the update is installed; the other items (diagnostics, help center, logout) go on once it is closed |
| running < `version` | Available | Shown only from Check for Updates: the update dialog with "later". The other menu items go on without it |
| otherwise | Up to date | "Already up to date" after Check for Updates; nothing otherwise |

Versions compare by SemVer precedence, so `0.2.0-rc.1` is older than `0.2.0`. The release pipeline publishes plain
`X.Y.Z` versions only.

The manifest is treated as unusable, and the check fails quietly (a message only after a manual Check for Updates),
when any of these hold:

- it cannot be downloaded within 30 seconds of no data, or is not a JSON object
- `version` or `min_supported_version` is missing or not a version
- there is no entry for the running platform, or its `url` / `sha256` is missing or invalid

## When the plugin checks

- When the streamer chooses any item of the 浪LIVE menu, before the item runs (not when OBS starts). During a
  plugin live, Go Live opens without a check, since that is where the live is ended
- 浪LIVE > 檢查更新 (Check for Updates) is the only item that shows an optional update

The mouse pointer shows that the plugin is busy while it checks.

Requests send `User-Agent: langlive-obs-plugin/<version>` and `Cache-Control: no-cache`. Redirects are followed, but
never from HTTPS to HTTP.

## Where the plugin looks

The manifest URL is compiled in: the CMake cache variable `LANGLIVE_UPDATE_MANIFEST_URL`. It is empty by default, so
builds without it never check for updates. The URL must use HTTPS; plain HTTP is accepted only for `127.0.0.1`,
`localhost`, and `[::1]`, for local testing with `scripts/dev/serve-update-manifest.py`.

QA builds (`LANGLIVE_QA_BUILD`) also read `qa-override.json` from the plugin's config folder on every check:
`{"update_manifest_url": "https://..."}` replaces the compiled-in URL; the update UI looks the same as in production builds. Production builds
ignore the file. `scripts/dev/qa-override.sh` writes it (macOS, or Git Bash on Windows): pass a scenario that
`deploy:qa` publishes (`optional`, `forced`, `uptodate`, `bad-sha`, `broken`) or any HTTPS URL, `status` to show the
current one, or `clear` to remove it. Scenario names need the scenarios folder URL in `LANGLIVE_QA_SCENARIOS_URL` or
`~/.config/langlive-obs-plugin/qa-scenarios-url`; it is not in this repository.

## Version policy

- Major version (1.4.2 → 2.0.0): forced update; `min_supported_version` becomes the new version
- Minor (1.4.2 → 1.5.0) and patch (1.4.2 → 1.4.3): optional; `min_supported_version` stays
- A problem QA finds is fixed in a new patch version, so QA's plugins are offered it too

CI applies this when it writes the manifest; `UPDATE_MIN_SUPPORTED_VERSION` overrides it (for example a patch that
must be forced), and a version older than the published one is refused.

## Publishing rules

- Installer file names include the version and never change once uploaded; cache them for a long time. Only
  `latest.json` is replaced in place; give it a short `Cache-Control`
- Upload the installers first, then `latest.json`, then invalidate the CDN cache for `latest.json`, so no manifest
  ever points at a file that is not there yet
- After publishing, download `latest.json` through the CDN and check its version and both SHA-256 values against the
  installers
- To force an update (for example a fix the backend depends on), raise `min_supported_version`. Every older plugin
  then blocks login until it is updated, so raise it only together with a release that has been tested
- CI does this for version tags: `deploy:qa` publishes the QA build to `test/<version>/` and `test/latest.json`; after
  QA, the manual `deploy:prod` publishes the production build to `prod/<version>/` and `prod/latest.json`
  (`max-age=60`). Production clients
  download through the CDN, whose root is the bucket's `prod/`; test and dev have no CDN and use the bucket's own URL
- QA and production use separate manifests (separate URLs, built in through CI variables), so a QA build never reaches
  production plugins
