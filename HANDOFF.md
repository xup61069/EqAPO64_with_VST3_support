# AI 交接快照：目前基線與剩餘限制

最後更新：2026-09-05（Asia/Taipei）

## 狀態

目前沒有待接手的程式碼 WIP。專案有兩個完全獨立的響度校正元件：`LoudnessCorrection:` 是公式版，`LoudnessCorrectionOriginal:` 是 Mixomo 原始雙棚架版；兩者的 command、parser、runtime DSP、GUI、校準流程與狀態皆分離。公式版具備選用的 APO 寬頻音量跟隨、三種曲線、10 ms 平滑轉換與端點失效安全；原版則固定追蹤 Windows 預設 Multimedia 播放端點，且已修掉中性差值分支造成的啟動降音量。IR 卷積的 UI 名稱已縮短為「IR 卷積」，載入與即時 block-size 交接也已改成有界、失敗時直通的流程。

本檔只保存有時效的交接摘要，不是 branch 定位器，也不是 release note。開始工作先讀 `AGENTS.md`，再執行：

```powershell
.\scripts\agent-status.ps1 -Fetch
```

若 Git、GitHub 或測試結果與本檔不同，以即時結果為準。版本變更請看 `CHANGELOG.md`；公式引擎、音量跟隨、獨立原版元件、VST MIDI 與 IR 卷積的架構理由分別見 [ADR-0001](docs/decisions/0001-loudness-engine-modes.md)、[ADR-0002](docs/decisions/0002-apo-volume-follow.md)、[ADR-0003](docs/decisions/0003-separate-original-loudness-component.md)、[ADR-0004](docs/decisions/0004-vst-midi-parameter-control.md) 與 [ADR-0005](docs/decisions/0005-ir-convolution-runtime.md)。

## 已確立的行為

- 不得把原版做成公式版的模式或共享參數；原版格式固定為 `Schema 1 Model MixomoShelfV1 State … ReferenceLevel … ReferenceOffset … Attenuation …`，公式版維持 `FormulaLoudnessV1`。
- 兩個校準對話框在接受、取消或關閉前都先停止循環粉紅噪音；temporary-audio 復原若保留外部修改，本次量測必須丟棄，不能再觸發即時儲存覆蓋該檔案。
- 原版保存 Mixomo 的 75 Hz/Q 0.52、10 kHz/Q 0.9 與 preamp 公式；`State 0` 必須 bit-transparent，端點不可用時直通，更新使用預配置雙 bank、25 ms warmup 與 10 ms crossfade。
- Full 是預設且較準確的引擎；Fast 是必須明確選用、最多兩段的實驗近似，不能宣稱和 Full 數值等價。
- `VolumeFollow` 缺省／`Off` 保持舊行為；`Linear` 使用 scalar、`Logarithmic` 使用 `scalar²`、`Windows` 使用 dB 的 `10^(dB/20)`。自動模式讀取端點 dB／scalar／mute，手動模式則由 `Volume` 推導 dB 與 scalar；兩者都不寫回 Windows 音量。
- 跟隨增益在校正與 common-A 合成後才套用；`Attenuation 0` 只關閉音色補償，仍保留跟隨，`State 0` 才 bit-transparent 地旁路兩者。
- 自動模式冷啟動沒有有效快照時保持靜音；成功讀取過一次後，短暫失敗會保留最後有效跟隨增益。端點靜音是精確零增益，恢復與變更使用所有聲道共用的 10 ms ramp。
- 只改變端點 scalar／mute 不會重新擬合響度輪廓；只有真正影響音色校正的 dB 變更才進入 bank warmup／crossfade 流程。
- VST MIDI 設定由每列的 `MidiConfig` 保存；支援 CC、Note、Pitch Bend，VST3 使用 ParamID 且只提供可見、非唯讀、可自動化參數，VST2 使用 index+name 防護。已有 mapping 的目前列進入 learn 前會以 durable temporary-audio journal 暫時移除自身 `MidiConfig`，對話框釋放 WinMM handle 後才還原，還原失敗或保留外部修改時不套用新 mapping。WinMM callback 與 VST audio callback 必須維持固定容量、有界工作、零 I/O／零阻塞／零動態配置。
- `Convolution:` 是不可改名的設定檔指令；「IR 卷積」只是較短的 UI 名稱。相對路徑以目前設定檔目錄為基準，IR 必須與裝置取樣率完全相同，上限為 1,048,576 frames／8,388,608 samples；每聲道的 HybridConv bank 另限制為 4,096 partitions，超限時保持乾聲。
- IR 的檔案讀取、驗證、反交錯與 HybridConv／FFTW bank 建立只能在非即時路徑進行。callback 遇到實際 frame count 與 bank 不符時只發布 lock-free 請求並輸出乾聲；背景 worker 建好吻合 bank 後，以 10 ms dry-to-wet 淡入接手。
- Editor 的「重建相符 FIR」是使用者明確觸發的同步、正規化 minimum-phase 幅度重建，不是一般 resampler，也不保留原始 phase、delay、spatial 或 HRIR 資訊。

## 最近驗證證據

- Release installer 已完整建置；本機產物為 `Setup\EqualizerAPO-x64-3.0.7.exe`，SHA-256 `43f6990582bb6c2fa7ed85b08c03d7083e81adc72807158952481d2e26093c65`。產物不提交 Git，也不代表已發布 release。
- 原生 transition／runtime loudness 測試通過，涵蓋三種曲線、mute、冷啟動與恢復、Full／Fast、立體聲、in-place／out-of-place 及接近中性輸出。Full／Fast 接近中性誤差分別為約 −0.000541／−0.000543 dB；三組轉換安全音訊的最大絕對輸出不超過約 1.000000009。
- 原生 IR／GraphicEQ 動態 block 測試通過，涵蓋 out-of-place／in-place、初始乾聲、背景交接、最終 wet 增益、FFTW planner 被其他執行緒持有，以及 4,096 partitions 成功／4,097 立即拒絕的邊界；受阻時首次 callback 為 0.002 ms，超限拒絕為 0.000 ms。舊 `Dual`／`Tripple` 包裝器與 `getProcTime` 的初始化失敗回報、完整回滾及重複 close 也已由原生 self-test 覆蓋。
- Python 共 213 項：212 通過；`test_built_host_cold_starts_and_hands_off` 因目前環境無法建立 global mapping（Win32 error 5）跳過 1 項，不能交接成「全部通過」。
- 這台機器的 Release microbenchmark（48 kHz、2 聲道、主要 batch 256）：Full 初始化 5.914 ms、更新 5.965 ms、處理 89.190 ns/sample；Fast 為 0.114 ms、0.096 ms、48.632 ns/sample；Full scalar fallback 151.588 ns/sample。額外 Full block／scalar 結果為 batch 16：61.527／165.439、batch 64：71.932／154.596、batch 1024：94.683／156.718 ns/sample。這是相對回歸資料，不能直接換算成真實裝置 CPU 百分比。
- UI regression 腳本的 90 張 theme／DPI 矩陣已成功產生；英文與簡中 dense 100% 淺色，以及繁中 dense 100% 淺色、200% 深色和 restored-tools 代表畫面已人工檢查，未見標題截斷、控制項重疊或錯誤狀態。未自動捲到的公式版跟隨控制、IR／VST 列、MIDI 對話框與校準視窗仍以編譯、翻譯及靜態 UI 契約覆蓋，不能宣稱已逐張完整視覺驗證。

## 仍存在的限制

- 尚無 VB-Audio Matrix 真實路由的長時間聆聽、dropout、端點切換與休眠喚醒驗證；首次使用應以安全音量確認 `Binding` 與曲線。
- 尚無實體 MIDI 控制器的 CC／Note／Pitch Bend、拔插重連與同名裝置驗證；目前證據是 codec、WinMM broker、VST2／VST3 佇列及目前列暫時釋放／復原契約的自動化與原生測試。其他 APO 列或其他程式仍可能占用只允許單一 client 的 MIDI driver。
- 本次新增與縮短的繁中介面字串已完成；德文、法文與簡中字串仍有未完成項目，Qt 會回退顯示英文，因此這些語系目前可能中英混排。
- 一般會自行套用 Windows 或硬體衰減的路徑若再啟用 `VolumeFollow`，會形成雙重衰減；不確定時維持 `Off`。
- Windows 若拒絕解除 endpoint notification callback，callback 本身可安全存活且不再指向 controller，但其 COM 註冊資源可能延後到端點釋放才回收。
- 合法但很長的 IR 在手動重建時仍可能暫時凍結 Editor；持續變動的 callback frame count 會安全直通並重建 bank，但不保存跨尺寸的卷積尾音。每個有效 IR filter 目前另有一個每 10 ms 輪詢的低頻背景 worker。
- factory 已移除固定 `MAX_PATH` 緩衝區造成的靜默截斷，但 Windows 設定、libsndfile 與實際安裝環境能否完整支援超過 260 字元的路徑，尚未以端到端案例證明。
- Fast 曲線品質未達一般模式門檻；行程外 VST host 仍是實驗功能，不是安全沙箱。
- 安裝程式目前未簽章；除非使用者明確要求 release，不得建立或推送 `v*` tag。

## 接手建議

1. 先用 `agent-status.ps1 -Fetch` 確認 repository、remote、branch、ahead/behind、dirty files 與 worktree。
2. 修改跟隨語意前完整閱讀 ADR-0002，並保留 `AGENTS.md` 指定的 parser、即時 callback、失敗恢復與 UI 測試矩陣。
3. DSP／parser 修改先跑 Python 契約，再跑 Release installer、runtime loudness／IR native self-test 與 `--loudness-performance`；UI 變更另跑並人工檢查 regression snapshots。
4. README 只描述目前行為；版本歷史只更新 `CHANGELOG.md`，不要把 release log 複製回 README。
