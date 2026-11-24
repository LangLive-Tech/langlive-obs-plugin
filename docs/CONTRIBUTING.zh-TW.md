# 參與開發

[English](CONTRIBUTING.md) | 繁體中文

感謝你對浪LIVE OBS 插件的關注。

## 這個 repo 的運作方式

開發在浪LIVE 的內部 repo 進行。這個 GitHub repo 是唯讀鏡像，只在正式版發佈時更新：

- `main` 永遠等於最新的正式版，每個正式版一個 commit（不公開內部的開發歷史）
- 每個正式版都有 tag `vX.Y.Z`，以及附上安裝檔的 GitHub Release
- GitHub Release 上的安裝檔，就是用同一個 tag 的原始碼建置出來的

## 回報問題

請在 GitHub 開 issue，並附上：

- 插件版本、OBS Studio 版本與作業系統（macOS／Windows）
- 重現步驟，以及預期的結果
- 可以的話，附上診斷檔案：在 OBS 中打開「浪LIVE」選單，選擇匯出診斷資料。檔案只會存在你的電腦，
  直到你自己附上為止；敏感資訊會被過濾

請不要在 issue 中貼出帳號密碼或推流 key。

## Pull Request

Pull request 不會在 GitHub 上直接 merge。審查通過後，maintainer 會把變更套用到內部 repo，並保留你的
作者資訊，隨下一個版本發佈。之後會在 pull request 留言說明包含在哪個版本，再關閉它。

開 pull request 之前：

- 一個 PR 只處理一件事
- 確認在 macOS 與 Windows 都能建置（見[從原始碼建置](../README.zh-TW.md#從原始碼建置)）
- `core/` 與 `src/app/` 裡的邏輯要新增或更新單元測試（見[執行測試](../README.zh-TW.md#執行測試)）
- 不要 commit 帳號密碼、key，或正式環境預設值以外的後端網址
- 新增的第三方相依套件，授權必須與 GPL-2.0-or-later 相容，並把授權聲明加進安裝檔

## 版號

版號遵循 [SemVer](https://semver.org/)：

| 升版 | 時機 |
|---|---|
| MAJOR | 與浪LIVE 後端不相容，舊版將無法使用 |
| MINOR | 新功能 |
| PATCH | 修正 bug |

浪LIVE 後端會設定最低支援版本。低於該版本的插件必須先更新才能登入；通常發生在 MAJOR 版發佈時。

## 授權

提交貢獻即表示你同意你的貢獻以 GNU General Public License v2.0 或更新版本授權，與本專案相同。
詳見 [LICENSE](../LICENSE)。
