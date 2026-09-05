# AI 交接快照：Fast loudness engine WIP

最後更新：2026-09-05（Asia/Taipei）

這是有時效的工作快照，不是 release note 或完成宣告。接手前先讀 `AGENTS.md`，再以 `git status`、`git diff` 與遠端狀態複核本檔。

## 一句話狀態

正式版仍以 `v3.0.7`／commit `5547918517f17cd1cbdcb5968d8bd966a5c07a00` 為基底；目前分支保存可選兩段式 Fast engine 的 WIP checkpoint。功能原型在 commit `1ea996e`，本交接文件另以 docs commit 保存；兩者都只推功能分支，不進 `main`、不打 release tag。

## 正確工作位置

- Remote：<https://github.com/xup61069/loudness-correction-apo>
- 本機活躍 worktree：`G:\AICODE\iso226_2023\loudness-correction-apo-exp-v305`
- 文件分支：`codex/ai-handoff-fast-engine`
- 基底：`origin/main` = `5547918517f17cd1cbdcb5968d8bd966a5c07a00` = tag `v3.0.7`

不要在 `G:\AICODE\iso226_2023` 直接執行 Git；它會落到上層不相關的 `ae-effects-db` repository。也不要改用旁邊的 `eqapo`、`loudness-correction-apo-release` 或落後的 `loudness-correction-apo-v3\main`。

## WIP checkpoint

功能 checkpoint 相對 `v3.0.7` 的 patch 識別值：

```powershell
git diff --binary 5547918517f17cd1cbdcb5968d8bd966a5c07a00 1ea996e | git hash-object --stdin
# 11a06073b78e9e599bb0941915f9ed97892f37b1
```

在任何編輯前執行上式。若 hash 不同，先確認 commit 與基底是否正確，不要用 reset 強行「修回」。`1ea996e` 包含 10 個 tracked 檔案、約 `+718/-24`：

| 檔案 | 目前意圖 |
|---|---|
| `Benchmark/Benchmark.cpp` | 測試 `Engine Fast`／`Engine Full` 解析、缺省 Full、未知或重複值 fail closed，以及序列化 round-trip |
| `Editor/guis/LoudnessCorrectionFilterGUI.cpp` | 載入、儲存與切換 Fast engine；勾選時輸出 `Engine Fast` |
| `Editor/guis/LoudnessCorrectionFilterGUI.h` | 傳遞 `EngineMode` 並宣告 checkbox slot |
| `Editor/guis/LoudnessCorrectionFilterGUI.ui` | 新增 Fast engine checkbox 與英文 tooltip |
| `Editor/guis/LoudnessCorrectionFilterGUIFactory.cpp` | 把 parser 得到的 engine 傳入 GUI |
| `Editor/translations/Editor_zh_TW.ts` | 新增「快速引擎」與 tooltip 的台灣繁中翻譯 |
| `Editor/translations/Editor_zh_TW.qm` | 上述 `.ts` 對應的已編譯翻譯 |
| `filters/loudnessCorrection/LoudnessCorrectionFilter.h` | 新增 EngineMode、設定相容規則、Fast engine 常數與 helper 宣告 |
| `filters/loudnessCorrection/LoudnessCorrectionFilter.cpp` | 實作兩段擬合、係數、峰值／餘裕掃描及音量更新分流 |
| `scripts/test-runtime-loudness.ps1` | 加入 Fast runtime 輸出與未知 engine 必須 bypass 的整合檢查 |

## 目前設計

- Full engine 仍是預設值。舊 Schema 1 設定若沒有 `Engine` 欄位，維持 Full；序列化 Full 時省略欄位，避免改寫既有 profile 文字。
- Fast engine 只有明確的 `Engine Fast` 才啟用。`Engine Turbo` 之類未知值或重複 `Engine` 欄位會使設定無效並 fail closed。
- Fast 路徑以 120 Hz low shelf 加 12.5 kHz peaking filter（Q=2.0）擬合 Full 路徑的目標曲線；先解加權 least squares，再做一次 residual refinement。
- Fast 路徑使用較小的掃描與 refinement 次數估算峰值和校正分支餘裕；仍沿用既有 endpoint volume tracking、common-A／subsonic guard、安全交接及 coefficient-bank crossfade。
- 低取樣率會按 Nyquist 上限減少可用 Fast band；這條退化路徑尚需專門的數值誤差測試。

這些是現有 diff 的實作方向，不代表產品需求已凍結或數值品質已獲接受。

## 高優先數值風險

交接盤點曾用 Python 等價重算 Fast fit；它不是直接呼叫 private C++，也尚未成為 repository regression test。重算沿用 `tests/test_loudness_profile.py` 的正式 CSV、`contour_delta()`、`biquad_response()` 與 Full `fit_correction()`，再逐行對應 WIP 的 120 Hz shelf、12.5 kHz peak、權重 20、2×2 normal equation 與一次 residual refinement。Dense comparison 使用 20 Hz 至 `min(20 kHz, 0.499 × sample rate)` 的 4097 個對數間距點，未納入 25 Hz guarded crossover，也未套用各引擎各自計算的 headroom 常數衰減；目標是檢查程式註解所稱的 raw curve fit。

初步結果顯示現行 tooltip 的「fits ... to the full correction curve」與「much faster」都缺乏足夠證據：

| 條件 | Fast-to-target anchor max / RMS | Dense Fast-vs-Full max |
|---|---:|---:|
| 48 kHz、reference 80、current 42 phon、attenuation 1.0 | 1.808 / 0.980 dB | 10.364 dB，約 19.6 kHz，Fast 較低 |
| 48 kHz、reference 80、current 20 phon、attenuation 1.0 | 4.505 / 2.986 dB | 14.747 dB，約 19.8 kHz，Fast 較低 |
| 48 kHz、reference 80、current 0 phon、attenuation 1.0 | 13.775 / 8.527 dB | 13.832 dB，約 88.2 Hz，Fast 較高 |

在 44.1–384 kHz、reference 80、current 42 phon、attenuation 1.0，anchor max 約 1.79–1.90 dB，dense max 約 9.58–10.36 dB；8 kHz 只剩 low shelf，dense max 約 1.61 dB。高頻差距的主要疑點是 Fast 使用 12.5 kHz peaking，而 Full 頂帶是 high shelf。

接手者應先把這套計算變成可重現的原生或正式 Python test，確認與 C++ 輸出一致並定義產品可接受門檻。門檻未決定前，不宜把 Fast 當作 Full 的等價近似；至少要把 UI 文案改為明確揭露 reduced-accuracy two-filter approximation。速度宣稱也必須先有 timing benchmark。

## 2026-09-05 已驗證

### Python regression suite

```powershell
python -m unittest discover -s .\tests -p "test_*.py" -v
```

- 結果：exit 0，執行 161 個測試；160 通過，1 skipped。
- Skip：`test_built_host_cold_starts_and_hands_off`，原因是無法建立 global mapping（Win32 error 5）。

### Release build

```powershell
.\scripts\build-installer-x64.ps1 -Configuration Release
```

- 結果：exit 0，Release installer 建置成功。
- 本機產物：`Setup\EqualizerAPO-x64-3.0.7.exe`
- SHA-256：`47EF757AE07CE8E1696A9304A9A8F4A6C531FE54590D47F764A04F7F8ADCA810`
- Installer 與 checksum 是 ignored build artifact，不可提交。

### Runtime loudness integration

```powershell
.\scripts\test-runtime-loudness.ps1 -Configuration Release
```

- 結果：exit 0。
- Parser/runtime context、ReferenceOffset、舊設定 fail-closed、crossover、raw anchor fit、guarded transfer、1–19 Hz 路徑、cold-start handoff、音量轉換、Fast engine 非 bypass 與未知 engine bypass 均通過腳本現有檢查；未偵測到 clipping。

### Whitespace

`git diff --check` 為 exit 0。Git 仍提示 `Editor_zh_TW.ts` 與 `LoudnessCorrectionFilter.h` 將來可能 LF→CRLF；不要因此重寫整個檔案。

### Public history

```powershell
.\scripts\test-public-history.ps1 -Revision HEAD
```

- 結果：exit 0，`Public history check passed for HEAD.`

## 尚未驗證／尚未完成

- 尚未以直接 C++ 輸出或正式 regression test 重現上述 Fast-vs-Full 誤差，也沒有定義可接受的最大／RMS dB 門檻。
- 尚未量測 Full 與 Fast 的初始化時間、音量更新成本及實際 callback CPU 差異；「much faster」tooltip 目前沒有交接中的 benchmark 證據。
- 尚未對 8 kHz、44.1、48、96、192、384 kHz，以及多組 reference／volume／attenuation 的 Fast 退化與極端值建立完整矩陣。
- 尚未確認 `findFastMaximumResponseDb` 等新 helper 的所有配置與昂貴運算都只發生在非 audio-callback 路徑；需沿 call graph 與既有即時契約再審一次。
- 尚未執行 `capture-ui-regression.ps1`，也未人工檢查英文、`zh_CN`、`zh_TW`、深色、高對比與 DPI 畫面。
- 只有 `zh_TW` 新增翻譯；其他 locale 目前會回退英文。是否補齊需先確認專案既有翻譯策略。
- `README.md`、`README_zh-TW.md`、`CHANGELOG.md`、`version.h`、`vcpkg.json` 都尚未為 Fast engine 更新；這是刻意的，因功能還沒定案。
- 尚未執行真實裝置聆聽、端點切換、安裝升級／rollback／uninstall 測試。
- 尚未建立 PR；普通分支 push 不會自動觸發 Build workflow。

## 建議接手順序

1. 先確認 repo、branch、`1ea996e`、10 檔清單與 patch hash；不要 reset、不要切到旁邊的舊 clone。
2. 逐段 review parser、序列化、Fast fit、headroom scan 與 publish path，確認錯誤處理和即時執行緒邊界。
3. 在原生 Benchmark 或可驗證的獨立測試中加入 Fast-vs-Full 數值矩陣，先定義門檻，再調整 shelf／peak、權重與掃描參數。
4. 用可重現 benchmark 驗證初始化與更新成本，之後才決定 UI 是否能宣稱速度改善。
5. 執行 UI regression matrix 並檢查三種語系；任何 `.ts` 修改後重新產生相符 `.qm`。
6. 行為凍結後才補雙語 README 與 CHANGELOG。若只是功能 PR，不要提前升版或打 tag。
7. 後續只明確 stage Fast-engine 相關檔案，保持 commit 可審查；提交前再次跑完整驗證階梯。
8. 推 feature branch、開對 `main` 的 PR，等待 GitHub Actions。除非使用者另行明確要求 release，絕對不要建立 `v*` tag。

## Git 安全提醒

- `codex/ai-handoff-fast-engine` 應依序包含 Fast-engine WIP checkpoint `1ea996e`，以及只新增 `AGENTS.md`、`HANDOFF.md` 的文件 commit。
- 這是可遠端接手的 checkpoint，不是完成版。不得把 branch push、build success 或本文件誤解成已通過產品驗收。
- 旁邊的 `eqapo` 有大量不同歷史與未提交變更，`loudness-correction-apo-release` 停在舊 release，`loudness-correction-apo-v3\main` 也落後遠端；不可用整批複製方式「同步」。
- 發布前必須依 `Release checklist.txt` 完成版本同步、公開歷史、installer recovery、升降級與 checksum 驗證。
