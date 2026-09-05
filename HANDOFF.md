# AI 交接快照：目前基線與剩餘限制

最後更新：2026-09-05（Asia/Taipei）

## 狀態

目前沒有待接手的程式碼 WIP。響度 Full engine 效能最佳化與 Experimental Fast 標示已由 [PR #3](https://github.com/xup61069/loudness-correction-apo/pull/3) squash merge；合併 commit 為 `ab860b1b28c121bb3f454e61ee7eddb9c04688af`，PR 與 `main` push 的 GitHub Actions 都成功。

本檔只保存有時效的交接摘要，不是 branch 定位器，也不是 release note。開始工作先讀 `AGENTS.md`，再執行：

```powershell
.\scripts\agent-status.ps1 -Fetch
```

若 Git、GitHub 或測試結果與本檔不同，以即時結果為準。版本變更請看 `CHANGELOG.md`，架構理由請看 `docs/decisions/`。

## 已確立的引擎決策

- Full 是預設且較準確的引擎；穩態 callback 使用無配置的 section-major block 路徑，handoff／warmup／crossfade／bypass fade 仍走 sample-major 路徑。
- Full 的前 28 個可用頻帶使用 Q=2.2 peak，12.5 kHz 頻帶可用時使用 Q=0.9 high shelf；不可沿用舊品質因子。
- Fast 是最多兩段的近似：固定使用 120 Hz 低架，取樣率可可靠表示 12.5 kHz 時才加入 Q=2 高頻峰值；只能明確選用。極低聆聽級別測試曾與 Full 相差約 10–15 dB，不可設成預設或宣稱數值等價。
- 完整決策、效能數據與修改護欄見 [ADR-0001](docs/decisions/0001-loudness-engine-modes.md)。

## 最近驗證證據

- PR CI：[run 33948261982](https://github.com/xup61069/loudness-correction-apo/actions/runs/33948261982)，成功。
- `main` push CI：[run 33949096262](https://github.com/xup61069/loudness-correction-apo/actions/runs/33949096262)，成功。
- 流程涵蓋公開歷史、Python 回歸、x64 installer、runtime loudness、行程外 VST host、90 組 UI theme／DPI 快照與 artifact 上傳。
- 本機效能量測的環境與結果保存在 ADR；不要把 microbenchmark 直接換算成真實裝置 CPU 百分比。

## 仍存在的限制

- 尚無真實音訊裝置的長時間 CPU、dropout、聆聽與端點切換驗證。
- Fast 曲線品質未達一般模式門檻；若要繼續開發，先建立由 C++ 直接輸出的 response-error matrix 與可接受誤差 gate。
- 行程外 VST host 仍是實驗功能，不是安全沙箱。
- 發布的 installer 目前未簽章；只能由本 repository 的 GitHub Release 下載並核對同一 Release 的 SHA-256。
- 除非使用者明確要求 release，不得建立或推送 `v*` tag。

## 接手建議

1. 先用 `agent-status.ps1 -Fetch` 確認 repository、remote、branch、ahead/behind、dirty files 與 worktree。
2. 依 `AGENTS.md` 的任務路由只讀相關文件，避免把本交接快照當成永久規格。
3. DSP／parser 修改先跑 Python 契約，再跑 Release build 與 runtime；效能修改另跑 `--loudness-performance`。
4. README 只描述目前行為；版本歷史只更新 `CHANGELOG.md`，不要再把 release log 複製回 README。
