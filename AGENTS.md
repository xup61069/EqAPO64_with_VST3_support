# AGENTS.md — AI 協作者操作規範

本檔適用於整個 repository。AI 開始工作時先讀本檔；若根目錄有 `HANDOFF.md`，再讀取其中有日期的暫時性交接快照。對使用者以台灣繁體中文溝通，程式碼、註解與 commit message 則沿用專案既有的英文風格。

## 先確認你在正確的 repository

正式來源是 <https://github.com/xup61069/loudness-correction-apo>。本機上層目錄可能同時包含數個舊 clone、worktree、安裝檔、dump 與另一個完全無關的 Git repository；不可從上層目錄直接執行提交。

開始前執行：

```powershell
$repoRoot = (git rev-parse --show-toplevel).Trim()
$originUrl = (git remote get-url origin).Trim()
$repoRoot
$originUrl
git status -sb
git worktree list
```

`origin` 必須指向 `xup61069/loudness-correction-apo`。若 `git rev-parse` 回到 `G:\AICODE`、remote 是 `ae-effects-db`，或 repo 根目錄不含本專案的 `EqualizerAPO.sln`，立即停止；那是錯誤的上層 repository。上層針對 After Effects 資料庫的 `AGENTS.md` 不適用於本專案。

工作樹若已有修改，全部先視為使用者或前一位 AI 的工作。先讀 diff、保留它們，只明確 stage 本次檔案；禁止用 `git add .`、`git add -A`、破壞性 reset 或 checkout 清理工作樹。

## 專案定位

這是 Mixomo `EqAPO64_with_VST3_support` 的非官方 Windows x64 fork，產品功能包括系統層 Equalizer APO、x64 VST2/VST3 效果器支援、響度校正、Configuration Editor、Device Selector 與更新程式。

不可把本專案描述成官方 Equalizer APO 發行版，也不可宣稱響度功能符合、通過、獲認證或獲背書於任何標準。對外措辭與資料授權以 `README.md`、`README_zh-TW.md` 及 `NOTICE.md` 為準。

目前版本不可硬編在耐久文件或腳本；有日期的 `HANDOFF.md` 可記錄當時快照。需要即時版本時執行：

```powershell
.\scripts\get-project-version.ps1
```

## 開工前必讀

- `HANDOFF.md`：最近一次 AI 交接快照；內容有時效，必須以實際 Git 狀態複核。
- `CONTRIBUTING.md`：工程規則、測試與 PR 要求。
- `README_zh-TW.md` 與 `README.md`：產品行為、設定格式、相容性、安全限制及建置方式。
- `CHANGELOG.md`：目前功能與歷史脈絡。
- `NOTICE.md`：響度資料與實作的授權邊界。
- `SECURITY.md`：安全問題通報與支援範圍。
- `Release checklist.txt`：只有發布工作才使用；不得略過其中任何一步。
- `third_party/README.md`：第三方相依套件的來源與重建方式。

## 結構速查

| 路徑 | 職責 |
|---|---|
| `filters/loudnessCorrection/` | 響度輪廓、濾波擬合、音量追蹤、係數更新與即時 DSP |
| `Editor/guis/` | Configuration Editor 的 Qt GUI、設定遷移、校準與工作室面板 |
| `EqualizerAPO/` | 系統 APO runtime |
| `EqApoOutProcHost/` | 實驗性行程外 VST host |
| `Benchmark/` | 原生 parser、DSP、交接與安全契約測試 |
| `Setup/` | NSIS installer；產生的 `.exe`／`.sha256` 不進 Git |
| `scripts/` | 相依套件、建置、staging、runtime、UI 與公開歷史檢查 |
| `tests/` | Python 靜態契約與回歸測試 |
| `.github/workflows/` | 正式 build 與 release 流程 |
| `third_party/` | bootstrap 管理的相依套件；不要手動提交其建置輸出 |

## 不可破壞的工程契約

### 即時音訊

- Audio callback 內不得配置或釋放記憶體、等待或取得可能阻塞的鎖、寫 log、呼叫系統 API，或執行不可預測的 I/O。
- 昂貴的輪廓擬合、端點查詢、峰值搜尋與係數準備必須在非即時路徑完成；callback 只消費已發布且生命週期安全的狀態。
- 保留既有 raw → common-A 安全交接、預熱、淡入、雙 bank 係數交叉淡化及失敗時 bypass 的行為。
- 輸出餘裕掃描只降低校正分支，不得把共同的次聲頻路徑一起壓低。它不是 limiter；文件不可暗示能保證 sample peak 或 true peak。

### 設定與相容性

- 已標記的設定格式必須可 round-trip；必填欄位及 `Binding`／`Engine` enum 的無效值要 fail closed，重複的已知欄位也要 fail closed。`Attenuation` 與 `Volume` 為舊設定相容例外：缺漏或格式無效時分別回退 1.0 與自動音量。既有 parser 也會忽略不認識的額外 token 以保留 forward compatibility；不可順手改變這些語意。
- 未標記的舊響度設定不可依數值猜測模型；保留原文並 bypass，直到使用者在 Editor 明確選擇遷移方式。
- 新增欄位時必須定義缺省值、舊設定行為、序列化規則、未知值與重複值行為，並加入原生回歸測試。
- 不得讓 UI 預覽、離線分析與實際 runtime 對同一份已儲存設定產生不同語意。

### Installer、資料與供應鏈

- Installer 會使用既有 Equalizer APO 路徑與登錄位置；升級、rollback、uninstall 只在可拋棄的 Windows 測試環境驗證。
- 不得刪除或覆寫使用者的設定、IR、耳機校正資料或其他非本產品擁有的檔案。
- 不得提交 credentials、installer、dump、proprietary plug-in、未確認可再散布的聲學資料或第三方資料集。
- `scripts/test-public-history.ps1` 會掃描整段可達 Git 歷史；禁入資料即使後來刪除，仍會污染公開歷史。
- GitHub Actions 必須以完整 commit SHA 固定；第三方建置 job 不得持有 repository write token。

### UI 與翻譯

- UI 變更至少檢查英文、`zh_CN`、`zh_TW`，並包含一般／深色／高對比與常用 DPI。
- `zh_TW` 使用台灣用語且不可留下 unfinished 字串。
- 追蹤中的 `.qm` 必須由相符的 `.ts` 重新產生；不可只提交其中一邊，也不可順手重排或正規化整批翻譯檔。

## 驗證階梯

所有變更至少執行：

```powershell
python -m unittest discover -s .\tests -p "test_*.py" -v
git diff --check
```

C++、DSP、project wiring 或 installer 變更再執行：

```powershell
.\scripts\build-installer-x64.ps1 -Configuration Release
.\scripts\test-runtime-loudness.ps1 -Configuration Release
```

行程外 VST host 變更再執行：

```powershell
python -m unittest discover -s .\tests -p "test_outproc_vst_lifecycle.py" -v
```

UI 變更再執行並人工檢查產物：

```powershell
.\scripts\capture-ui-regression.ps1 -Configuration Release
```

準備 PR 或 release 時再執行：

```powershell
.\scripts\test-public-history.ps1 -Revision HEAD
```

Runtime 測試依賴已建好的 `Benchmark\x64\Release\Benchmark.exe` 與 staged runtime，因此要在 Release build 後執行。任何因權限或環境而 skipped 的測試都要在交接中逐項列出，不能寫成「全部通過」。

## Git 與發布安全

- 從最新 `origin/main` 建立短期 `codex/` 分支；不得直接把混雜工作樹推到 `main`。
- 只用 `git add -- <明確路徑>`，接著檢查 `git diff --cached --name-only` 與 `git diff --cached`。
- 每個 commit 聚焦單一主題；文件、功能、測試可依可審查性拆分，但不可遺漏功能所需的測試與使用者文件。
- 禁止 force push 到共享分支，禁止用 reset／checkout 清掉別人的改動。
- 未經使用者明確要求發布，不得建立或推送 `v*` tag。推送這類 tag 會觸發正式 GitHub Release。
- 版本更新必須同步 `version.h`、`vcpkg.json`、`CHANGELOG.md`、雙語 README、release workflow 的手動預設值，以及 `tests/test_loudness_safety_contract.py` 的版本契約。
- Build workflow 會在 `main` push、對 `main` 的 PR 或手動執行時觸發；普通功能分支 push 本身不代表 CI 已跑。

## 完工與交接

交付時必須清楚列出：

1. 修改了什麼，以及刻意沒碰哪些既有修改。
2. 實際執行的驗證、結果、skip 與未執行項目。
3. 仍存在的風險、限制與下一個可執行步驟。
4. branch、commit、push、PR、tag 與 release 的真實狀態。

若仍有未完成工作，更新根目錄 `HANDOFF.md` 的日期與快照；不要把易過期的 branch、dirty file 或一次性待辦塞回本檔的長期規範。
