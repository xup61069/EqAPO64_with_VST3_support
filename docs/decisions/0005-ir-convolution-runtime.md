# ADR-0005：IR 卷積的動態 block 與安全重建

## 狀態

Accepted

## 日期

2026-09-05

## 背景

Equalizer APO 的卷積核心以固定 block size 初始化 HybridConv；但 Windows 音訊引擎在鎖定階段提供的最大 frame count，不一定等於之後每次 [`APOProcess`](https://learn.microsoft.com/en-us/windows/win32/api/audioenginebaseapo/nf-audioenginebaseapo-iaudioprocessingobjectrt-apoprocess) 實際送入的 frame count。兩者不同時，舊實作只能持續旁路；直接在第一次 callback 重新讀檔並建立 FFTW／HybridConv 狀態，則會把配置、鎖與不可預測時間帶進即時音訊執行緒。

IR 檔本身也可能有錯誤中繼資料、不完整讀取、非有限樣本、過大配置或不同取樣率。Editor 過去的自動取樣率轉換其實是從幅度響應重建 minimum-phase FIR，並不保留原始延遲與相位，因此不能把它描述成一般 resampler。

## 決策

1. `Convolution:` 保持為設定檔的穩定指令；Configuration Editor 的短名稱改為「IR 卷積」。相對路徑只相對目前設定檔所在目錄解析。
2. `initialize()` 在非即時路徑完整讀取、驗證並反交錯 IR，將結果放進 `ConvolutionFilter` 基底類別擁有的唯讀 cache。檔案必須和裝置取樣率相同，且不得超過 1,048,576 frames 或 8,388,608 samples；單一 HybridConv bank 每聲道最多 4,096 個 partition，避免長 IR 搭配極小 block 時產生數百萬次小型配置。partial read、關檔錯誤、NaN／Inf、空內容、超過 partition 上限與配置失敗一律 fail safe。
3. 初始化先為 `maxFrameCount` 建立固定尺寸 bank。若 callback 的實際尺寸不同但仍不超過上限，callback 只用 lock-free atomic 發布「尺寸＋generation」請求、輸出乾聲並立即返回；不得讀檔、配置、鎖定、記錄、建立執行緒或呼叫 FFTW planner。
4. 每個已成功載入 IR 的 filter 由非即時 worker 以 10 ms 間隔檢查請求，從基底類別 cache 建立所需尺寸的 bank。單槽 `pending` 由 worker 以 CAS 發布，callback 只在尺寸吻合且 `retired` 槽可用時接手；被取代的 bank 交回單槽 `retired`，只由 worker 或已停止 worker 的 cleanup 回收。worker 不呼叫 virtual IR 產生器，也不讀取 GraphicEQ 的 derived state。
5. 新 bank 接手時，以初始化時配置、每聲道獨立的 scratch 保存乾聲，做 10 ms dry-to-wet 淡入；in-place 與 out-of-place 必須相同。scratch 無法完整配置時，不啟用需要動態交接的路徑，避免無淡入跳變。
6. `GraphicEQFilter` 在 `initialize()` 的 virtual 準備階段先產生一份 minimum-phase impulse cache，之後和檔案 IR 共用相同 bank worker 與 handoff。所有產品內 FFTW plan 建立／銷毀共用同一把非即時 planner mutex。
7. Editor 只在使用者按下「重建相符 FIR」後，從來源的幅度響應同步建立正規化 minimum-phase FIR。輸出先寫入同目錄唯一暫存檔並完整關閉，再以 replace＋write-through 取代 path-hash 命名的產物；不得覆寫來源檔，也不得在輸入文字時自動執行。

## 後果與限制

- 固定但小於初始化上限的 callback 尺寸，會先短暫輸出乾聲，背景 bank 完成後再淡入卷積；音訊 callback 不等待 FFTW planner。
- 若 host 持續變換 callback 尺寸，每次不連續都會讓舊卷積歷史失效並重新請求 bank。系統會保持有界且安全，但不能保證跨不同 block size 保存卷積尾音或無縫 wet 輸出。
- 當 `ceil(IR frames / block frames)` 超過 4,096 時，該尺寸的 bank 不會建立，音訊維持乾聲；使用者需縮短 IR 或使用較大的 block size。
- 每個已載入的卷積 filter 有一個低頻率輪詢 worker；這以少量背景 wakeup 換取 callback 零系統呼叫。若日後改成共享 worker，仍須保留單一擁有者回收與 filter 解構前停止的生命週期。
- 手動「重建相符 FIR」仍同步執行；很長但合法的 IR 可能暫時阻塞 Editor。它不保留來源 phase、delay、spatial 或 HRIR 資訊，需要這些特性時必須使用原生裝置取樣率輸出。
- 原生回歸必須以 `maxFrameCount = 1024`、實際 `frameCount = 480` 覆蓋檔案 IR／GraphicEQ、in-place／out-of-place、dry-to-wet、最終增益，以及 FFTW planner 被其他執行緒持有時 callback 仍快速返回。
