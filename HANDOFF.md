# AI 交接快照：響度效能優化與 Fast engine WIP

最後更新：2026-09-05（Asia/Taipei）

這是有時效的工作快照，不是 release note 或完成宣告。接手前先讀 `AGENTS.md`，再以 `git status -sb`、`git diff`、`git diff --cached`、`git log --oneline origin/main..HEAD` 與遠端狀態複核本檔。

## 一句話狀態

`codex/ai-handoff-fast-engine` 以正式版 `v3.0.7` 為基底；目前已優化預設且數值等價的 Full engine，Release 原生量測顯示 callback 約省 44%～52%、初始化約省 62%～65%。兩段式 Fast engine 雖更省 CPU，但已確認與 Full 在極端條件可差約 10～15 dB，因此只保留為明確標示的實驗選項，不可設成預設或宣稱等價。目前不進 `main`、不打 tag、不建立 release。

## 正確工作位置

- Remote：<https://github.com/xup61069/loudness-correction-apo>
- 本機活躍 worktree：`G:\AICODE\iso226_2023\loudness-correction-apo-exp-v305`
- 分支：`codex/ai-handoff-fast-engine`
- 基底：`origin/main` = `5547918517f17cd1cbdcb5968d8bd966a5c07a00` = tag `v3.0.7`
- 早期 Fast 原型 checkpoint：`1ea996e`
- 初版交接文件 checkpoint：`59840a9`

不要在 `G:\AICODE\iso226_2023` 直接執行 Git；它會落到上層不相關的 `ae-effects-db` repository。旁邊的 `eqapo`、`loudness-correction-apo-release` 與 `loudness-correction-apo-v3\main` 也不是本分支的正式工作位置。

## 本輪解決的效能問題

### 即時 callback

- 以 48 kHz 為例，原本 settled Full 路徑每個 sample、每個 channel 都要跨 14 段 low-pass、14 段 high-pass 與 29 段 correction BiQuad，共走訪 57 個物件；記憶體存取與函式狀態切換是主要成本。低取樣率會依 Nyquist 減少 correction band。
- `BiQuad::processBlock()` 現在一次把完整 block 跑過單一 section，將係數與歷史留在 local/register，再進下一個 section。
- `LoudnessCorrectionFilter::initialize()` 依 `maxFrameCount` 預先配置一份共用 low-pass scratch。callback 不會配置、resize 或釋放記憶體，且逐 channel 重用 scratch，沒有額外的 channel 倍數配置。
- 只有 common-A 已完成、沒有 handoff／warmup／crossfade／bypass fade 的 settled 狀態走 block 路徑；狀態可能在 block 中改變的轉換仍走原 sample-major 路徑。
- Identity／runtime-bypass bank 不再白跑 correction cascade。未來重新使用該 bank 前，既有流程會 reset 並 warm 新係數。
- 原生回歸同時比較 Full／Fast、in-place／out-of-place，以及音量更新後的轉換；block 與 scalar 最大逐樣本誤差為 `0`。

### 初始化與音量更新

- Full 的主要非即時成本是 4097 點 dense headroom scan，不是 response-matrix inverse。
- 每個頻率現在只計算一次 `z`／`z²`，整個 correction cascade 先以 complex response 相乘，最後只做一次 `norm`／`log10`；guarded crossover response 也重用同一組 `z`／`z²`。
- 原生 oracle 以舊版逐 band dB 相加方式，涵蓋 8/48/384 kHz 與音量 -100/-38/0 dB；新 aggregate response 最大差為 `2.84e-14 dB`。

## 可重現量測

環境：Windows x64 Release、48 kHz、2 channels、每次量測約 1,048,576 frames、7 次取中位數。同一台機器與同一工作階段先量 baseline，再套用最佳化；實際數字仍會受 CPU 排程與電源策略影響。

| Full engine 項目 | 最佳化前 | 最佳化後（兩次穩定重跑） | 改善 |
|---|---:|---:|---:|
| initialize | 16.47–16.97 ms | 5.96–6.27 ms | 約 62%–65% |
| volume update | 16.00–19.42 ms | 5.57–6.07 ms | 約 62%–71% |
| callback，256 frames | 159.21–181.92 ns/sample | 87.16–89.73 ns/sample | 約 44%–52% |

同一個最佳化後 binary 的 block/scalar 比較：

| Batch | Block | Scalar fallback | Block / scalar |
|---:|---:|---:|---:|
| 16 | 59.22–60.33 ns/sample | 171.88–172.56 ns/sample | 0.345–0.350 |
| 64 | 71.84–72.34 ns/sample | 163.65–171.83 ns/sample | 0.418–0.442 |
| 256 | 87.16–89.73 ns/sample | 158.99–159.85 ns/sample | 0.545–0.564 |
| 1024 | 94.48–94.58 ns/sample | 150.42–152.64 ns/sample | 0.619–0.629 |

Fast 在同一輪為 46.04–47.26 ns/sample、initialize 0.12 ms 左右，但這只是成本數字，不代表其曲線品質已合格。

重跑方式：

```powershell
.\build-local-x64.ps1 -Configuration Release
$previousPath = $env:Path
try {
    $env:Path = (Resolve-Path '.\Setup\lib64').Path + ';' + $previousPath
    .\Benchmark\x64\Release\Benchmark.exe --nopause --loudness-performance
} finally {
    $env:Path = $previousPath
}
```

## 嘗試與決策紀錄

| 嘗試 | 結果 | 決策 |
|---|---|---|
| 固定兩段式 Fast engine | CPU 很低，但 Python 等價重算顯示極端條件可偏離 Full 約 10～15 dB | 保留為 explicit experimental，不設預設、不當作效能修正主體；UI 加入風險說明 |
| Settled section-major block processing | 16～1024 frames 均比 scalar 快，且原生測試逐樣本完全一致 | 保留 |
| 每頻率共享 `z`／`z²`，aggregate complex response 後一次取 log | Full initialize/update 大幅下降；與舊計算最大差 `2.84e-14 dB` | 保留 |
| 改動 handoff／warmup／crossfade 的 sample 時序 | 可能破壞即時安全與聲音連續性 | 未採用；轉換路徑維持原行為 |

## Fast engine 的已知限制

Python 等價重算沿用正式 profile CSV 與 Full fit，在 48 kHz、reference 80 phon、attenuation 1.0 時得到：

| Current level | Fast-to-target anchor max / RMS | Dense Fast-vs-Full max |
|---:|---:|---:|
| 42 phon | 1.808 / 0.980 dB | 10.364 dB |
| 20 phon | 4.505 / 2.986 dB | 14.747 dB |
| 0 phon | 13.775 / 8.527 dB | 13.832 dB |

因此 GUI 已改成 `Experimental fast engine`，tooltip 明示它是可能明顯偏離 Full 的 two-filter approximation，尤其在很低的聆聽音量。若要產品化 Fast，必須先建立直接取 C++ response 的完整誤差矩陣並訂出可接受門檻；不能只看效能數字。

## 本輪檔案範圍

| 檔案 | 用途 |
|---|---|
| `filters/BiQuad.h` | 新增無配置、保持 recurrence 順序的 block processor |
| `filters/loudnessCorrection/LoudnessCorrectionFilter.{h,cpp}` | 預配 scratch、settled block 路徑、identity skip、response scan 最佳化 |
| `Benchmark/Benchmark.cpp` | block/scalar 精確回歸、aggregate/legacy oracle、`--loudness-performance` |
| `Editor/guis/LoudnessCorrectionFilterGUI.ui` | Fast 改為實驗性標示與誤差警告 |
| `Editor/translations/Editor_zh_TW.{ts,qm}` | 台灣繁中標示；`.qm` 必須由 build 重新產生 |
| `AGENTS.md` | 固化 callback 路徑契約與 benchmark 指令 |
| `HANDOFF.md` | 保存本輪證據、限制與後續工作 |

## 驗證狀態

目前已完成：

- `build-local-x64.ps1 -Configuration Release`：成功，0 warning、0 error。
- `Benchmark.exe --nopause --loudness-transition-test`：完整 native loudness suite 通過；aggregate oracle 最大差 `2.84e-14 dB`，Full/Fast block/scalar 的 in-place/out-of-place 最大差皆為 `0`。
- `Benchmark.exe --nopause --loudness-performance`：修正小 batch 暖機上限後連續兩次成功，沒有未 settle 警告；上述各 batch 均改善。
- `python -m unittest discover -s .\tests -p "test_*.py" -v`：exit 0；161 tests 中 160 通過、0 失敗、1 skipped。Skip 為 `test_built_host_cold_starts_and_hands_off`，本機無法建立 global mapping（Win32 error 5）。
- `scripts\build-installer-x64.ps1 -Configuration Release`：exit 0；產生 ignored 的 `Setup\EqualizerAPO-x64-3.0.7.exe`，SHA-256 `a97e576ef04c666d4b91f72f66a8ddefee1adf531dee6d01feeb5bf1c7a44c59`。同一流程重新產生 `Editor_zh_TW.qm`，626/626 finished、0 unfinished。
- `scripts\test-runtime-loudness.ps1 -Configuration Release`：exit 0；parser/runtime、guarded response、次聲頻、identity、轉換、cold handoff、Fast 非 bypass 與未知 engine fail-closed 均通過，未偵測到 clipping。
- `scripts\capture-ui-regression.ps1 -Configuration Release`：exit 0；產生 90/90 張 PNG 與 manifest，涵蓋 en/zh_TW、light/dark/high-contrast、100%～200% DPI 與 150% text。人工抽查響度面板的 zh_TW light/dark/high-contrast 與 100%/150%/200% 樣本，未見新增文案截字或版面退化。德／法／簡中仍有各 9 筆既有 unfinished、150 筆未翻譯來源文字；本輪 zh_TW 為 0 unfinished。
- `scripts\test-public-history.ps1 -Revision HEAD`：commit 後執行，exit 0，`Public history check passed for HEAD.`
- `git diff --check`：通過；Git 的 LF→CRLF 提示不是 whitespace error，不要因此重寫整檔。

Commit-time 自動驗證階梯已完成；環境限制與尚未執行的真實裝置測試列在下一節。

## 尚未完成／不得誤報

- 尚未做真實音訊裝置的長時間 CPU、dropout、聆聽與端點切換測試；原生 benchmark 是可重現的 microbenchmark，不等於整機 CPU 百分比。
- Fast 的數值品質未通過產品門檻，只有 `zh_TW` 為新文案提供翻譯，其他 locale 回退英文。
- 尚未建立 PR，也沒有 GitHub Actions 結果。普通 feature-branch push 不代表 CI 已跑。
- 沒有修改 `README.md`、`README_zh-TW.md`、`CHANGELOG.md`、`version.h` 或 `vcpkg.json`，因為目前沒有發布或升版授權。
- 沒有建立或推送 `v*` tag，也沒有 GitHub Release。

## 建議接手順序

1. 先用 `git status -sb`、`git diff`、`git diff --cached`、`git log --oneline origin/main..HEAD` 與 `git diff origin/main...HEAD` 複核真實狀態。
2. Review settled block path時，優先檢查 no-allocation、`frameCount <= maxFrameCount`、in-place scratch 次序，以及 transition fallback 條件。
3. 若繼續改善 CPU，以 profiler 與 `--loudness-performance` 建 baseline，並保留 block/scalar exact regression；不要犧牲 Full 曲線或轉換安全。
4. 若繼續 Fast，先把 C++ response 誤差矩陣變成正式 gate，再調整 filter topology；現有兩段式結果不可直接發布。
5. 功能行為凍結後才補雙語 README／CHANGELOG、開 PR 並等待 Actions。除非使用者另行明確要求 release，不得建立 `v*` tag。

## Git 安全提醒

- 只明確 stage 上表檔案；禁止 `git add .`、`git add -A`、破壞性 reset 或 checkout。
- Installer、checksum、PDB、local build 與暫存 profiling 產物不進 Git。
- 這個分支是可遠端接手的功能分支，不是正式完成版；不得把 push、build success 或本文件誤解成已通過產品驗收。
