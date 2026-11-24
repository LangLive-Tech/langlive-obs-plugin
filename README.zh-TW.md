# 浪LIVE OBS 插件

[English](README.md) | 繁體中文

讓主播用 OBS Studio 在 [浪LIVE](https://www.lang.live/) 開播的插件。登入、設定直播間、一鍵套用建議的
編碼設定，以及開始或結束直播，全部都在 OBS 裡完成，不必擔心推流設定出錯。

> **狀態：** QA 測試中，尚未正式發佈。

## 功能

- **登入**：用手機號碼與簡訊驗證碼登入，或用浪LIVE App 掃描 QR Code 登入；第三方帳號不在第一版範圍。
  只有已開通 OBS 開播權限的帳號可以登入；後端隨時可以讓登入失效，插件會請主播重新登入。
- **開播**：設定封面、標題、開播推送文案、活動與頻道，選擇直播場景後，直接在插件裡開始或結束直播。按「上傳封面」選圖、
  裁切後會立即上傳；上傳被拒（例如超過每日上限）時保留原本的封面。
- **建議設定**：儲存開播設定時，依所選的直播場景與帳號的直播方向（橫式／直式），套用對應的解析度、
  每秒幀數、位元率與關鍵影格間隔。
- **開播前檢查**：透過插件開播前、以及推流開始時，插件會再檢查一次推流設定，列出每個不符合浪LIVE 規格的問題，
  並可一鍵修正。用 OBS 自己「開始串流」的推流一律不檢查、不更動。
- **匯出診斷資料**：將 OBS 與插件的執行紀錄、閃退紀錄及設定資訊打包成本機的 zip 檔，提供給浪LIVE
  客服。敏感資訊會被過濾，檔案不會自動上傳。
- **更新**：點「檢查更新」會顯示新版本；強制更新時，點選單任何項目也會顯示。下載並驗證安裝檔後關閉 OBS，讓安裝檔執行。
  低於最低支援版本時，必須先更新才能登入與開播。

### 推流規格

所有直播場景皆使用 H.264 視訊編碼、AAC 音訊編碼，關鍵影格間隔 2 秒。

| 直播場景 | 橫式 | 直式 | FPS | 視訊位元率 |
|---|---|---|---|---|
| 聊天、一般直播（推薦） | 1280 × 720 | 720 × 1280 | 25 | 3000 kbps |
| 才藝表演、高畫質直播 | 1920 × 1080 | 1080 × 1920 | 30 | 6000 kbps |

## 支援平台

| 宿主程式 | macOS（Apple Silicon／Intel） | Windows x64 |
|---|---|---|
| OBS Studio 31+ | ✓ | ✓ |

## 安裝

正式發佈前，安裝檔只提供給測試人員；發佈後會放在浪LIVE 官網。關閉 OBS 後執行對應平台的安裝檔（OBS 開著時安裝檔會拒絕安裝）：

- macOS：`langlive-obs-plugin-macos-universal-vX.Y.Z.pkg`（Apple Silicon 與 Intel 共用一個 universal 安裝檔）
- Windows：`langlive-obs-plugin-windows-x64-vX.Y.Z.exe`

安裝後，插件會自動保持在最新版本。

macOS 安裝檔已用 Developer ID 簽章並經過 Apple 公證，可以直接雙擊安裝。Windows 安裝檔還沒有程式碼簽章：
會出現 SmartScreen 警告；Windows 11 開著「智慧型應用程式控制」時，OBS 完全無法載入插件。

## 架構

插件以 C++17、Qt 6 與 CMake 開發，分為四層：

| 層 | 路徑 | 職責 |
|---|---|---|
| UI | `src/ui/` | 「浪LIVE」選單與各個對話框（Qt）。UI 只把意圖交給應用層，不直接呼叫 OBS API |
| Application | `src/app/` | `PluginController`：登入、開播、建議設定與開播前檢查。只透過 `ports.hpp` 的 port 存取 OBS 與本機儲存 |
| Adapters | `src/obs/` | 用 OBS API 實作 port：讀取與套用推流設定、開始與停止推流、儲存開播設定、匯出診斷資料 |
| Core | `core/` | 推流規格、驗證、版本比較、紀錄去敏 |

`core/` 與 `src/app/` 不依賴 OBS 或 Qt，因此可以單獨做單元測試。浪LIVE 後端 API client
（`src/obs/http-langlive-api.cpp`）也是其中一個 adapter。登入 token 存在系統的安全儲存：macOS 的登入鑰匙圈、Windows 的
認證管理員，兩者都不需要管理者權限。QA 版的登入和設定是分開存的（另一個鑰匙圈項目、`qa/` 設定資料夾），
同一台電腦換裝 QA 版和正式版時，兩個環境不會混在一起。

client 端的檢查是為了使用體驗。以浪LIVE 後端為準：登入、推流 key、畫質上限與最低支援版本，全部由
後端 API 提供。

設計原則：透過插件的直播，開播、關播一律經由插件的 UI（開播／結束直播）。OBS 無法阻止停止串流，所以用其他方式
（OBS「停止串流」、快捷鍵、OBS 放棄重新連線）停止插件直播的串流時，直播就整個結束：後端關播、清空 OBS 的推流
金鑰，並跳出只有「確定」的通知。下一場直播要再從插件開播。用 OBS 自己「開始串流」的推流不屬於插件，插件一律不檢查、不更動、不結束。

## 從原始碼建置

需求：

- CMake 3.28+
- macOS：Xcode 16+
- Windows：Visual Studio 2022 以上

第一次 configure 會下載 OBS 預先建置的相依套件、Qt 6 與 obs-studio 原始碼，可能需要幾分鐘。

```sh
# macOS（universal：arm64 + x86_64）
cmake --preset macos
cmake --build --preset macos

# Windows
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
```

建置安裝檔（輸出在 `release/`）：

```sh
# macOS：release/langlive-obs-plugin-macos-universal-vX.Y.Z.pkg
./scripts/macos/package-macos-installer.sh

# Windows（需要 Inno Setup 6）：release/langlive-obs-plugin-windows-x64-vX.Y.Z.exe
cmake --install build_x64 --config RelWithDebInfo --prefix install
pwsh scripts/windows/package-windows-installer.ps1
```

這樣建置的安裝檔沒有簽章。CI 會為版本 tag 的 macOS 安裝檔簽章並公證；Windows 的程式碼簽章預計之後加入。

Windows 11 的「智慧型應用程式控制」會擋下未簽章的 DLL，開著的時候 OBS 無法載入你自己建置的插件。
請在開發機上關閉（Windows 安全性 > 應用程式與瀏覽器控制 > 智慧型應用程式控制）。

從原始碼建置的版本使用內建的模擬後端（任何手機號碼與驗證碼都能登入），也不會檢查更新：後端與更新的網址預設為空，
QA 版與正式版由 CI 設定。要連到真正的後端，configure 時加上 `-DLANGLIVE_API_BASE_URL=https://...`、
`-DLANGLIVE_UPDATE_MANIFEST_URL=https://.../latest.json` 與 `-DLANGLIVE_WEBSITE_URL=https://...`（空白：正式網站）。
官方版本使用的建置、CI 與安裝檔腳本，全部都在這個 repo 中。

## 執行測試

單元測試涵蓋 `core/` 與 `src/app/` 的應用層。不需要 OBS 或 Qt，只要有 CMake 與 C++17 編譯器就能建置：

```sh
cmake -S tests -B build_tests
cmake --build build_tests
ctest --test-dir build_tests --output-on-failure
```

應用層只透過 `src/app/ports.hpp` 的介面與 OBS 溝通；測試以記憶體中的假實作（`tests/app/fakes.hpp`）取代。

UI 測試不需要 OBS，直接執行插件的 Qt 對話框（offscreen，不需要螢幕），檢查它們的行為：直播中哪些欄位鎖定、
點了會開什麼、會出現哪些提示。測試會用到 `.deps/` 裡的 Qt 與 obs-studio 標頭檔，所以要先 configure 過一次插件：

```sh
# macOS
cmake -S tests/ui -B build_ui_tests -G Ninja
cmake --build build_ui_tests
ctest --test-dir build_ui_tests --output-on-failure

# Windows（Visual Studio generator）
cmake -S tests/ui -B build_ui_tests
cmake --build build_ui_tests --config RelWithDebInfo
ctest --test-dir build_ui_tests -C RelWithDebInfo --output-on-failure
```

## 參與開發

回報問題與提交 pull request 的方式，請見 [docs/CONTRIBUTING.zh-TW.md](docs/CONTRIBUTING.zh-TW.md)。

## 授權

本插件連結 libobs，以 GNU General Public License v2.0 或更新版本授權。詳見 [LICENSE](LICENSE)。

### 官方安裝檔的原始碼

正式版透過浪LIVE 官網與插件內建的更新功能散布（GitHub Releases 預計之後提供）。不論從哪裡取得，版本
`X.Y.Z` 完整的對應原始碼，就是這個 repo 的 tag `vX.Y.Z`。版本正式發佈後，它的 tag 永遠不會刪除或移動；
發佈之前，QA 測試期間同一個 tag 可能重新發佈。

### 第三方軟體

安裝檔內附所含第三方元件的授權條款與聲明。

### 商標

GPL 只涵蓋程式碼。「LangLive」、「浪LIVE」名稱與浪LIVE 的 logo 是浪LIVE 的商標，不在 GPL 授權範圍內。
散布修改過的版本時，請勿以會讓人誤以為是浪LIVE 官方版本的方式使用它們。
