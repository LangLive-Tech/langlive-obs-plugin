# 更新 manifest（`latest.json`）

[English](UPDATE-MANIFEST.md) | 繁體中文

插件下載一個 JSON 檔（更新 manifest）來得知是否有新版本。本頁說明它的格式，以及插件如何使用它。發佈方式（上傳、快取）記錄在 [TODO.zh-TW.md](TODO.zh-TW.md#發佈s3cloudfront)。

## 範例

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

## 欄位

| 欄位 | 必填 | 說明 |
|---|---|---|
| `version` | 是 | 最新版本，SemVer 格式：`1.2.3`、`1.2.3-rc.1`。可加前置的 `v` 與 build metadata（`+qa`），比較版本時忽略 build metadata |
| `released_at` | 否 | 這個版本的發佈時間，UTC、ISO 8601 格式（`2026-11-20T03:15:00Z`）：`deploy:qa`／`deploy:prod` 第一次發佈它的時間，不是建置時間。同一版本重新發佈時沿用原本的時間。只用來顯示，不用來判斷是否更新；讀取它的插件遇到缺少或格式錯誤時當作沒有，不能因此判定 manifest 無效。0.1.7 以前的插件會忽略它 |
| `min_supported_version` | 是 | 仍可登入的最舊版本，更舊的版本必須先更新。不需要強制更新時，填最早發佈的版本（例如 `0.1.0`） |
| `platforms` | 是 | 每個安裝檔一筆，插件只讀自己平台的 key |
| `platforms.macos` | 是 | universal `.pkg`（Apple Silicon 與 Intel） |
| `platforms.windows-x64` | 是 | Inno Setup `.exe` |
| `platforms.*.url` | 是 | 安裝檔下載網址。只接受 HTTPS；檔名必須以 `.pkg`（macOS）或 `.exe`（Windows）結尾 |
| `platforms.*.sha256` | 是 | 安裝檔的 SHA-256：64 位十六進位（大小寫皆可）。下載後比對不符，檔案會被刪除，不會開啟，並提示「下載的安裝檔驗證失敗」 |

不認得的欄位會被忽略，所以之後可以新增欄位，不會影響已發佈的插件。manifest 須小於 1 MB。

## 插件如何判斷

以目前執行的版本與 manifest 比較：

| 條件 | 結果 | 主播看到的畫面 |
|---|---|---|
| 目前版本 < `min_supported_version` | 強制更新 | 點選單任何項目都會顯示。更新視窗沒有「稍後再說」，但可以關閉。完成更新前無法登入與開播；其他項目（診斷資料、說明中心、登出）在關掉視窗後照常執行 |
| 目前版本 < `version` | 一般更新 | 只有點「檢查更新」時顯示，更新視窗可選「稍後再說」。其他選單項目不顯示，直接執行 |
| 其他 | 已是最新 | 點「檢查更新」時顯示已是最新版本；其他時候不顯示 |

版本依 SemVer 的先後順序比較，所以 `0.2.0-rc.1` 比 `0.2.0` 舊。發佈流程只發佈 `X.Y.Z` 版本。

以下任一情況，manifest 視為無法使用，檢查會靜默失敗（只有手動「檢查更新」時才顯示訊息）：

- 30 秒內沒有收到資料而下載失敗，或內容不是 JSON object
- 缺少 `version` 或 `min_supported_version`，或不是合法版本
- 沒有目前平台的項目，或其 `url`／`sha256` 缺少或不合法

## 插件何時檢查

- 主播點選「浪LIVE」選單的任何項目時，在執行該項目之前檢查（OBS 啟動時不檢查）。直播中點「開播」不檢查、直接打開，因為要從那裡結束直播
- 只有「浪LIVE > 檢查更新」會顯示一般更新

檢查時滑鼠游標會顯示忙碌中。

請求會帶 `User-Agent: langlive-obs-plugin/<版本>` 與 `Cache-Control: no-cache`。會跟隨重新導向，但不會從 HTTPS 導向 HTTP。

## 插件從哪裡讀取

manifest 網址在建置時寫入：CMake 快取變數 `LANGLIVE_UPDATE_MANIFEST_URL`。預設為空，沒有設定的建置不會檢查更新。網址必須是 HTTPS；只有 `127.0.0.1`、`localhost`、`[::1]` 可用 HTTP，供 `scripts/dev/serve-update-manifest.py` 在本機測試。

QA 版（`LANGLIVE_QA_BUILD`）每次檢查時還會讀插件設定資料夾裡的 `qa-override.json`：`{"update_manifest_url": "https://..."}` 會取代建置時寫入的網址，更新畫面和正式版一樣，不另外註明。正式版不會讀這個檔。可用 `scripts/dev/qa-override.sh` 寫入（macOS，或 Windows 的 Git Bash）：參數是 `deploy:qa` 發佈的情境（`optional`、`forced`、`uptodate`、`bad-sha`、`broken`）或任何 HTTPS 網址；`status` 顯示目前的設定，`clear` 移除。使用情境名稱時，需要把情境資料夾的網址放在 `LANGLIVE_QA_SCENARIOS_URL` 或 `~/.config/langlive-obs-plugin/qa-scenarios-url`；這個網址不在 repo 裡。

## 版號規則

- major 版（1.4.2 → 2.0.0）：強制更新，`min_supported_version` 設為新版本
- minor（1.4.2 → 1.5.0）與 patch（1.4.2 → 1.4.3）：一般更新，`min_supported_version` 不變
- QA 發現的問題以新的 patch 版修正，QA 的插件也會跳出更新

CI 寫入 manifest 時套用這個規則；`UPDATE_MIN_SUPPORTED_VERSION` 可覆寫（例如必須強制的 patch），比已發佈版本舊的版本會被拒絕。

## 發佈規則

- 安裝檔檔名包含版本號，上傳後不再變動，可設定長時間快取。只有 `latest.json` 會原地更新，設定短的 `Cache-Control`
- 先上傳安裝檔，再上傳 `latest.json`，最後失效 CDN 上 `latest.json` 的快取，確保 manifest 不會指向尚未上傳的檔案
- 發佈後透過 CDN 下載 `latest.json`，檢查版本，並比對兩個安裝檔的 SHA-256
- 需要強制更新時（例如後端依賴的修正），調高 `min_supported_version`。所有更舊的插件都會在更新前無法登入，所以只能搭配已測試過的版本一起調高
- CI 在版本 tag 時執行：`deploy:qa` 把 QA 版發佈到 `test/<版本>/` 與 `test/latest.json`；QA 測完後手動執行
  `deploy:prod`，把正式版發佈到 `prod/<版本>/` 與 `prod/latest.json`（`max-age=60`）。正式環境透過 CDN 下載，CDN 的根目錄就是
  bucket 的 `prod/`；test 與 dev 沒有 CDN，使用 bucket 本身的網址
- QA 與正式環境使用不同的 manifest（不同網址，由 CI 變數在建置時寫入），QA 版不會被正式版插件看到
