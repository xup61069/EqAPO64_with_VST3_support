# AI 交接快照：目前基線與剩餘限制

最後更新：2026-09-05（Asia/Taipei）

## 狀態

目前沒有待接手的程式碼 WIP。響度校正已具備選用的 APO 寬頻音量跟隨、三種曲線、10 ms 平滑轉換與端點失效安全；啟用接近中性的響度校正時，舊有固定約 1 dB 衰減也已移除。

本檔只保存有時效的交接摘要，不是 branch 定位器，也不是 release note。開始工作先讀 `AGENTS.md`，再執行：

```powershell
.\scripts\agent-status.ps1 -Fetch
```

若 Git、GitHub 或測試結果與本檔不同，以即時結果為準。版本變更請看 `CHANGELOG.md`；引擎與音量跟隨的架構理由分別見 [ADR-0001](docs/decisions/0001-loudness-engine-modes.md) 與 [ADR-0002](docs/decisions/0002-apo-volume-follow.md)。

## 已確立的響度行為

- Full 是預設且較準確的引擎；Fast 是必須明確選用、最多兩段的實驗近似，不能宣稱和 Full 數值等價。
- `VolumeFollow` 缺省／`Off` 保持舊行為；`Linear` 使用 scalar、`Logarithmic` 使用 `scalar²`、`Windows` 使用 dB 的 `10^(dB/20)`。自動模式讀取端點 dB／scalar／mute，手動模式則由 `Volume` 推導 dB 與 scalar；兩者都不寫回 Windows 音量。
- 跟隨增益在校正與 common-A 合成後才套用；`Attenuation 0` 只關閉音色補償，仍保留跟隨，`State 0` 才 bit-transparent 地旁路兩者。
- 自動模式冷啟動沒有有效快照時保持靜音；成功讀取過一次後，短暫失敗會保留最後有效跟隨增益。端點靜音是精確零增益，恢復與變更使用所有聲道共用的 10 ms ramp。
- 只改變端點 scalar／mute 不會重新擬合響度輪廓；只有真正影響音色校正的 dB 變更才進入 bank warmup／crossfade 流程。

## 最近驗證證據

- Release installer 已完整建置；本機產物為 `Setup\EqualizerAPO-x64-3.0.7.exe`，SHA-256 `b5631392eb7f9d9dad73bd8657155e31c99e51a6aca423b152cbd61c5d9fad6b`。產物不提交 Git，也不代表已發布 release。
- 原生 transition／runtime loudness 測試通過，涵蓋三種曲線、mute、冷啟動與恢復、Full／Fast、立體聲、in-place／out-of-place 及接近中性輸出。Full／Fast 接近中性誤差分別為約 −0.000541／−0.000543 dB。
- Python 共 163 項：162 通過；`test_built_host_cold_starts_and_hands_off` 因目前環境無法建立 global mapping（Win32 error 5）跳過 1 項，不能交接成「全部通過」。
- 這台機器的 Release microbenchmark（48 kHz、2 聲道、主要 batch 256）：Full 85.176 ns/sample、Fast 48.999 ns/sample；Full scalar fallback 158.168 ns/sample。額外 Full block／scalar 結果為 batch 16：70.822／188.772、batch 64：83.874／185.391、batch 1024：100.601／179.183 ns/sample。這是相對回歸資料，不能直接換算成真實裝置 CPU 百分比。
- UI regression 腳本的 90 張 theme／DPI 矩陣已成功產生，18 張繁中 dense 畫面人工檢查沒有重疊或錯誤狀態；但新增的跟隨下拉在預設捲動位置下方，既有腳本也不開啟校準對話框，因此 combo／tooltip／校準視窗本身仍只有編譯、翻譯與靜態幾何契約覆蓋，不能宣稱已完整視覺驗證。

## 仍存在的限制

- 尚無 VB-Audio Matrix 真實路由的長時間聆聽、dropout、端點切換與休眠喚醒驗證；首次使用應以安全音量確認 `Binding` 與曲線。
- 一般會自行套用 Windows 或硬體衰減的路徑若再啟用 `VolumeFollow`，會形成雙重衰減；不確定時維持 `Off`。
- Windows 若拒絕解除 endpoint notification callback，callback 本身可安全存活且不再指向 controller，但其 COM 註冊資源可能延後到端點釋放才回收。
- Fast 曲線品質未達一般模式門檻；行程外 VST host 仍是實驗功能，不是安全沙箱。
- 安裝程式目前未簽章；除非使用者明確要求 release，不得建立或推送 `v*` tag。

## 接手建議

1. 先用 `agent-status.ps1 -Fetch` 確認 repository、remote、branch、ahead/behind、dirty files 與 worktree。
2. 修改跟隨語意前完整閱讀 ADR-0002，並保留 `AGENTS.md` 指定的 parser、即時 callback、失敗恢復與 UI 測試矩陣。
3. DSP／parser 修改先跑 Python 契約，再跑 Release installer、runtime loudness 與 `--loudness-performance`；UI 變更另跑並人工檢查 regression snapshots。
4. README 只描述目前行為；版本歷史只更新 `CHANGELOG.md`，不要把 release log 複製回 README。
