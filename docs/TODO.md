# TODO

English | [繁體中文](TODO.zh-TW.md)

Work tracking for the plugin. Checked items are done and kept as a record; unchecked items are still open. Check an
item off (`- [x]`) when it lands instead of removing it. Code-level TODOs are marked with `TODO:` in the source.

## Installers

- [x] Windows: Inno Setup installer (`scripts/windows/package-windows-installer.ps1`) installs into
  `%ProgramData%\obs-studio\plugins` without admin rights and leaves out the `.pdb`
- [x] Windows: the installer refuses to install while OBS (`obs64.exe`, any user) is running; tested with a test build
  of the same script with OBS running and not running, and with the CI-built installer with OBS running
- [x] macOS: `scripts/macos/package-macos-installer.sh` builds `release/langlive-obs-plugin-macos-universal-vX.Y.Z.pkg`:
  installs into `~/Library/Application Support/obs-studio/plugins` without admin rights, bundle relocation off, no
  `.dSYM`, refuses to install while OBS is running
- [x] macOS: tested install, OBS loading the installed plugin, install refused while OBS runs, and
  `scripts/uninstall-macos.sh` (unsigned build, from the command line)
- [x] macOS: installer GUI tested by double-clicking a locally built `.pkg`
- [x] macOS: test a downloaded `.pkg` (quarantined, so Gatekeeper checks it; needs signing and notarization)
- [x] Windows: install / uninstall with the Inno Setup installer tested on a test PC without Smart App Control (OBS
  33): install, refused while OBS runs, uninstall
- [x] Windows uninstall also removes the plugin's settings (`%APPDATA%\obs-studio\plugin_config\langlive-obs-plugin`,
  with QA builds' `qa\`), as `scripts/uninstall-macos.sh` does
- [ ] Windows: try that on a desktop (the CI build only compiles the installer)
- [x] Both installers carry `LICENSE` and `THIRD-PARTY-NOTICES.txt` (installed in the plugin's `licenses` folder).
  The installers show no license page, to keep their steps short. Qt and libobs are not bundled: they
  come from the OBS Studio installation, so only qrcodegen (MIT) needs a notice
- [x] Installer as updater: both installers refuse while OBS runs and block downgrades. The plugin quits OBS and
  opens the installer once OBS has exited
- [x] ~~Optional: silent install (`/SILENT`) and restarting OBS after the update~~: not needed (decided)
- [x] macOS: the `.pkg` refuses to install over a newer version (message in both languages); the same version and
  upgrades are allowed
- [x] Windows: the installer refuses to install over a newer version (message in both languages, exit code 1 when
  silent); the same version and upgrades are allowed

## Code signing

Unsigned builds are blocked by Gatekeeper (macOS) and by Smart App Control (Windows 11), warned by SmartScreen
(Windows). The updater verifies downloads with the manifest's SHA-256, not with signatures. QA and Prod builds are
signed with the same certificates. Credentials live only in protected GitLab CI variables or the signing service,
never in the repository.

### macOS

- [x] Company Apple Developer Program account; Developer ID Application certificate (plugin bundle) and Developer ID
  Installer certificate (`.pkg`), in the obs-mac runner's login keychain (account details on Confluence)
- [x] Version tags sign the plugin bundle in the package step (Developer ID Application, hardened runtime, secure
  timestamp; `cmake/macos/resources/create-package.cmake.in`) and the `.pkg` (`productbuild --sign`, Developer ID
  Installer). The protected `MACOS_KEYCHAIN_PASSWORD` unlocks the keychain (`scripts/ci/macos-signing.sh`). QA and
  production installers alike; branch and local builds stay unsigned; `MACOS_SIGN=1` signs a main pipeline to try it
- [x] Notarize the `.pkg` (`xcrun notarytool submit --wait`, an Apple ID app-specific password saved as a notarytool
  profile in the runner's keychain) and staple it (`scripts/ci/notarize-macos.sh`)
- [x] Verify in CI: `codesign --verify --strict`, `pkgutil --check-signature`, `stapler validate`,
  `spctl --assess --type install`. The manual `check:macos-signing` job runs the same steps on throwaway files
- [ ] Move notarization to a team App Store Connect API key, so it does not depend on one person's Apple ID
- [x] OBS loads the plugin signed with our Developer ID (another team than OBS's; OBS disables library validation):
  the notarized QA installer, installed from a quarantined download, on OBS 33.0.1 (macOS 15)
- [ ] The same on the oldest supported OBS version (31)

### Windows

- [ ] Authenticode code-signing certificate (OV or EV). The private key must stay in an HSM / hardware token, so pick a
  signing service that CI can call (e.g. a cloud HSM / signing service from the certificate vendor)
- [ ] Sign the plugin DLL and the installer `.exe` with `signtool sign /fd sha256 /tr <timestamp URL> /td sha256`
- [ ] Sign the uninstaller too (Inno Setup `SignTool` directive in `installer.iss.in`)
- [ ] Verify in CI: `signtool verify /pa /v`
- [ ] SmartScreen reputation builds with downloads; plan for warnings on the first releases
- [ ] Windows 11 Smart App Control blocks unsigned DLLs that have no reputation: OBS logs "An Application Control
  policy has blocked this file" and the plugin does not load. A signed DLL is required, not optional. Test on a machine
  with Smart App Control on before release
- [x] Confirmed that a self-signed certificate does not help, even when added to the machine's Trusted Root and
  Trusted Publishers: Smart App Control only accepts certificates from Microsoft's root program

## Updates

- [x] Version parsing and comparison in `core/` (`core::ParseVersion`, `core::IsUpdateRequired`, `core::EvaluateUpdate`)
  with unit tests
- [x] Read the update manifest (`latest.json`, format in [UPDATE-MANIFEST.md](UPDATE-MANIFEST.md)) before each menu item
  runs, not when OBS starts. An optional update is shown only by "Check for Updates"; a required one by any item,
  which blocks logging in and going live (the other items go on once it is closed). Go Live during a plugin live
  opens without a check
- [x] Update dialog; a required update has no "Later" but can be closed with X (PM decision). Every menu item
  then checks again and shows it again while the update is still required (also when that check fails)
- [x] Closing the update dialog (X) during the download cancels it, e.g. when it is stuck on a bad connection;
  updating again starts a new download. Each download has its own temp folder, so a canceled one still finishing
  cannot remove the next one's file; installer names with path separators are refused
- [x] Download the installer, verify its SHA-256 and file type; HTTPS only (plain HTTP only for localhost); never
  follow an HTTPS-to-HTTP redirect
- [x] Hand over to the installer: a detached helper waits for OBS to exit, then opens it; updating is blocked while live
- [x] Forced update: login is blocked while the version is below `min_supported_version`
- [x] Local test server `scripts/dev/serve-update-manifest.py`; tested on macOS and Windows: optional update,
  required update, download, verify, install (`LANGLIVE_UPDATE_MANIFEST_URL=http://127.0.0.1:8765/latest.json`)
- [x] Production manifest URL from the `PROD_UPDATE_MANIFEST_URL` CI variable; was: as the `LANGLIVE_UPDATE_MANIFEST_URL` default (empty for now, so release builds do not
  check for updates); QA URL injected by CI
- [x] ~~Verify the installer's code signature before launching it (`QtUpdateFeed::LaunchInstaller`)~~: not needed
  (decided). The manifest's SHA-256 verifies the download, and the OS checks the signature when the installer opens
- [x] Test a corrupted download (`--bad-sha256`) and a failed check (server down) by hand
- [x] ~~Manifest signature: the sha256 only protects the installer if the manifest itself is trusted (HTTPS); decide
  whether to sign the manifest too~~: not needed (decided). The manifest is fetched over HTTPS only, only CI's
  protected keys can write it on S3 / the CDN, and the OS checks the signature of the released installers

## Backend API integration

Built against the backend's API document (Plugin & Web API, `/v6/obs_plugin/...`); integration testing with the backend
has not started. Calls go through the `LangLiveApi` port (`app/langlive-api.hpp`): `HttpLangLiveApi` (`src/obs/`)
when the build sets `LANGLIVE_API_BASE_URL`, otherwise `MockLangLiveApi`, which follows the documented behavior.

- [x] API responses without the JSON envelope: a transport error or an HTTP error status (a gateway's 502 / 503 page)
  is a network failure, so an unsent `streaming/close` is kept for the next session; only a 2xx non-envelope answer is
  an invalid response. `streaming/info` reads `status` / `direction` as numbers or strings, and an answer without
  `stream_info` is an invalid response instead of "not live"
- [x] API calls do not follow redirects (the token header and body would go to the new host). Manifest and installer
  downloads follow them within HTTPS only; where libcurl cannot set that limit (macOS 12's own libcurl), not at all
- [x] API client: `ret_code` mapped to errors (99300 too frequent, 99301 SMS limit, 99302 not eligible, 99303 session
  expired, 99304 live on the website); JSON parsing tested against the documented examples
- [x] `X-LOCAL` header from OBS's language (zh-TW, zh-CN, otherwise en-US), so `ret_msg` matches the UI language
- [x] UI languages: Traditional Chinese, Simplified Chinese (`zh-CN.ini`), and English; other languages show English. Copy
  from the PM's string sheet (2026-11-19)
- [x] Log for troubleshooting: every API call (path, HTTP status, `ret_code`, time taken), `live_id` on open and close,
  the API language, and the pre-stream check's failures with the encoder settings. Never tokens, stream keys or phone
  numbers
- [x] HTTPS with libcurl (`src/obs/curl-http.*`): OBS ships no Qt TLS backend, so Qt Network failed every request
  with "TLS initialization failed"
- [ ] Integration test with the backend (QA API URL as a CI variable). Done on macOS against the QA backend: SMS
  login, streaming/info, setting, start, streaming, close. Still to do: Windows, the go-live form after a save
- [x] A live left open is closed: on OBS's exit event, streaming/close is sent synchronously (3 s at most) before
  the plugin unloads; the open `live_id` is kept locally (`open-live.json`) until closed, and the next session
  (OBS loaded, or a login) closes it if the backend still has that same live open from the plugin. Not tested in
  OBS yet (the QA backend's streaming/start returned 24 at the time)
- [x] The backend ends lives whose stream is gone for a while (confirmed with the backend), so a crashed OBS that is
  never started again leaves no live open
- [x] Go Live saves the form (streaming/setting) before streaming/start, which does not keep it for the next live
- [x] Reported to the backend: streaming/start failed with ret_code 24 "系統出錯" without `push_text` (documented as
  optional); fixed on QA (checked 2026-11-18). The plugin still always sends it. Not fixable on the backend (decided):
  error responses have `data: []` instead of `{}`, or no `data` at all (ret_code 19 for a missing field); the plugin
  accepts all three. Earlier, while the server could not write its error log, errors came back as ret_code 0 with that
  log error as `ret_msg` and no `data`; the plugin logs the `ret_msg` of a ret_code 0 without a stream target and says
  going live failed. Not to report (decided): send_code answers ret_code 0 with "Illegal arguments." for an empty
  number (the plugin checks the number before sending); streaming/setting does not keep the events (`act1`, `act2`),
  by design
- [ ] `X-LOCAL` (zh-TW / zh-CN / en-US) does not change `ret_msg` yet, which is always Chinese: the backend supports
  it and is waiting for the PM's translations. Check again once they are in
- [x] Release builds must set `LANGLIVE_API_BASE_URL`; fail the release job without it (the mock logs in with anything).
  CI builds take it from the project variable of the same name; the release job fails when it is unset
- [x] API URLs per environment: `QA_API_BASE_URL`, `PROD_API_BASE_URL` (QA's for now, until the production backend is ready)
- [x] Nickname at login: `sms/login` and `qrcode/verify` return only `authorization_token` in `data`; the
  nickname comes from `ret_msg` ("[nickname], logged in" in Chinese), and `data.nickname` is read first if it is ever
  added
- [x] `topic_id` and `ret_code` types: the documentation uses both strings and numbers, but the QA backend
  (2026-11-17) always sends `ret_code` as a string and `topic_id` as a number. The plugin accepts both
- [x] Token refresh: the backend has none (2026-11-17), so a streamer logs in again when the token expires after a week
- [x] The backend decides whether a login is still valid. 99303 (invalid token) and 99302 (the account may no
  longer stream from OBS) from any call that sends the token log out, and a notice says which. When OBS has loaded, a
  login restored from the last session is checked with one `streaming/info` (the same one that recovers a live left
  open), so a revoked token logs out then instead of at the next Go Live; without network the login is kept
- [x] The channel is not saved (spec 4.3, confirmed 2026-11-17): the form no longer fills in the channel the backend
  keeps. `streaming/setting` refuses a request without a valid `topic_id` (ret_code 19), so Save without a chosen
  channel sends the backend's own back unchanged

### 1. Login

- [x] Phone number login UI: country code, SMS code with a 90-second resend countdown, inline errors; "Send code" needs a complete mobile number, "Log in" also a 6-digit code
- [x] Like the website, a phone number or code field left empty shows "Required" under it, with a red border; once
  the phone number is complete, the empty code field shows it too
- [ ] Typing digits in the phone number and code fields without switching off a Chinese input method. The fields'
  input method is off (`WA_InputMethodEnabled`) and full-width digits become ASCII ones, but on macOS a first test
  with Zhuyin still went through the input method. Next: switch to an ASCII input source while a field has the focus
  and back after (as browsers do for password fields). Test on Windows too
- [x] QR code login UI: QR code, expiry countdown, expired state with refresh
- [x] Consent line linking the terms of service and privacy policy, and the scam notice
- [x] Styled like the website's login dialog; both tabs share one size so switching does not move the dialog
- [x] Phone number login through the API (`sms/send_code`, `sms/login`), asynchronous with the buttons disabled while
  waiting; the daily limit (10 per number per Taiwan day) and the missing OBS permission are shown per spec 4.2
- [x] ~~Resend interval from the backend~~: not needed (decided); fixed at 90 s per the product spec
- [x] QR code login: `qrcode/create` (GET, although documented as POST: POST gets 405) gives the code, shown for its
  documented 300 seconds; `qrcode/verify` is checked every 2 seconds while the QR tab is shown (1802 waiting, 1801
  scanned: "confirm in the app", 0 logged in like the SMS login, -1 expired: refresh). 99302 and other refusals end
  the code; a network error waits for the next check. The waiting answers are not logged. The mock backend confirms
  at the fifth check
  - [x] Tested in OBS on the QA backend, with the app's own API standing in for the app (`/v2/passport/qrcode`,
    type 0 scan, type 1 confirm): 1802, 1801 once, 1802 until the confirmation, then 0
  - [x] Tested with the LangLive app's scan page (passed 2026-11-17)
- [ ] Shared for both methods
  - [x] OBS streaming eligibility: 99302 blocks login with the spec's message
  - [x] Forced update: block login when the plugin is below `min_supported_version` (see Updates)
  - [x] Session: the token is saved and restored on OBS start; 99303 logs out and asks to log in again
  - [x] Keep the token in secure storage instead of `session.json` (`src/obs/secure-store-*.cpp`): a generic password
    in the login keychain (macOS) or a generic credential in Credential Manager (Windows), per user, no admin rights.
    `session.json` keeps only the nickname; a token left there by 0.1.4 or older moves on the next start. If the token
    cannot be stored it is not written to disk, and the streamer logs in again next time. On macOS the item belongs
    to OBS: if OBS's code signature changes, macOS asks once. Both uninstallers remove it
  - [x] QA builds keep their own login and settings: keychain service `com.langlive.obs-plugin.qa` / Credential
    Manager target `LangLive OBS Plugin (QA)/…`, and a `qa/` folder in the plugin's config folder
    (`qa-override.json` stays where it is). A QA login, open live, or uploaded cover never reaches a production
    build on the same computer. Both uninstallers remove both logins
  - [ ] Windows: try saving, restoring, and removing the login (and the uninstaller removing it) in a desktop session;
    Credential Manager is not available over SSH, so only the build and the installer script were checked
  - [x] Logout revokes the token on the backend (`POST logout`, only that token; after closing the live when one is
    open, since the close needs it). The streamer is logged out locally even if the call fails; the token then
    expires by itself. An expired login (99303) calls nothing
    (confirmed with the backend)
  - [x] Nickname from the login response
- [ ] Third-party account login (Facebook, Google, LINE, Apple): not started, not in the first release

### 2. Go-live settings

- [x] Go-live dialog per the wireframe: cover with cropping (800 × 800), title, push notification, main / sub events,
  channel, orientation hint, scenario, save, start / end live
- [x] Save also applies the recommended stream settings for the scenario and orientation (spec 4.6)
- [x] While live: title "Go Live · Live", a notice, every field and Save disabled; only End Live is available
- [x] Closes once the live starts; stays open under the pre-stream check's dialog when the check fails
- [x] Closing (X or Escape) only hides it, so the events and channel, which are not saved to disk, are kept until logout
- [x] Lives are started and ended only through the plugin's UI (design principle, see README). OBS's own Stop
  Streaming (or a hotkey, or OBS giving up reconnecting) ends a plugin live for good: closed on the backend, the
  stream key cleared from OBS, and an "End Live" notice with only "Got It" ("Streaming has stopped and your live has ended."). End Live from the plugin clears the key too,
  without the notice; so does OBS exiting during a live. With the mock backend (no key given) OBS's own key is kept
- [x] A live the backend still has open from the plugin while this OBS streams none (the close failed because the
  network was down when the stream stopped, or a crash) blocks Start Live until the backend closes it after 10 minutes
  without stream. Opening Go Live now closes it first (`streaming/close` with the `live_id` from `streaming/info`),
  without asking (PM's decision: another computer streaming from the plugin is rare); Start Live tries again if that
  failed and does not go live until it succeeds. Website lives and this OBS's own live are left alone
- [x] OBS failing to start the plugin's stream (e.g. the encoder cannot start) sends no frontend event, only its main
  window's `StreamingStopped` Qt signal and its own error dialog (`StartFailureWatcher`). The live is then closed and
  the key cleared like a stopped stream, without a second message; before, the plugin stayed "starting" until OBS was
  restarted. Not yet triggered in a real OBS (tested with fakes; the signal is found in OBS 31)
- [x] Orientation, channels, events, and the saved title / push notification from `streaming/info`
  (`LoadGoLiveInfo`, on opening the dialog); a reload keeps what the streamer changed
- [x] While `streaming/info` loads: a spinner with "Loading your live room…", and the whole form, Save, and Start Live
  are disabled. API calls give up after 20 seconds in all
- [x] Save also sends title and push notification to `streaming/setting`; the scenario stays local, the cover see below
- [x] Cover upload: `upload/image` gives a pre-signed S3 URL, then a PUT with the headers it returns
  (`RequestCoverUpload` / `UploadCover`); the returned `playUrl` is the cover in `streaming/start`
  (`streaming/setting` has no cover and ignores `liveimg`). PM decision (2026-11-17): Upload Cover opens the file
  picker, the crop dialog's Upload Cover uploads right away and shows the backend's message on failure (e.g. the
  daily limit of 20), and the previous cover stays; the thumbnail does nothing when clicked; Save uploads nothing.
- [x] The form shows the room's cover from `streaming/info`, downloaded every time it loads (the website can change
  it too). A cover uploaded here reaches the room only with `streaming/start`, so it is kept (`uploadedCoverUrl`) with
  the room's cover at that time (`coverBaselineUrl`): while the room's cover is unchanged, ours is newer and is shown
  and goes live; once it differs (changed on the website, or ours went live), the room's wins. A login drops it
- [x] Show the cover's review status: not needed (decided 2026-11-17)
- [x] Start live (`StartLive`): cover, `streaming/start`, the returned ingest and stream key written into OBS
  (`SetStreamTarget`), then start; asynchronous, with errors (99304 live on the website, 99302, network) shown
- [x] Start Live asks `streaming/info` first: a live from the website shows "You are live on OBS through the LangLive
  website..." without the pre-stream check; when nothing is live it also refreshes the account's orientation. (While a website
  live ran, `direction` used to be that live's instead of the account's; the backend has fixed it.)
- [x] Check the stream settings before `streaming/start`, so the backend does not open a live the pre-stream check
  then stops (`ConfiguredStreamState`, read from the profile)
- [x] Reconnecting: when the stream drops, OBS retries on its own; the go-live dialog shows "Go Live · Reconnecting" with
  a warning instead of looking live. If OBS gives up it shows its own error and the live ends (no second dialog)
- [ ] (On hold, 2026-11-17) Reconnecting: agree with the backend how long a live room is kept while OBS retries (OBS default: every 2 s,
  up to 25 times)
- [x] End live: `streaming/close` in `OnStreamingStopped`, for End Live, OBS's Stop Streaming, giving up reconnecting,
  a failed connection, and the pre-stream check. Sends the `live_id` that `streaming/start` returned
- [x] ~~Retry `streaming/close` when it fails~~: not needed (confirmed with the backend). The call is not expected to
  fail, and the backend ends a live on its own once it has seen no stream for a while, even if close was never sent

### 3. Other

- [ ] Live source: report and read how the account is streaming, so the plugin and the website block each other while live (spec 4.8)
- [ ] Update manifest and `min_supported_version` (see Updates)
- [x] ~~Optional: stream spec limits from the backend instead of `core/` defaults~~: not in this version (decided);
  the limits stay fixed in `core/`

## Pre-stream check

- [x] The 10 checks and messages of spec 4.7.1 in `core/`, with unit tests
- [x] A quality-based rate control (CRF, CQP, ICQ, lossless) in advanced output mode fails the bitrate check, since
  its bitrate setting is no limit; applying the settings always sets CBR (VideoToolbox falls back to ABR without CBR)
- [x] Only lives started from the go-live dialog are checked (agreed with the PM): the plugin cannot tell whether OBS's
  own Start Streaming goes to LangLive, so such streams are never checked or changed
- [x] "OBS 推流設定異常" dialog per the wireframe, with scenario cards when the resolution is wrong
- [x] Applying the recommended settings changes only the output resolution, never the canvas (base resolution): it
  holds the streamer's scene layout. The canvas aspect ratio is not checked (agreed with the PM): a canvas that does
  not match the account's orientation still goes live without a message, since how to stream is the streamer's choice
- [x] Its "套用設定並開始直播" (spec 4.7.2) applies the settings, then starts the live the check stopped again with the
  same form (`streaming/start` again)
- [x] Applying settings is refused while streaming, recording, or the virtual camera is active
- [x] Check before starting when the stream starts from the plugin, instead of starting and stopping right away. The
  check runs again when the stream starts, with OBS's actual encoders
- [x] Keyframe interval: OBS's simple output mode has no setting for it (encoders default to auto), so in simple mode the
  plugin sets it to 2 s on `STREAMING_STARTING`, for that stream only and without a dialog. In advanced mode the
  streamer's own value is checked as before: a value other than 2 s stops the stream and shows the dialog, whose Apply
  writes 2 s into `streamEncoder.json`. The streamer's output mode is never changed. Tested on macOS with x264 (simple)
- [x] Video / audio codec: only H.264 and AAC are supported. Apply switches any other codec to x264 / AAC in the profile
  (simple: `StreamEncoder`, `StreamAudioEncoder`; advanced: `AdvOut` `Encoder`, `AudioEncoder`, and a new
  `streamEncoder.json` with CBR). Hardware H.264 (NVENC, QSV, AMF, Apple VT) is kept
- [x] OBS creates its encoders only at startup or when its settings are saved, so on `STREAMING_STARTING` the plugin
  gives the stream output x264 / AAC encoders that match the switched profile (`UseProfileCodecs`), for that stream;
  after OBS restarts it uses its own
- [x] Tested the codec switch on macOS: simple and advanced mode with Apple VT HEVC, after an OBS restart, and with
  recording
- [ ] Test the codec switch on Windows (NVENC HEVC / AV1)

## UI

- [x] Every dialog uses the LangLive website's dark theme (`ui/style.hpp`), independent of the OBS theme
- [x] Notices use the plugin's own notice dialog (`ShowMessage`) instead of the system message box
- [x] Diagnostics export dialog
- [x] Diagnostics redact any field whose name ends in key, token, secret, password, bearer, or authorization (any case),
  so OBS's `RefreshToken` (Twitch / YouTube / Restream in `basic.ini`) and WHIP's `bearer_token` are removed too
- [ ] Diagnostics export runs on the UI thread, and redacting a multi-MB log with `std::regex` takes seconds
- [x] Logging out (or an expired login) with a dialog open over the go-live dialog hides the go-live dialog and deletes
  it only once no dialog is open; deleting it under its running child crashed OBS. The session-expired notice is
  queued instead of shown inside the controller
- [ ] Final copy and translations for the plugin (spec section 6 is pending)
- [x] Help center: the website's FAQ, OBS plugin section (`/obsFaq?anchor=obsplugin`), on the build's website and
  in OBS's language like the terms and privacy links (`core::WebsitePageUrl`). Page set by the PM
- [x] No logout during a plugin live, including while it starts (PM's decision): the menu's Log Out is disabled until
  the live ends. Streams from OBS's own Start Streaming do not block logout and are left alone by it; an expired login
  (99303) keeps the live streaming, and it is closed after the next login

## Verification

- [x] Build and unit tests on macOS (universal)
- [x] Build on Windows (Visual Studio 2026)
- [x] UI tests (`tests/ui/`), passing on macOS and Windows: dialogs run headless with fakes; login, go-live (missing fields, closing once live,
  locked while live, reconnecting, Escape, Save, cover upload), pre-stream check, updates, notices, menu
- [x] Run the UI tests in CI (`ui-tests:macos`, `ui-tests:windows`, after the build job that sets up `.deps/`);
  releases and the dev snapshot wait for them
- [x] ~~End-to-end tests with real OBS: obs-websocket + a local RTMP server + ffprobe (GOP, resolution, reconnecting)~~:
  not needed (decided)
- [ ] Run the plugin in OBS Studio on macOS and Windows and go through every menu item (macOS partly done; on Windows
  the plugin loads on the test PC without Smart App Control, menu items not gone through yet)
- [x] CI builds with warnings as errors (`-DCMAKE_COMPILE_WARNING_AS_ERROR=ON` on the `macos` / `windows-x64` presets;
  the `macos-ci` preset also needs ccache)

## CI

- [x] Run `.gitlab-ci.yml` on GitLab runners: macOS and Windows shell-executor runners, all jobs passing (runner setup in
  `AGENTS.md`)
- [x] macOS and Windows runners: build the plugin and both installers, kept as job artifacts
- [x] Register runners on the company GitLab (its shared runners are tagged for other projects, so the jobs stay pending): tags `obs-mac` and `obs-windows`
- [x] Publish the GitLab Release for `vX.Y.Z` tags: installers in the generic package registry, SHA-256 in the
  release description (`scripts/ci/gitlab-release.sh`)
- [ ] Sign the installers in CI (macOS done, see Code signing; Windows open)
- [x] Release pipeline: version tags deploy to test, then to production after QA, with the update manifests. No rc
  tags: QA tests the version itself, and a version not yet released can be tagged again
- [ ] GitHub sync of released versions
  - [x] `sync:github` after `deploy:prod`: one commit per release on GitHub's `main` (the tag's tree, not our history),
    the tag, and the GitHub Release with the production installers (`scripts/ci/sync-github.sh`); refuses a version
    whose GitLab Release is not released
  - [ ] Create the GitHub repository (private until the public release) and set `GITHUB_REPOSITORY` and the protected,
    masked `GITHUB_PUSH_TOKEN` in the GitLab project settings; run the manual `check:github-access` job on `main`
  - [ ] Test run while the repository is private: play `sync:github:tag` with `SYNC_TAG=v0.1.7`, check the commit,
    tag and Release on GitHub, then delete and recreate the repository (and re-select it in the token) so the first
    official release is GitHub's first commit
  - [ ] First sync with the first official release, then check the tag, `main` and the Release on GitHub

## Release publishing (S3 / CDN)

The release pipeline publishes installers and update manifests to S3; production downloads go through a CDN. The S3
access keys and the CDN are provided by DevOps.

- [x] Environments: `dev` snapshot replaced by every default-branch build; a `vX.Y.Z` tag builds both QA and
  production installers (`QA_*` / `PROD_*` variables) into its GitLab Release; `deploy:qa` (automatic) publishes to
  `test/`, `deploy:prod` (manual) to `prod/`. Access keys as masked CI variables (`PROD_*` protected, for protected
  `v*` tags only; `TEST_*` for `test/` and `dev/`)
- [x] Update manifests (`scripts/ci/publish-manifest.sh`): `test/latest.json` (bucket URL) and `prod/latest.json` (CDN,
  `PROD_DOWNLOAD_URL`), written last. Major version: forced update; minor and patch: optional
  (`UPDATE_MIN_SUPPORTED_VERSION` overrides); older than the published version: refused. `released_at`: when the
  version was first published there (kept when the same version is published again); the QA scenarios and the
  local test server have it too
- [ ] Show `released_at` in the update dialog (optional field; waiting for PM to decide whether and how)
- [x] Update test scenarios for QA: QA builds (`LANGLIVE_QA_BUILD`) read `qa-override.json` (`update_manifest_url`)
  on every check; the update UI looks the same as production (logged only). `deploy:qa` publishes
  `test/scenarios/{optional,forced,uptodate,bad-sha,broken}.json` on the version's installers. Production builds have no override
- [x] QA builds name the OBS menu 浪LIVE(Test) (LangLive(Test) in English), so QA and production builds are told apart
- [x] The tag's GitLab Release gets all four installers when the tag is built (`scripts/ci/gitlab-release.sh`); its
  production status is updated by `deploy:prod`
- [x] A tag can be published again until production offers its version or a newer one (`scripts/release/retag.sh`); the
  dev snapshot is one pair of files per version, `...-vX.Y.Z-SNAPSHOT` (X.Y.Z plus the changes since), replaced by
  every build
- [x] `retag.sh` deletes the Release, package and tag only when it has read production's version: an unreachable
  manifest, an error status (the CDN answers 403 for a missing file), or an answer without an X.Y.Z version stops it,
  instead of counting as "nothing released"; so does a version older than production's, or a Release that
  `deploy:prod` marked released
- [x] Installers have versioned file names that never change and a long `Cache-Control`; only `latest.json` changes
  in place, with `max-age=60`
- [x] Upload order: installers first, then `latest.json`, so no manifest points at a file that is not there yet.
  `latest.json` is read back from the bucket and compared
- [x] Flush the CDN's cache after `deploy:prod` (`flush-cdn:prod`) with DevOps' CDN flush API, on the shared `ll`
  runner, the only one that reaches it; no checkout, and no S3 keys once DevOps scopes them (below). It flushes
  the paths `deploy:prod` lists in `cdn-flush-paths.txt` (`/latest.json` and the version's two installers, by name:
  the API refuses `*`). Tested by hand: the API accepts the plugin's domain and several paths in one request
  - [ ] DevOps scopes the S3 keys to their environments (`TEST_*`: `dev`, `test`; `PROD_*`: `production`); until
    then every job, the one on the shared runner too, gets them
- [ ] After `deploy:prod`, download `latest.json` through the CDN and check its version and SHA-256 values

## Open source

- [x] `LICENSE` (GPL v2 text); source files are GPL-2.0-or-later
- [x] README: where the source of each official installer is, third-party notices, trademarks
- [ ] Source link in the website download section and in the installer's license files (GPL: the source must be
  available where the installers are; the GitHub repository and its tags must be public before the public release)
