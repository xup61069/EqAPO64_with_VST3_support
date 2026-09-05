# ADR-0003：原版響度校正必須是獨立元件

## 狀態

Accepted

## 日期

2026-09-05

## 背景

Mixomo fork 的原始響度校正使用 75 Hz（Q 0.52）與 10 kHz（Q 0.9）兩段棚架，以及獨立的寬頻 preamp 公式。後續公式版則依 29 點輪廓、不同的濾波器 bank、端點綁定、手動音量、引擎模式與 `VolumeFollow` 運作。兩者雖曾使用相同欄位名稱，實際響應與狀態語意並不相同。

原始來源錨點為 [Mixomo 初始匯入 commit `3a0cc87`](https://github.com/Mixomo/EqAPO64_with_VST3_support/commit/3a0cc87e1dc73c71158d178729f30dbe679872a9)，其基礎可追溯到 [Equalizer APO commit `2368d26`](https://github.com/mirror/equalizerapo/commit/2368d2660dcf40179e4f75fea7dbf1dae3ca9a0d)。來源只用來驗證原始公式與歸屬；已知的未初始化值、阻塞 callback 與生命週期問題不予複製。

## 決策

建立完全獨立的 `LoudnessCorrectionOriginal:` 元件，格式固定為：

```text
Schema 1 Model MixomoShelfV1 State … ReferenceLevel … ReferenceOffset … Attenuation …
```

它擁有自己的 parser、factory、runtime class、GUI factory、GUI 與校準對話框。公式版 `LoudnessCorrection:` 不得加入「原版模式」，原版也不得讀取 `FormulaLoudnessV1`、Engine、Binding、手動 Volume 或 VolumeFollow。兩者只可共用與模型無關、已驗證安全的底層端點讀取工具。

原版固定追蹤 Windows 預設 `eRender`／`eMultimedia` 主音量。設定欄位必填且唯一；格式錯誤、重複、非有限值或越界時 fail closed。`State 0` 必須逐位元直通。端點失效時旁路原版校正，恢復時以預配置雙 bank 預熱並交叉淡化。

保留原始數學公式，但修正 `difference == 0` 時未初始化 preamp 的缺陷，使中性點為精確單位增益。10 kHz shelf 超出 Nyquist 時只略過不可表示的高棚架，不捨棄仍有效的低棚架與 preamp。

舊的無標記棚架列由使用者明確選擇：可「保留為原版元件」，原值直接進入 `MixomoShelfV1`；也可轉成公式版。程式不得依數值猜測。

## 後果

- 使用者能保留原始聲音，又不會讓公式版選項改變其語意。
- 兩套元件可以同檔存在，但會依列順序各自處理，文件應提醒通常不要疊加。
- 新功能若只屬於公式版，不得因便利而滲入原版格式；原版格式變更需要新的 schema/model 決策。
- 測試必須分別驗證 command routing、codec、DSP、失效安全、校準暫時旁路及 UI 名稱。
