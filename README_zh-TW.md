# Equalizer APO 64 — VST3 與響度校正

[English](README.md)

[![建置](https://github.com/xup61069/loudness-correction-apo/actions/workflows/build.yml/badge.svg)](https://github.com/xup61069/loudness-correction-apo/actions/workflows/build.yml)
[![最新版本](https://img.shields.io/github/v/release/xup61069/loudness-correction-apo)](https://github.com/xup61069/loudness-correction-apo/releases/latest)

這是以 [Equalizer APO](https://sourceforge.net/projects/equalizerapo/) 與既有 VST3 整合工作為基礎的獨立 Windows x64 分支；本專案加入 VST3 載入、公式響度校正、校準工具，以及繁體中文介面。

## 下載

請從 [GitHub 最新版本](https://github.com/xup61069/loudness-correction-apo/releases/latest)下載安裝程式與對應的 `.sha256` 檔，安裝前先核對雜湊值。

## 響度校正

濾波器會計算 29 點公式參數表，再以 29 頻帶峰值濾波器組擬合目標響應。自動模式會追蹤 Equalizer APO 實際選取的播放端點；無法辨識或讀取該端點時，校正會安全略過，不會誤追蹤其他裝置。濾波器變更採 100 ms 交叉淡化；密集取樣與峰值細化後另保留 1 dB 餘裕，並以低頻與高頻的原生執行期案例驗證輸出峰值。

此參數資料在 4 kHz 以下以 20–90 phon、4 kHz 以上以 20–80 phon 的證據最完整；超出範圍的數值只作近似調整，不代表驗證或符合性聲明。

編輯器可設定：

- 1–100 phon 的參考級別；
- 參考級別偏移與校正強度；
- 自動追蹤目前選取的 Windows 播放端點音量，或改用手動音量；以及
- 用粉紅噪音量測聆聽位置的校準流程。

啟用時的設定格式如下：

```text
LoudnessCorrection: State 1 ReferenceLevel 80 ReferenceOffset 0 Attenuation 1.0
```

若要指定手動音量，附加 `Volume -38.0`；未指定時，濾波器會追蹤目前選取的播放端點。輸入裝置與無法使用的端點必須改用手動音量。

v3 採用這個設定格式。v2.0.0 的響度校正項目使用不同模型；編輯器會保留原始設定文字並保持略過，直到您按下明確的轉換按鈕。轉換會對應舊版的中性音量與強度並啟用公式輪廓，之後仍應依您的系統檢查與校準。

內建粉紅噪音播放器只能送到 Windows Wave API 使用的預設播放裝置。若它不是編輯器目前選取的端點，程式會阻止播放，避免在錯誤的揚聲器上完成校準；播放期間若預設裝置改變，也會立即停止測試噪音。安裝時的自動更新檢查預設不啟用，由使用者自行勾選。

## 權利聲明與授權

儲存庫擁有者已確認，所包含的響度輪廓資料與實作可以隨原始碼和二進位檔公開散布。本專案僅稱為「響度校正」，不主張符合任何標準、取得認證、受到背書、具有從屬關係或獲得核准；詳見 [NOTICE.md](NOTICE.md)。

程式碼採 GPL-3.0 授權，請見 [LICENSE](LICENSE)。安裝後的程式目錄也會包含 `NOTICE.md`。目前發布的安裝程式尚未數位簽章，Windows 可能顯示未知發行者或 SmartScreen 警告；安裝前請核對一併提供的 SHA-256 檔。
