# TODO

[English](TODO.md) | 繁體中文

插件的工作清單。打勾的是已完成、保留作為紀錄；未打勾的是還沒做的。完成時把該項打勾（`- [x]`），不要刪除。程式碼中的待辦以 `TODO:` 標記。

## 安裝檔

- [x] Windows：Inno Setup 安裝檔（`scripts/windows/package-windows-installer.ps1`）安裝到 `%ProgramData%\obs-studio\plugins`，不需要系統管理員權限，且不含 `.pdb`
- [x] Windows：OBS（任何使用者的 `obs64.exe`）執行中時拒絕安裝；已用同一份腳本的測試版本測過 OBS 開著和沒開，並用 CI 建出的安裝檔測過 OBS 開著
- [x] macOS：`scripts/macos/package-macos-installer.sh` 產出 `release/langlive-obs-plugin-macos-universal-vX.Y.Z.pkg`：
  安裝到 `~/Library/Application Support/obs-studio/plugins`，不需要系統管理員權限；關閉 bundle relocation、不含 `.dSYM`、OBS 執行中時拒絕安裝
- [x] macOS：已測試安裝、OBS 載入安裝後的插件、OBS 執行中拒絕安裝，以及 `scripts/uninstall-macos.sh`（未簽章版本，以指令列安裝）
- [x] macOS：雙擊本機建置的 `.pkg`，安裝程式畫面測試通過
- [x] macOS：測試從網路下載的 `.pkg`（帶隔離標記，Gatekeeper 會檢查；需要簽章與 notarize）
- [x] Windows：在沒有開啟智慧型應用程式控制的測試電腦（OBS 33）測過 Inno Setup 安裝檔：安裝、OBS 開著時拒絕安裝、解除安裝
- [x] Windows 解除安裝時也刪除插件的設定（`%APPDATA%\obs-studio\plugin_config\langlive-obs-plugin`，含 QA 版的 `qa\`），和 `scripts/uninstall-macos.sh` 一致
- [ ] Windows：在桌面實際測試上一項（CI 只確認安裝檔編譯得過）
- [x] 兩個安裝檔都附上 `LICENSE` 與 `THIRD-PARTY-NOTICES.txt`（安裝在插件的 `licenses` 資料夾），安裝檔不顯示授權說明頁，以簡化安裝步驟。Qt 與 libobs 不在安裝檔內，而是使用 OBS Studio 安裝的版本，所以只有 qrcodegen（MIT）需要聲明
- [x] 安裝檔兼更新程式：兩個安裝檔都會在 OBS 執行中時拒絕安裝，並防止降版。插件會關閉 OBS，等 OBS 結束後開啟安裝檔
- [x] ~~選用：靜默安裝（`/SILENT`）與更新後重開 OBS~~：不需要（已決定）
- [x] macOS：已安裝較新版本時，`.pkg` 拒絕安裝（中英文提示）；相同版本與升級不受影響
- [x] Windows：已安裝較新版本時，安裝檔拒絕安裝（中英文提示，靜默安裝時結束代碼為 1）；相同版本與升級不受影響

## 程式碼簽章

未簽章的版本會被 Gatekeeper（macOS）與智慧型應用程式控制（Windows 11）阻擋、被 SmartScreen（Windows）警告。更新程式用 manifest 的 SHA-256 驗證下載的檔案，不靠簽章。QA 版與 Prod
版使用同一組憑證簽章。憑證只存在 protected GitLab CI 變數或簽章服務中，絕不放進 repo。

### macOS

- [x] 公司的 Apple Developer Program 帳號；Developer ID Application 憑證（插件 bundle）與 Developer ID Installer 憑證（`.pkg`），
  放在 obs-mac runner 的登入鑰匙圈（帳號資訊在 Confluence）
- [x] 版本 tag 在打包步驟簽署插件 bundle（Developer ID Application、hardened runtime、安全時間戳記；`cmake/macos/resources/create-package.cmake.in`），
  並簽署 `.pkg`（`productbuild --sign`，Developer ID Installer）。protected 變數 `MACOS_KEYCHAIN_PASSWORD` 用來解鎖鑰匙圈（`scripts/ci/macos-signing.sh`）。
  QA 版與正式版都簽；分支與本機建置不簽；`MACOS_SIGN=1` 可以在 main 的 pipeline 試跑簽章
- [x] notarize `.pkg`（`xcrun notarytool submit --wait`，使用存在 runner 鑰匙圈的 notarytool profile，內容是 Apple ID 的 App 專用密碼），
  並 staple（`scripts/ci/notarize-macos.sh`）
- [x] 在 CI 驗證：`codesign --verify --strict`、`pkgutil --check-signature`、`stapler validate`、`spctl --assess --type install`。
  手動 job `check:macos-signing` 用暫存檔跑同樣的步驟
- [ ] 公證改用團隊的 App Store Connect API key，不依賴某個人的 Apple ID
- [x] OBS 能載入用我們的 Developer ID 簽署的插件（和 OBS 不同 team；OBS 停用了 library validation）：從帶隔離標記的下載檔安裝公證過的 QA 安裝檔，在 OBS 33.0.1（macOS 15）上確認
- [ ] 在支援的最舊 OBS 版本（31）上再確認一次

### Windows

- [ ] Authenticode 程式碼簽章憑證（OV 或 EV）。私鑰必須存放在 HSM／硬體金鑰中，所以要選 CI 能呼叫的簽章服務（例如憑證商提供的雲端 HSM／簽章服務）
- [ ] 以 `signtool sign /fd sha256 /tr <時間戳記網址> /td sha256` 簽署插件 DLL 與安裝檔 `.exe`
- [ ] 解除安裝程式也要簽章（`installer.iss.in` 的 Inno Setup `SignTool` 指令）
- [ ] 在 CI 驗證：`signtool verify /pa /v`
- [ ] SmartScreen 信譽會隨下載量累積；前幾個版本要預期會出現警告
- [ ] Windows 11 的「智慧型應用程式控制」（Smart App Control）會擋下沒有信譽的未簽章 DLL：OBS log 出現
  「An Application Control policy has blocked this file」，插件不會載入。所以 DLL 一定要簽章，不是選配。發佈前要在開啟此功能的電腦上測試
- [x] 已確認自簽憑證沒有用，即使加進電腦的「受信任的根憑證」與「受信任的發行者」也一樣：智慧型應用程式控制只接受微軟根憑證計畫裡的憑證

## 更新

- [x] `core/` 的版本解析與比較（`core::ParseVersion`、`core::IsUpdateRequired`、`core::EvaluateUpdate`），含單元測試
- [x] 點選單項目時先讀取更新 manifest（`latest.json`，格式見 [UPDATE-MANIFEST.zh-TW.md](UPDATE-MANIFEST.zh-TW.md)），OBS 啟動時不檢查。
  一般更新只有「檢查更新」會顯示；強制更新點任何項目都會顯示，並擋下登入與開播（其他項目關掉視窗後照常執行）。直播中點「開播」不檢查
- [x] 更新對話框；必須更新時沒有「稍後再說」，但可以按 X 關閉（PM 決定）。之後點選單任何項目都會重新檢查，
  仍需強制更新（或檢查失敗）就再顯示更新視窗
- [x] 下載更新時按 X 關閉更新視窗就會取消下載（例如網路很差卡住時）；再按更新會重新下載。每次下載用各自的暫存資料夾，
  取消中的下載收尾時不會刪掉下一次的檔案；檔名含路徑分隔字元的安裝檔一律拒絕
- [x] 下載安裝檔並驗證 SHA-256 與檔案類型；只允許 HTTPS（HTTP 只限 localhost），不跟隨從 HTTPS 轉到 HTTP 的重新導向
- [x] 交給安裝檔：背景程式等 OBS 結束後才開啟安裝檔；直播中不能更新
- [x] 強制更新：版本低於 `min_supported_version` 時無法登入
- [x] 本機測試 server `scripts/dev/serve-update-manifest.py`；已在 macOS 與 Windows 測過：一般更新、必須更新、下載、驗證、安裝（`LANGLIVE_UPDATE_MANIFEST_URL=http://127.0.0.1:8765/latest.json`）
- [x] 正式環境的 manifest 網址取自 CI 變數 `PROD_UPDATE_MANIFEST_URL`；原規劃：設為 `LANGLIVE_UPDATE_MANIFEST_URL` 的預設值（目前為空，所以正式版不會檢查更新）；QA 網址由 CI 注入
- [x] ~~正式版有簽章後，開啟安裝檔前先驗證其簽章（`QtUpdateFeed::LaunchInstaller`）~~：不需要（已決定）。下載的檔案由 manifest 的 SHA-256 驗證，開啟安裝檔時作業系統也會檢查簽章
- [x] 手動測試下載檔被竄改（`--bad-sha256`）與檢查失敗（server 沒開）
- [x] ~~manifest 簽章：只有 manifest 本身可信（HTTPS）時，sha256 才能保護安裝檔；決定是否也要簽署 manifest~~：不需要（已決定）。
  manifest 只透過 HTTPS 下載，S3／CDN 只有 CI 的 protected 金鑰能寫入，正式版安裝檔開啟時作業系統也會檢查簽章

## 串接後端 API

依後端的 API 文件（Plugin 與 Web API，`/v6/obs_plugin/...`）實作，尚未與後端做整合測試。呼叫都透過 `LangLiveApi` port
（`app/langlive-api.hpp`）：建置時有設定 `LANGLIVE_API_BASE_URL` 就用 `HttpLangLiveApi`（`src/obs/`），否則用依文件行為模擬的 `MockLangLiveApi`。

- [x] 沒有 JSON envelope 的回應：連線錯誤或 HTTP 錯誤狀態（閘道的 502／503 頁面）視為網路錯誤，所以沒送出的 `streaming/close`
  會留到下次啟動補送；只有 2xx 卻不是 envelope 才是回應格式錯誤。`streaming/info` 的 `status`／`direction` 接受數字或字串，
  沒有 `stream_info` 時當作回應格式錯誤，而不是「沒在直播」
- [x] API 呼叫不跟隨轉址（token 標頭和請求內容會送到新的網域）。manifest 和安裝檔下載只在 HTTPS 內跟隨轉址；libcurl 無法設定
  這個限制時（macOS 12 內建的 libcurl）完全不跟隨
- [x] API 用戶端：`ret_code` 對應錯誤（99300 過於頻繁、99301 簡訊上限、99302 無開播資格、99303 登入失效、99304 Web 直播中）；JSON 解析以文件範例測試
- [x] 依 OBS 語系送 `X-LOCAL` header（zh-TW、zh-CN，其他用 en-US），讓 `ret_msg` 與介面語言一致
- [x] 介面語系：繁體中文、簡體中文（`zh-CN.ini`）、英文；其他語系顯示英文。文案依 PM 提供的文字表（2026-11-19）
- [x] 排查用 log：每次 API 呼叫（路徑、HTTP 狀態、`ret_code`、耗時）、開播與關播的 `live_id`、API 語系、開播前檢查不通過的項目與當時的編碼設定。不記 token、推流碼與手機號碼
- [x] 以 libcurl 連 HTTPS（`src/obs/curl-http.*`）：OBS 沒有附 Qt 的 TLS 後端，Qt Network 的請求都會以
  「TLS initialization failed」失敗
- [ ] 與後端整合測試（QA 的 API 網址以 CI 變數提供）。已在 macOS 對 QA 後端測過：簡訊登入、streaming/info、
  setting、start、推流、close。尚未測：Windows、儲存後重開開播表單
- [x] 沒關掉的直播會被關播：收到 OBS 結束事件時，在插件卸載前同步送出 streaming/close（最多 3 秒）；開播的
  `live_id` 存在本機（`open-live.json`）直到關播成功，下次啟動 OBS 或登入時，若後端仍是這場插件開的直播就自動關播。
  尚未在 OBS 實測（當時 QA 後端的 streaming/start 回 24）
- [x] 後端偵測到一段時間沒有推流會自動關播（已與後端確認），所以 OBS 當機後不再開啟也不會留下直播
- [x] 開始直播前先儲存表單（streaming/setting）：streaming/start 不會保存，下次開播不會帶出
- [x] 已回報後端：streaming/start 沒有 `push_text` 時回 ret_code 24「系統出錯」（文件寫選填）；QA 已修正（2026-11-18 確認），插件仍一律送。
  後端無法處理（已決定）：錯誤回應的 `data` 是 `[]` 而非 `{}`，或完全沒有 `data`（缺少欄位時的 ret_code 19），插件三種都接受。
  先前有一段時間伺服器寫不進自己的 error log，錯誤變成回 ret_code 0、`ret_msg` 是寫 log 失敗的訊息、沒有 `data`；插件遇到
  ret_code 0 卻沒有推流位址時，會把 `ret_msg` 寫進 log，並顯示「無法開始直播」。不用回報（已決定）：send_code 號碼為空時回
  ret_code 0、「Illegal arguments.」（插件送出前會先檢查號碼）；streaming/setting 不保存主／副衝刺活動（`act1`、`act2`）是設計如此
- [ ] `X-LOCAL`（zh-TW／zh-CN／en-US）目前不影響 `ret_msg`，一律回中文：後端已支援，在等 PM 提供翻譯。翻譯上線後再測一次
- [x] 正式版必須設定 `LANGLIVE_API_BASE_URL`，沒設定時讓發佈 job 失敗（mock 登入一律成功）。CI 建置取自同名的專案變數；沒設定時 release job 失敗
- [x] 各環境的 API 網址：`QA_API_BASE_URL`、`PROD_API_BASE_URL`（正式後端完成前先用 QA 的）
- [x] 登入時的暱稱：`sms/login` 與 `qrcode/verify` 的 `data` 只有 `authorization_token`，暱稱從 `ret_msg` 的「〇〇，登入成功」
  取出；若之後後端加上 `data.nickname`，會優先使用
- [x] `topic_id`、`ret_code` 的型別：文件字串、數字兩種寫法都有，但 QA 後端實際回應（2026-11-17）`ret_code` 一律是字串、
  `topic_id` 一律是數字。插件兩種都接受
- [x] 更新 token：後端目前沒有這個 API（2026-11-17 確認），token 一週後過期時，主播要重新登入
- [x] 登入是否有效由後端決定。任何帶 token 的 API 回 99303（token 失效）或 99302（帳號已無 OBS 開播權限）都會登出，並提示原因。
  OBS 載入完成時，用一次 `streaming/info` 檢查上次保存的登入（和補關播共用同一次呼叫），所以 token 被註銷時會在啟動時就登出，
  不用等到下次開播；沒有網路時保留登入
- [x] 直播頻道不儲存（規格 4.3，2026-11-17 確認）：開播視窗不再帶入後端記住的直播頻道。`streaming/setting` 沒有有效的 `topic_id` 會回 ret_code 19，所以儲存設定時若沒選頻道，會把後端原本的值原樣送回

### 1. 登入

- [x] 手機號碼登入畫面：國碼、簡訊驗證碼與 90 秒重送倒數、欄位下方的錯誤提示；手機號碼完整才能「取得驗證碼」，再填滿 6 位數驗證碼才能「登入」
- [x] 和官網一樣，手機號碼或驗證碼欄位留空離開時，下方顯示紅字「必填」，欄位外框變紅；手機號碼填完整後，空的驗證碼欄位也會顯示
- [ ] 用中文輸入法時，不必切換就能在手機號碼和驗證碼欄位打數字。欄位已關閉輸入法（`WA_InputMethodEnabled`），全形數字也會轉成半形，
  但 macOS 第一次用注音測試，仍然會經過輸入法。下一步：游標在這兩個欄位時切到英文輸入法，離開時切回（瀏覽器處理密碼欄位的做法）。Windows 也要測
- [x] QR Code 登入畫面：QR Code、失效倒數、失效後可重新整理
- [x] 「登入後即代表您同意服務條款及隱私權保護政策」連結，以及防詐提醒
- [x] 比照官網登入框的樣式；兩個分頁固定同一個大小，切換時視窗不會移動
- [x] 手機號碼登入改走 API（`sms/send_code`、`sms/login`），非同步執行，等待時按鈕停用；依規格 4.2 顯示每日簡訊上限（同號碼每日 10 則，台灣時間重置）與無開播資格
- [x] ~~重送間隔由後端提供~~：不需要（已決定），依產品規格固定 90 秒
- [x] QR Code 登入：`qrcode/create` 取得 QR Code（實際是 GET，文件寫 POST，但 POST 會回 405），依文件顯示 300 秒；停在 QR Code 分頁時每 2 秒查一次 `qrcode/verify`（1802 等待中、1801 已掃描：顯示「請在 App 上確認」、0 登入成功：和手機登入一樣、-1 已失效：可重新整理）。99302 與其他拒絕會結束這個 QR Code；網路錯誤則等下一次查詢。等待中的回應不寫 log。模擬後端在第 5 次查詢時確認登入
  - [x] 在 OBS 裡用 QA 後端實測，以 App 自己的 API 代替 App（`/v2/passport/qrcode`，type 0 掃描、type 1 確認）：1802、1801 只回一次、確認前一直是 1802，最後 0
  - [x] 用浪LIVE App 的掃碼頁實測通過（2026-11-17）
- [ ] 兩種方式共用
  - [x] OBS 開播資格：99302 阻擋登入並顯示規格中的提示
  - [x] 強制更新：插件版本低於 `min_supported_version` 時無法登入（見「更新」）
  - [x] 登入狀態：保存 token，開啟 OBS 時還原；收到 99303 時登出並請主播重新登入
  - [x] token 改存系統安全儲存，取代 `session.json`（`src/obs/secure-store-*.cpp`）：macOS 登入鑰匙圈的一般密碼、Windows
    認證管理員的一般認證，都是使用者層級，不需要管理者權限。`session.json` 只留暱稱；0.1.4 以前存在檔案裡的 token，下次啟動時自動搬過去。
    存不進去時不寫到磁碟，主播下次要重新登入。macOS 上這個項目屬於 OBS，OBS 的簽章變了時 macOS 會詢問一次。兩個解除安裝程式都會刪除它
  - [x] QA 版的登入和設定分開存：鑰匙圈 service `com.langlive.obs-plugin.qa`／認證管理員 `LangLive OBS Plugin (QA)/…`，
    設定檔放在插件設定資料夾裡的 `qa/`（`qa-override.json` 位置不變）。QA 的登入、未關閉的直播、已上傳的封面都不會帶到同一台電腦上的正式版。
    兩個解除安裝程式都會刪除兩邊的登入
  - [ ] Windows：在桌面登入的情況下測試登入的儲存、還原、刪除（以及解除安裝時刪除）；SSH 連線沒有認證管理員，所以只確認過建置與安裝程式腳本
  - [x] 登出時呼叫後端的 `POST logout` 註銷 token（只註銷這一個；有直播開著時，等關播送出後才註銷，因為關播要用這個 token）。
    呼叫失敗時本機一樣會登出，token 之後會自動失效。登入失效（99303）時不另外呼叫
  - [x] 暱稱由登入回應提供
- [ ] 第三方帳號登入（Facebook、Google、LINE、Apple）：尚未開始，不在第一版範圍

### 2. 開播設定

- [x] 依 wireframe 完成開播對話框：封面與裁切（800 × 800）、標題、開播推送文案、主／副衝刺活動、直播頻道、直播方向提示、直播場景、儲存設定、開始／結束直播
- [x] 「儲存設定」也會依直播場景與直播方向套用建議的推流設定（規格 4.6）
- [x] 直播中：標題為「開播 · 直播中」、顯示提示、所有欄位與「儲存設定」停用，只能「結束直播」
- [x] 開播成功後自動關閉；開播前檢查不通過時保持開啟，異常對話框疊在上面
- [x] 關閉（X 或 ESC）只是隱藏，所以不存檔的主／副衝刺活動與直播頻道會保留到登出
- [x] 開播、關播一律經由插件的 UI（設計原則，見 README）。用 OBS「停止串流」（或快捷鍵、OBS 放棄重新連線）停止插件直播時，
  直播整個結束：後端關播、清空 OBS 的推流金鑰，並跳出只有「知道了」的「結束直播」通知（「已為你停止串流，並結束直播。」）。從插件「結束直播」也會清空金鑰，
  但不跳通知；直播中關閉 OBS 也會清空。mock 後端（不給金鑰）時保留 OBS 原本的金鑰
- [x] 後端仍有一場從插件開的直播、但這個 OBS 並沒有在播（停止串流時網路斷線導致關播失敗，或當機），在後端 10 分鐘無推流自動關播前，
  會擋住開始直播。現在開啟開播視窗時會先關掉它（以 `streaming/info` 的 `live_id` 呼叫 `streaming/close`），不詢問主播（PM 決定：
  另一台電腦同時用插件開播的情況很少）；若當時關播失敗，按下開始直播會再試一次，成功前不會開播。網站開的直播與這個 OBS 自己的直播不受影響
- [x] OBS 開始插件的串流失敗時（例如編碼器無法啟動），不會送 frontend 事件，只有主視窗的 `StreamingStopped` Qt 訊號和 OBS 自己的
  錯誤視窗（`StartFailureWatcher`）。這時會像停止串流一樣關播、清空金鑰，不另外跳訊息；之前插件會一直停在「開播中」直到重開 OBS。
  還沒在真的 OBS 觸發過（用 fake 測試；OBS 31 找得到這個訊號）
- [x] 直播方向、直播頻道、衝刺活動，以及後端保存的標題／推送文案，開啟開播視窗時由 `streaming/info` 取得（`LoadGoLiveInfo`）；重新載入時保留主播已修改的內容
- [x] `streaming/info` 載入中：顯示轉圈與「正在載入直播間資訊…」，整個表單、「儲存設定」與「開始直播」都停用。API 呼叫總共最多等 20 秒
- [x] 「儲存設定」同時把標題與推送文案送到 `streaming/setting`；直播場景存在本機，封面見下方
- [x] 封面上傳：`upload/image` 取得 S3 預簽名網址，再帶它回傳的 header 用 PUT 上傳（`RequestCoverUpload`／`UploadCover`）；回傳的 `playUrl` 就是 `streaming/start` 的封面（`streaming/setting` 沒有封面欄位，帶 `liveimg` 也會被忽略）。PM 決定（2026-11-17）：按「上傳封面」直接開檔案選單，裁切視窗的「上傳封面」當下就上傳，失敗時顯示後端的訊息（例如每天 20 次的上限）並保留原本的封面；點縮圖沒有反應；「儲存設定」不上傳。
- [x] 開播視窗顯示直播間在後端的封面（`streaming/info`），每次載入都重新下載（網頁也能換封面）。在插件上傳的封面要等 `streaming/start` 才會變成直播間的封面，
  所以記下它（`uploadedCoverUrl`）和當時直播間的封面（`coverBaselineUrl`）：直播間封面沒變時，插件上傳的比較新，顯示並用它開播；變了（網頁上換過，或已經開播用掉）就以直播間的為準。重新登入會清掉
- [x] 顯示封面的審核狀態：不做（2026-11-17 決定）
- [x] 開始直播（`StartLive`）：封面、`streaming/start`、把回傳的推流網址與推流碼寫入 OBS（`SetStreamTarget`）後開始推流；非同步執行，並顯示錯誤（99304 Web 直播中、99302、網路）
- [x] 開始直播先查 `streaming/info`：Web 開播頁正在直播時，直接顯示「您目前正透過 Web 開播頁進行 OBS 直播…」，不執行開播前檢查；沒有直播時順便更新帳號的直播方向。（Web 直播中 `direction` 原本回的是那場直播的方向而不是帳號的，後端已修正）
- [x] 在呼叫 `streaming/start` 前先檢查推流設定，避免後端開了直播又被開播前檢查停掉（`ConfiguredStreamState`，讀取設定檔）
- [x] 重新連線：推流中斷時 OBS 會自動重試；開播對話框顯示「開播 · 重新連線中」與警告，不再看起來像正常直播中。若 OBS 放棄，會顯示 OBS 自己的錯誤訊息並結束直播（插件不再另跳視窗）
- [ ] （暫緩，2026-11-17）重新連線：與後端確認 OBS 重試期間直播間保留多久（OBS 預設每 2 秒重試，最多 25 次）
- [x] 結束直播：在 `OnStreamingStopped` 呼叫 `streaming/close`，涵蓋「結束直播」、OBS「停止串流」、放棄重新連線、連線失敗、開播前檢查；帶上 `streaming/start` 回傳的 `live_id`
- [x] ~~`streaming/close` 失敗時重試~~：不需要（已與後端確認）。這支 API 不會失敗；就算插件沒呼叫，後端偵測到一段時間沒有推流也會自動結束直播

### 3. 其他

- [ ] 開播方式：回報與查詢帳號目前的開播方式，讓插件與 Web 開播頁在直播中互相阻擋（規格 4.8）
- [ ] 更新 manifest 與 `min_supported_version`（見「更新」）
- [x] ~~選用：推流規格上限由後端提供，取代 `core/` 的預設值~~：這個版本不做（已決定），上限固定寫在 `core/`

## 開播前檢查

- [x] `core/` 實作規格 4.7.1 的 10 項檢查與文案，含單元測試
- [x] 進階輸出模式用品質為準的碼率控制（CRF、CQP、ICQ、無損）時，位元率設定不是上限，所以位元率檢查不通過；套用設定一律設成 CBR
  （VideoToolbox 沒有 CBR 時會改用 ABR）
- [x] 只檢查從開播對話框開播的直播（已與 PM 確認）：插件無法判斷 OBS 原生「開始串流」是不是推到浪LIVE，所以這類推流一律不檢查、不更動
- [x] 依 wireframe 完成「OBS 推流設定異常」對話框：解析度有誤時可選直播場景
- [x] 套用建議設定只改輸出解析度，不改來源（畫布）解析度，因為畫布是主播排好的場景。不檢查畫布長寬比（已與 PM 確認）：
  畫布和帳號方向不同也照常開播、不跳訊息，怎麼播由主播決定
- [x] 「套用設定並開始直播」（規格 4.7.2）：套用設定後，重新開始被檢查擋下的直播：用同一份表單再呼叫一次 `streaming/start`
- [x] 正在推流、錄影或開啟虛擬攝影機時，拒絕套用設定
- [x] 從插件開播時，在開始推流前檢查，而不是先開再立刻關。推流開始時會再用 OBS 實際的編碼器檢查一次
- [x] 關鍵影格間隔：OBS 簡單輸出模式沒有這個設定（編碼器預設為自動），所以簡單模式下插件會在 `STREAMING_STARTING` 時把它設成 2 秒，只影響該次推流，不跳訊息窗。進階模式則照舊檢查主播自己的設定：不是 2 秒就停止推流並跳出訊息窗，「套用建議設定」會把 2 秒寫進 `streamEncoder.json`。插件不會更改主播的輸出模式。已在 macOS 以 x264（簡單模式）測試
- [x] 視訊／音訊編碼：只支援 H.264 與 AAC。套用時把其他編碼改成 x264／AAC 寫進設定檔（簡單模式：`StreamEncoder`、`StreamAudioEncoder`；進階模式：`AdvOut` 的 `Encoder`、`AudioEncoder`，並以 CBR 重寫 `streamEncoder.json`）。硬體 H.264（NVENC、QSV、AMF、Apple VT）維持不變
- [x] OBS 只在啟動或儲存設定時建立編碼器，所以插件在 `STREAMING_STARTING` 時，替串流輸出換上符合新設定的 x264／AAC 編碼器（`UseProfileCodecs`），只影響該次推流；OBS 重開後就用它自己的
- [x] 已在 macOS 實測編碼切換：簡單與進階模式的 Apple VT HEVC、重開 OBS 後、搭配錄影
- [ ] 在 Windows 實測編碼切換（NVENC HEVC／AV1）

## UI

- [x] 所有對話框使用浪LIVE 官網的深色風格（`ui/style.hpp`），不受 OBS 主題影響
- [x] 提示訊息使用插件自己的提示框（`ShowMessage`），不用系統的訊息框
- [x] 匯出診斷資料對話框
- [x] 診斷資料會遮蔽名稱結尾是 key、token、secret、password、bearer、authorization 的欄位（不分大小寫），所以 OBS 的 `RefreshToken`
  （`basic.ini` 的 Twitch／YouTube／Restream）和 WHIP 的 `bearer_token` 也會被遮蔽
- [ ] 匯出診斷資料在 UI 執行緒執行，而且用 `std::regex` 遮蔽好幾 MB 的 log 要好幾秒
- [x] 開播視窗上有對話框時登出（或登入失效），開播視窗會先隱藏，等沒有任何對話框時才刪除；之前在子視窗執行中刪除會讓 OBS 當掉。
  「登入已失效」通知改成排隊顯示，不在 controller 處理到一半時跳出
- [ ] 插件的正式文案與翻譯（規格第 6 節待定）
- [x] 說明中心：官網 FAQ 的 OBS 插件段落（`/obsFaq?anchor=obsplugin`），和服務條款連結一樣依建置的網站與 OBS 語系產生
  （`core::WebsitePageUrl`）。頁面由 PM 指定
- [x] 透過插件開播中（含開播途中）不能登出（PM 決定）：選單的「登出」變灰色，結束直播後才能登出。用 OBS「開始串流」的推流不影響登出，登出也不動它；登入失效（99303）時不中斷直播，下次登入後補關

## 驗證

- [x] 在 macOS 建置（universal）並通過單元測試
- [x] 在 Windows 建置（Visual Studio 2026）
- [x] UI 測試（`tests/ui/`），macOS 與 Windows 皆通過：對話框以無畫面模式搭配假實作執行；涵蓋登入、開播設定（缺漏欄位、開播成功才關閉、直播中鎖定、重新連線中、ESC、儲存設定、封面上傳）、開播前檢查、版本更新、提示訊息、選單
- [x] 在 CI 執行 UI 測試（`ui-tests:macos`、`ui-tests:windows`，排在準備好 `.deps/` 的建置 job 之後）；發佈與 dev snapshot 都要等 UI 測試通過
- [x] ~~以真的 OBS 做端到端測試：obs-websocket + 本機 RTMP server + ffprobe（GOP、解析度、重新連線）~~：不需要（已決定）
- [ ] 在 macOS 與 Windows 的 OBS Studio 實際執行，測過每個選單項目（macOS 已部分測過；Windows 在沒有開啟智慧型應用程式控制的測試電腦能載入，選單項目還沒逐一測過）
- [x] CI 建置時 warning 視為 error（在 `macos`／`windows-x64` preset 加上 `-DCMAKE_COMPILE_WARNING_AS_ERROR=ON`；`macos-ci` preset 另外需要 ccache）

## CI

- [x] 在 GitLab runner 上執行 `.gitlab-ci.yml`：macOS 與 Windows 的 shell executor runner，所有 job 皆通過（runner 設定見 `AGENTS.md`）
- [x] macOS 與 Windows runner：建置插件與兩種安裝檔，保存為 job artifact
- [x] 在公司 GitLab 註冊 runner（共用 runner 都設了其他專案的 tag，所以 job 會一直 pending）：tag 為 `obs-mac` 與 `obs-windows`
- [x] `vX.Y.Z` tag 發佈 GitLab Release：安裝檔放在 generic package registry，SHA-256 寫在 release 說明（`scripts/ci/gitlab-release.sh`）
- [ ] 在 CI 為安裝檔簽章（macOS 已完成，見「程式碼簽章」；Windows 尚未）
- [x] 發佈 pipeline：版本 tag 先部署到 test，QA 測完再部署到正式環境，並更新 manifest。不使用 rc tag：QA 直接測該版本，
  尚未上線的版本可重新打 tag
- [ ] 已發佈版本同步到 GitHub
  - [x] `deploy:prod` 之後執行 `sync:github`：每個正式版在 GitHub 的 `main` 上一個 commit（內容等於該 tag，不推送內部歷史）、
    打上 tag，並建立附上正式版安裝檔的 GitHub Release（`scripts/ci/sync-github.sh`）；GitLab Release 尚未標示 released 的版本一律拒絕
  - [ ] 建立 GitHub repo（正式對外公開前先設私有），並在 GitLab 專案設定加入 `GITHUB_REPOSITORY` 與 protected、masked 的
    `GITHUB_PUSH_TOKEN`；在 `main` 手動執行 `check:github-access` 確認
  - [ ] repo 仍為私有時試跑：以 `SYNC_TAG=v0.1.7` 手動執行 `sync:github:tag`，到 GitHub 確認 commit、tag 與 Release，
    再刪除並重建 repo（並在 token 重新選取該 repo），讓第一個正式版成為 GitHub 上的第一個 commit
  - [ ] 第一個正式版發佈時完成首次同步，並到 GitHub 確認 tag、`main` 與 Release

## 發佈（S3／CDN）

發佈 pipeline 把安裝檔與更新 manifest 上傳到 S3；正式環境透過 CDN 下載。S3 金鑰與 CDN 由 DevOps 提供。

- [x] 環境：`dev` 為 snapshot，預設分支每次建置都覆蓋；`vX.Y.Z` tag 同時建置 QA 與正式版（`QA_*`／`PROD_*` 變數）
  並放進 GitLab Release；`deploy:qa`（自動）發佈到 `test/`，`deploy:prod`（手動）發佈到 `prod/`。金鑰放在 masked 的
  CI 變數（`PROD_*` 為 protected，只給 protected 的 `v*` tag；`TEST_*` 可寫 `test/` 與 `dev/`）
- [x] 更新 manifest（`scripts/ci/publish-manifest.sh`）：`test/latest.json`（bucket 網址）與 `prod/latest.json`（CDN，
  `PROD_DOWNLOAD_URL`），最後才寫入。major 版為強制更新，minor、patch 為一般更新（`UPDATE_MIN_SUPPORTED_VERSION`
  可覆寫）；比已發佈版本舊則拒絕。`released_at`：這個版本第一次發佈到該環境的時間（同一版本重新發佈時沿用）；QA 測試情境與本機測試
  server 也有這個欄位
- [ ] 更新視窗顯示 `released_at`（選填欄位；等 PM 決定要不要顯示、怎麼顯示）
- [x] QA 的更新測試情境：QA 版（`LANGLIVE_QA_BUILD`）每次檢查更新都會讀 `qa-override.json`（`update_manifest_url`），更新畫面
  和正式版一樣（只寫在 log）；`deploy:qa` 會用該版安裝檔發佈 `test/scenarios/{optional,forced,uptodate,bad-sha,broken}.json`。正式版沒有覆寫功能
- [x] QA 版的 OBS 選單名稱是「浪LIVE(Test)」（英文為 LangLive(Test)），用來區分 QA 版與正式版
- [x] tag 建置完成時，GitLab Release 附上四個安裝檔（`scripts/ci/gitlab-release.sh`，QA 版檔名加 `-qa`）；
  `deploy:prod` 成功後更新正式版狀態
- [x] 正式環境上線該版本（或更新的版本）之前，同一個 tag 可以重新發佈（`scripts/release/retag.sh`）；dev snapshot 每個版本只有一組檔案，
  檔名為 `...-vX.Y.Z-SNAPSHOT`（X.Y.Z 加上之後的修改），每次建置覆蓋
- [x] `retag.sh` 只有在讀到正式環境的版本時，才會刪除 Release、package 和 tag：manifest 連不上、回錯誤狀態（CDN 對不存在的檔案回 403）、
  或內容沒有 X.Y.Z 版本，都會停下來，不再當成「還沒發佈」；版本比正式環境舊、或 Release 已被 `deploy:prod` 標成已上線，也會拒絕
- [x] 安裝檔使用含版本號、不會變動的檔名，並設定長的 `Cache-Control`；只有 `latest.json` 會原地更新，設定 `max-age=60`
- [x] 上傳順序：先上傳安裝檔，再上傳 `latest.json`，避免 manifest 指向尚未上傳的檔案；`latest.json` 上傳後從 bucket 讀回比對
- [x] `deploy:prod` 後用 DevOps 提供的 CDN Flush API 清除 CDN 快取（`flush-cdn:prod`），在共用的 `ll` runner 上執行，因為只有它連得到這個 API；不 clone repo，DevOps 限定 S3 金鑰的範圍後（見下一項）也拿不到任何金鑰。清除的路徑由 `deploy:prod` 寫在 `cdn-flush-paths.txt`（`/latest.json` 與這個版本的兩個安裝檔，逐一列出：API 不接受 `*`）。已手動測過：API 接受插件的網域，也接受一次送多個路徑
  - [ ] DevOps 把 S3 金鑰限定在各自的 environment（`TEST_*`：`dev`、`test`；`PROD_*`：`production`）；在那之前，每個 job（包括共用 runner 上的）都拿得到金鑰
- [ ] `deploy:prod` 後透過 CDN 下載 `latest.json`，檢查版本與 sha256

## 開源

- [x] `LICENSE`（GPL v2 全文）；原始碼標示為 GPL-2.0-or-later
- [x] README：說明官方安裝檔對應的原始碼位置、第三方授權聲明、商標
- [ ] 在官網下載區與安裝檔附帶的授權檔中放上原始碼連結（GPL：散布安裝檔的地方要能取得對應原始碼；正式對外公開前，GitHub repo 與 tag 要先公開）
