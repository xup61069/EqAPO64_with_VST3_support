# Equalizer APO 64 — VST3 與響度校正

[English](README.md)

[![建置](https://github.com/xup61069/loudness-correction-apo/actions/workflows/build.yml/badge.svg)](https://github.com/xup61069/loudness-correction-apo/actions/workflows/build.yml)
[![最新版本](https://img.shields.io/github/v/release/xup61069/loudness-correction-apo)](https://github.com/xup61069/loudness-correction-apo/releases/latest)

這是以 [Equalizer APO](https://sourceforge.net/projects/equalizerapo/) 與既有 VST3 整合工作為基礎的獨立 Windows x64 分支；本專案加入 VST3 載入、資料驅動的響度校正濾波器、校準工具，以及繁體中文介面。

## 下載

請從 [GitHub 最新版本](https://github.com/xup61069/loudness-correction-apo/releases/latest)下載安裝程式與對應的 `.sha256` 檔，安裝前先核對雜湊值。

## 響度校正

濾波器採用 29 頻帶的資料驅動響度輪廓；它會隨聆聽音量調整頻率響應，並依實際的峰值濾波器組擬合目標曲線，同時保留餘裕以避免削波。

編輯器可設定：

- 1–100 phon 的參考級別；
- 參考級別偏移與校正強度；
- 自動追蹤 Windows 音訊端點音量，或改用手動音量；以及
- 用粉紅噪音量測聆聽位置的校準流程。

啟用時的設定格式如下：

```text
LoudnessCorrection: State 1 ReferenceLevel 80 ReferenceOffset 0 Attenuation 1.0
```

若要指定手動音量，附加 `Volume -38.0`；未指定時，濾波器會追蹤 Windows 音訊端點音量。

v3 已恢復這個設定格式。v2.0.0 的響度校正項目採用不同模型；在編輯器開啟時，會轉為安全且停用的草稿。請檢查音量模式、設定參考級別並完成校準後，再自行啟用。

## 權利聲明與授權

儲存庫擁有者已確認，所包含的響度輪廓資料與實作可以隨原始碼和二進位檔公開散布。本專案僅稱為「響度校正」，不主張符合任何標準、取得認證、受到背書、具有從屬關係或獲得核准；詳見 [NOTICE.md](NOTICE.md)。

程式碼採 GPL-3.0 授權，請見 [LICENSE](LICENSE)。安裝後的程式目錄也會包含 `NOTICE.md`。
