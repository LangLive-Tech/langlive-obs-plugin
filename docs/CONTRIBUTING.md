# Contributing

English | [繁體中文](CONTRIBUTING.zh-TW.md)

Thanks for your interest in the LangLive OBS Plugin.

## How this repository works

Development happens in LangLive's internal repository. This GitHub repository is a read-only
mirror that is updated only when an official version is released:

- `main` always equals the latest official release, with one commit per release (the internal development
  history is not published)
- Each release has a tag `vX.Y.Z` and a GitHub Release with the installers
- The installers on a GitHub Release are built from the source at the same tag

## Reporting issues

Open an issue on GitHub. Please include:

- Plugin version, OBS Studio version, and operating system (macOS / Windows)
- Steps to reproduce, and what you expected to happen
- A diagnostics file if possible: in OBS, open the LangLive menu and choose the diagnostics
  export. The file stays on your computer until you attach it; sensitive data is redacted

Do not post account credentials or stream keys in issues.

## Pull requests

Pull requests are not merged on GitHub directly. After review, a maintainer applies the change
to the internal repository with your authorship preserved, and it ships in the next release. The
pull request is then closed with a comment noting which version includes it.

Before opening a pull request:

- Keep the change focused on one topic
- Make sure it builds on macOS and Windows (see [Building from source](../README.md#building-from-source))
- Add or update unit tests for logic in `core/` and `src/app/` (see [Running tests](../README.md#running-tests))
- Do not commit credentials, keys, or backend URLs other than the production defaults
- New third-party dependencies must use a license compatible with GPL-2.0-or-later, and their
  license notices must be added to the installers

## Versioning

Versions follow [SemVer](https://semver.org/):

| Bump | When |
|---|---|
| MAJOR | Incompatible with the LangLive backend; older versions stop working |
| MINOR | New features |
| PATCH | Bug fixes |

The LangLive backend sets a minimum supported version. Plugins older than that must be updated
before logging in; this normally happens with MAJOR releases.

## License

By contributing, you agree that your contributions are licensed under the GNU General Public
License v2.0 or later, the same license as this project. See [LICENSE](../LICENSE).
