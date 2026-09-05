# ADR-0001：響度校正引擎模式與即時處理策略

## 狀態

Accepted

## 日期

2026-09-05

## 背景

Full 引擎會依公式輪廓建立完整濾波器組，曲線品質較高，但原本在每個 sample 上逐一走訪所有 crossover 與 correction section，callback 成本偏高。另有一個最多使用兩段的 Fast 原型：固定使用 120 Hz 低架，取樣率可可靠表示 12.5 kHz 時才加入 Q=2 高頻峰值。它的 CPU 成本較低，但在極低聆聽級別的密集頻率測試中，與 Full 可相差約 10–15 dB。

效能改善不能破壞既有的 raw → common-A 安全交接、預熱、係數交叉淡化、失效時 bypass，以及 audio callback 不配置、不等待、不做 I/O 的即時契約。

## 決策

1. **Full 維持預設引擎。** 未寫入 `Engine` 或明確使用 `Engine Full` 都採 Full；既有設定因此不需要重寫。
2. **Fast 只作明確選用的實驗模式。** 只有 `Engine Fast` 才會啟用，介面必須顯示近似誤差警告；不得描述成 Full 的等價或一般建議替代品。
3. **Full 的穩態 callback 採 section-major block processing。** `initialize()` 依 `maxFrameCount` 預先配置 scratch；每個 section 一次處理完整 block，避免逐 sample 重複載入係數與物件狀態。
4. **轉換狀態保留 sample-major 路徑。** handoff、warmup、coefficient crossfade 與 bypass fade 可能在 block 中改變狀態，因此不套用穩態捷徑。
5. **非即時響應掃描聚合複數響應。** 每個頻率共用 `z`／`z²`，先相乘完整 cascade，再做一次 magnitude／dB 轉換，降低初始化與音量更新成本。
6. **兩條處理路徑必須維持逐樣本等價測試。** Full／Fast、in-place／out-of-place、不同 block size 與係數轉換都要覆蓋。

## 證據

Windows x64 Release、48 kHz、2 聲道、約 1,048,576 frames、7 次取中位數的量測結果：

| 項目 | 最佳化前 | 最佳化後 | 改善 |
|---|---:|---:|---:|
| Full initialize | 16.47–16.97 ms | 5.96–6.27 ms | 約 62%–65% |
| Full volume update | 16.00–19.42 ms | 5.57–6.07 ms | 約 62%–71% |
| Full callback，256 frames | 159.21–181.92 ns/sample | 87.16–89.73 ns/sample | 約 44%–52% |

原生 block／scalar 回歸的最大逐樣本誤差為 0；聚合響應與舊版逐 band dB oracle 的最大差為 `2.84e-14 dB`。量測數字只代表該環境的 microbenchmark，不等於真實裝置的整機 CPU 百分比或無掉音保證。

## 後果與護欄

- 修改 callback 時，先確認目前狀態是否真的 settled；不得把轉換路徑誤導入 block 快速路徑。
- scratch 容量必須在 `initialize()` 決定，callback 不得 `resize`、配置或釋放。
- Full 的前 28 個可用頻帶使用 Q=2.2 peak；12.5 kHz 頻帶可用時使用 Q=0.9 high shelf。註解、README 與測試必須和這組常數同步。
- Fast 若要升為一般模式，必須先加入直接由 C++ 產生的完整 response-error matrix 與明確品質門檻；只有效能數字不足以改變此決策。
- 可重現效能命令與驗證階梯以根目錄 [AGENTS.md](../../AGENTS.md) 為準。
