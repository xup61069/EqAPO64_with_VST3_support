# Equalizer APO 響度校正（非官方 fork）

[English](README.md)

[![建置](https://github.com/xup61069/loudness-correction-apo/actions/workflows/build.yml/badge.svg)](https://github.com/xup61069/loudness-correction-apo/actions/workflows/build.yml)
[![最新版本](https://img.shields.io/github/v/release/xup61069/loudness-correction-apo)](https://github.com/xup61069/loudness-correction-apo/releases/latest)

本儲存庫是直接 fork 自 [Mixomo/EqAPO64_with_VST3_support](https://github.com/Mixomo/EqAPO64_with_VST3_support) 的 Windows x64 專案，沿用其系統層級雙精度音訊管線與 x64 VST2／VST3 音訊效果流程，並新增或維護公式響度校正、校準工具及繁體中文介面。

原始碼關係：[Equalizer APO](https://sourceforge.net/projects/equalizerapo/) → [TheFireKahuna/equalizerAPO64](https://github.com/TheFireKahuna/equalizerAPO64) → [Mixomo/EqAPO64_with_VST3_support](https://github.com/Mixomo/EqAPO64_with_VST3_support) → 本儲存庫。

從 v3.0.2 起，本專案的專屬提交直接重建在 Mixomo 的 `main` 歷史上，不再把後來的專案檔案複製到無關的 Git 根節點；因此 GitHub 顯示的 fork 關係與 Git 提交祖先會指向同一個來源。

上面連結中的來源儲存庫名稱只用於歸屬說明，不是本專案名稱。本儲存庫不是 Equalizer APO 上游專案，也不是上游官方建置。本功能僅稱為「響度校正」，不主張符合任何標準、取得認證、受到背書、具有從屬關係或獲得核准。

> 本安裝程式會原地取代既有 Equalizer APO，沿用相同的預設安裝目錄與登錄位置，不能和上游版本並存。安裝、升級或降級前，請先備份 `config` 與自行安裝的外掛。

## 系統需求

- x64 硬體上的 Windows 10 1809 版以上或 Windows 11；本專案未發布 x86 或 ARM64 安裝程式。最低版本依隨附的 [Qt 6.10 Windows 支援條件](https://doc.qt.io/qt-6.10/supported-platforms.html)而定。
- 可在 Windows 音訊裝置上安裝 Audio Processing Object（APO）的系統管理員權限。
- Equalizer APO 能夠啟用的播放或擷取端點。
- Microsoft Visual C++ 2015–2022 x64 執行階段；缺少時，安裝程式會提供微軟下載選項。
- 選用：用於聲學校準的聲壓計，以及用於音訊效果載入的 x64 VST 外掛。

## 下載與驗證

> **請勿使用 v3.0.0。** 該版本已由 v3.0.2 及後續版本取代；請一律安裝 [GitHub 最新版本](https://github.com/xup61069/loudness-correction-apo/releases/latest)。

請從同一個 Release 下載 x64 安裝程式及其對應的 `.sha256` 檔案，然後在 PowerShell 執行：

```powershell
Get-FileHash .\EqualizerAPO-x64-*.exe -Algorithm SHA256
Get-Content .\EqualizerAPO-x64-*.exe.sha256
```

兩個 64 字元的 SHA-256 值必須完全一致。目前安裝程式、隨附的執行檔與 DLL、Release tag 及檢查碼檔都沒有簽章。雜湊相符只能檢查檔案是否損壞，或是否與該 GitHub Release 提供的檔案一致，不能獨立證明發行者身分。

## 安裝或升級

1. 若 `C:\Program Files\EqualizerAPO\config` 與 `C:\Program Files\EqualizerAPO\VSTPlugins` 中有需要保留的檔案，請先備份。
2. 關閉音訊工具，並以系統管理員身分執行安裝程式。安裝程式可能關閉本專案正在執行的工具，也會嘗試建立 Windows 還原點；Windows 拒絕建立還原點時，安裝仍可繼續。
3. 在「裝置選擇器」中，只替需要處理的播放或擷取端點啟用 Equalizer APO。
4. 允許安裝程式重新啟動 Windows 音訊服務；音訊可能短暫中斷。若安裝程式或裝置選擇器要求，請重新啟動 Windows。
5. 更新檔案與 APO 註冊期間，請勿強制關閉安裝程式或關閉電腦。

一般升級會保留 `config` 與非空的 `VSTPlugins` 目錄，但仍建議另外備份。自動更新檢查預設關閉。

安裝程式在取代程式檔案時，會把持久復原紀錄保存在應用程式目錄之外。如果舊程式樹已備份後安裝遭到中斷，請重新執行相同或較新的安裝程式；它會先還原已保存的程式樹，再開始新的交易。安裝尚未完成時，請勿手動刪除復原資料。

## 快速開始

1. 開啟 **Equalizer APO Configuration Editor**，選取實際要使用的播放端點。
2. 新增 **高階過濾器 → 響度校正**。
3. 選擇「**單一端點**」可跟隨目前執行 APO 的實際播放端點；若像 VB-Audio Matrix 一樣，需要所有響度校正實例共用 Windows 預設 Multimedia 播放音量，則選擇「**全域（Windows 預設）**」。
4. 要自動追蹤時關閉「手動音量」；若 Windows 無法代表真實聆聽音量，則啟用手動音量。
5. 設定參考響度與補償強度；只有在備有合適聲壓計時才進行校準。
6. 確認儲存的命令已啟用，且包含 `State 1`。

此濾波器用來補償聆聽音量變化時的感知音色平衡；它不是曲目響度正規化、空間校正、聽力測驗、自動麥克風量測或限幅器。

## 響度校正行為

引擎會計算 20 Hz 至 12.5 kHz 的 29 點公式參數表，再把可表示的頻率擬合成最多 29 個 Q=3 的峰值濾波器。取樣率較低時，中心頻率超過 Nyquist 頻率 90% 的頻帶會被略過。固定的 28 階 Linkwitz-Riley 分頻會建立共用的未校正 `A = L + H` 域，擬合校正只作用於高通貢獻。原生極端案例測試中，A 域安定後的 1–19 Hz 幅度會維持在單位增益的 0.01 dB 內，不承受校正分支的輸出餘裕衰減。這個 0.01 dB 是穩態幅度保證，不涵蓋冷啟動的 raw→A 交接；交接可能改變相位，改以一個取樣的輸出跳幅為限制。初始化時，濾波器會先讓原始輸入直通至少 1.0 秒以累積分頻歷史；之後每個聲道只有在 raw 與 A 的取樣交會，而且交接跳幅不大於兩者自然一取樣變化中的較大值時，才切入 A。系統刻意不設逾時；若安全交接點沒有出現，受影響聲道會維持未校正，校正也不會啟用。所有聲道進入 A 後，校正濾波器會靜音預熱 250 ms，再以 100 ms 淡入。後續音量造成係數變更時，會沿用即時分頻歷史，並在 A 域以 100 ms 於預先配置的兩組濾波器間交叉淡化。

目前估計響度的計算方式為：

```text
clamp(ReferenceLevel + Volume - ReferenceOffset, 0, 100)
```

`Volume` 是所選 Windows 播放端點目前的 dB 音量，或明確指定的手動值。擬合響應以 `ReferenceLevel` 為相對基準；因此 `Volume` 與 `ReferenceOffset` 都是零時，在參考輪廓上不會加入相對校正。

### 參數

| 參數 | 範圍 | 意義 |
|---|---:|---|
| `Schema` | `1` | 識別有版本的參數格式。 |
| `Model` | `FormulaLoudnessV1` | 識別此公式輪廓，不表示符合任何標準。 |
| `Binding` | `Single` 或 `All` | `Single` 跟隨這個 APO 實例的實際播放端點；`All` 讓所有實例跟隨目前 Windows 預設 Multimedia 播放端點。有手動 `Volume` 時不生效。 |
| `State` | `0` 或 `1` | 內部略過或啟用狀態；新濾波器使用 `1`。 |
| `ReferenceLevel` | 1–100 phon | 選擇中性的參考輪廓；預設為 80 phon。 |
| `ReferenceOffset` | −100 至 +100 dB | 從目前估計響度中扣除；因此正值會要求較強的低音量補償。 |
| `Attenuation` | 0–1 | 補償強度；`0` 為平直，`1` 套用完整擬合校正。 |
| `Volume` | −100 至 0 dB | 選用的手動音量；有此欄位即採手動模式，省略時則自動追蹤端點。 |

### 頻率與響度範圍

參數表的最高頻率是 12.5 kHz，資料證據最完整的範圍為：

- 20 Hz 至 4 kHz：20–90 phon；
- 5 kHz 至 12.5 kHz：20–80 phon。

介面允許設定 1–100 phon 的參考響度，執行期則會把計算出的目前響度限制在 0–100 phon。低於 20 phon 或高於各頻率上限的值只作近似調整，不代表經過驗證或符合性聲明；參數表也不表示 12.5 kHz 以上的頻率。

### 自動與手動音量

自動模式有兩種明確綁定：

- **單一端點**（`Binding Single`）跟隨該 APO 實例實際執行所在的播放端點，絕不退回 Windows 預設裝置或改追蹤其他裝置。一般實體輸出，或每個端點必須跟隨各自音量時使用此模式。
- **全域（Windows 預設）**（`Binding All`）讓所有響度校正實例共同跟隨目前 Windows 預設 `eRender`／`eMultimedia` 端點的主音量。VB-Audio Matrix 或類似路由需要多個 APO 實例共用同一音量控制時使用。控制器至少每兩秒確認一次預設端點；偵測到變更後，會先丟棄舊端點再綁定新端點。重新綁定失敗時會依下述 10 ms 淡化失效安全地停止校正，不會退回舊端點。

需要的端點消失、切換失敗或無法讀取音量時，自動校正會失效安全地暫停。冷啟動交接前維持原始輸入直通；已進入 A 域時，則只把校正殘差在 10 ms 內淡到未校正的 `A = L + H` 路徑。指定來源恢復後，目標濾波器會先靜音預熱 250 ms，再以 100 ms 淡回校正；若冷啟動交接仍未完成，則在可安全交接前維持未校正。`Binding Single` 在 Windows 無法確認 APO 位於播放流程時維持略過；`Binding All` 則保留原始 Mixomo 行為，直接讀取預設 `eRender`／`eMultimedia` 端點，不依賴目前 APO 的端點中繼資料。

自動追蹤只能看見 Windows 端點音量，無法偵測應用程式自己的音量滑桿、類比擴大機或喇叭旋鈕，也無法得知 Windows 端點之後的增益變更。這類系統應使用手動模式，並在真實衰減改變時同步更新手動值。

`Binding Single` 用於擷取／輸入或無法辨識的端點時，必須明確指定手動 `Volume`。`Binding All` 不追蹤該 APO 端點，而是一律沿用 Windows 預設 Multimedia 播放端點的音量。內建校準噪音仍只適用於播放端點。有 `Volume` 時會覆蓋自動追蹤，`Binding` 在執行期不生效。

### 校準

校準會估算追蹤音量為 0 dB 時，在聆聽位置的 1 kHz 聆聽級別；它不會透過麥克風自動量測。

1. 先把系統或硬體音量調到安全範圍。粉紅噪音可能很大聲；若感到不適，立即停止。
2. 將聲壓計放在聆聽位置，設為**慢速反應**與 **Z 加權（平直）**。
3. 只量測一支喇叭；校準視窗開啟時，響度校正會自動暫停。
4. 將播放測試音的應用程式音量設為最大，播放內建粉紅噪音，並手動輸入量到的 dB SPL。
5. 儲存校準後，繼續使用相同的 Windows 音量或手動音量方式。若使用外接硬體旋鈕，請保持校準時的位置，或同步更新手動值。

只有當所選端點可讀取，而且同時是 Windows 預設的 **Console** 播放端點時，內建播放器才可使用；全域綁定還要求它同時是預設 **Multimedia** 播放端點，因為校準的是該音量來源。否則會阻止播放，避免校準到錯誤喇叭。開始播放前會再次檢查目的地；解碼期間或播放期間若必要的預設端點改變，噪音不會開始或會立即停止。

### 輸出餘裕

濾波器會在密集頻率網格上估算擬合校正分支的穩態響應峰值、細化局部極大值，並依該峰值再加 1 dB 餘裕衰減校正分支；接著再掃描完整的「A 域＋校正」傳遞，必要時才進一步降低校正分支。這個輸出餘裕增益只作用於已校正的高通貢獻；共用的未校正 `A = L + H` 路徑不會被整體衰減，因此 20 Hz 以下的穩態幅度不會跟著校正分支被往下拉。這可降低削波風險，但不是取樣峰值或真峰值限幅器；瞬態、多音訊號、後續外掛及其他增益級仍可能削波，必要時請另外保留輸出餘裕。

## 設定格式與升級

自動音量設定格式如下：

```text
LoudnessCorrection: Schema 1 Model FormulaLoudnessV1 Binding Single State 1 ReferenceLevel 80 ReferenceOffset 0 Attenuation 1.0
```

加入 `Volume -38.0` 即改用手動模式：

```text
LoudnessCorrection: Schema 1 Model FormulaLoudnessV1 Binding Single State 1 ReferenceLevel 80 ReferenceOffset 0 Attenuation 1.0 Volume -38.0
```

VB-Audio Matrix 這類共用音量路由請使用 `Binding All`，並省略 `Volume`：

```text
LoudnessCorrection: Schema 1 Model FormulaLoudnessV1 Binding All State 1 ReferenceLevel 80 ReferenceOffset 0 Attenuation 1.0
```

### 原始 Mixomo 棚架輪廓項目

原始 Mixomo 濾波器以相同欄位名稱表示另一種棚架濾波模型；而且部分有效的舊棚架數值會與早期公式版本寫入的數值重疊。因此，所有沒有格式標記的項目都會保持原始文字並略過處理，直到在 Configuration Editor 中選定其意義；數值同時符合兩種模型時，介面會並列兩個選項。按下**轉換原始棚架輪廓**後，系統會把 `舊 ReferenceLevel - 舊 ReferenceOffset` 對應至新的 `ReferenceOffset`，以保留原本的 Windows 中性音量點；同時把 `Attenuation` 對應為校正強度、保留既有手動音量、將低於 −100 dB 的音量限制為 −100 dB，並選用 `Binding All` 保留 Mixomo 共用預設音量的行為，最後才啟用帶有格式標記的公式輪廓。兩種響應模型並不相同，轉換後仍須檢查並重新校準。

### 先前發布的公式項目

v3.0.0 或 v3.0.1 寫入、沒有格式標記的公式項目同樣會略過處理，不會由程式猜測。選擇**保留既有公式數值**後，Configuration Editor 會加入明確的 `Schema 1 Model FormulaLoudnessV1` 標記與 `Binding Single`，同時保留其他數值與啟用狀態。若同一組數值也符合原始棚架模型，旁邊會同時顯示棚架轉換選項。這個標記可避免未來或其他模型被靜默誤解；在 `Binding` 出現前寫入、但已有 Schema 1 標記的項目也會按 `Single` 載入。

### 從 v2.0.0 轉換

有效的 v2.0.0 項目會保持原始文字不變並維持略過，直到按下**轉換並啟用公式輪廓**。轉換會：

- 將 `NeutralVolumeDb` 對應到 `ReferenceOffset`；
- 將 `Strength` 對應到 `Attenuation`；
- 有 `ManualVolumeDb` 時予以保留；
- 把低於 −100 dB 的音量截為 −100 dB；以及
- 以自動輸出餘裕取代已停用的舊 headroom 模式。

轉換後會以 `State 1` 啟用。Configuration Editor 的「即時模式」啟用時通常會立刻儲存，因此轉換前請先備份或檢查設定，完成後再依目前系統重新校準。

### 曾由 v3.0.0 開啟的項目

v3.0.0 可能已把舊項目重寫為沒有標記的公式格式 `State 0 ReferenceLevel ...` 草稿。請先選擇**保留既有公式數值**，讓編輯器加入模型標記；此步驟會刻意保留 `State 0`。備份設定後，可關閉編輯器，只把該已標記項目在 `config\config.txt` 裡的 `State 0` 改成 `State 1`；也可以刪除草稿後重新新增「響度校正」。啟用前請檢查音量模式並重新校準。

## VST 外掛載入

編輯器可從**外掛 → VST 外掛**載入 x64 VST2（`.dll`）與 VST3（`.vst3`）音訊效果；本專案不包含任何商業或第三方外掛。

- VST3 僅支援 x64 音訊效果模組，不支援樂器或只使用 MIDI／事件的外掛。若套件公開多個音訊效果類別，目前會選取第一個可辨識類別。
- Windows 音訊服務必須能讀取外掛及其使用的所有資源。把外掛放到 `C:\Program Files\EqualizerAPO\VSTPlugins` 通常較容易處理權限。
- 部分外掛依賴桌面工作階段、版權保護、不支援的匯流排配置，或不適合系統音訊服務的 API，因此不保證全部相容。
- 外掛在 Windows 音訊處理路徑內執行，沒有沙箱隔離；只使用可信且穩定的外掛，以安全音量測試，並保留可復原的設定備份。

## 更新

安裝時選取自動更新檢查，會建立在登入時執行的排程工作，最多每 24 小時連線一次本儲存庫的 GitHub Releases API。它不會自動下載或安裝更新，只會顯示通知，並可開啟 HTTPS Release 頁面。

日後再次執行安裝程式時，若未勾選此選項，排程工作會被移除。也可以從「開始」功能表的**檢查更新**捷徑手動檢查；各版本變更記錄於 [CHANGELOG.md](CHANGELOG.md)。

## 解除安裝

請使用 Windows 的**已安裝的應用程式**，或「開始」功能表中的**解除安裝**捷徑。解除安裝程式會嘗試移除更新排程、端點 APO 註冊、應用程式檔案及捷徑；完成後可能需要重新啟動。

除非勾選**移除設定檔與登錄檔備份**，否則設定檔與登錄備份會保留；非空的 `VSTPlugins` 目錄也會留在原處。若其中有重要內容，解除安裝前仍應自行備份。

## 疑難排解

| 現象 | 檢查方式 |
|---|---|
| 完全沒有音訊處理效果 | 確認已在裝置選擇器啟用該端點、命令沒有被註解，而且包含 `State 1`。變更裝置註冊後，請重新啟動 Windows 音訊服務或電腦。 |
| 響度校正保持平直 | 確認 `Attenuation` 大於零，而且目前估計響度與參考輪廓不同。 |
| 無法使用自動音量 | 每個裝置分開追蹤時，在可讀且可辨識的播放端點使用 `Binding Single`；Matrix 類共用控制則使用 `Binding All`，並把音量來源設為 Windows 預設 Multimedia 播放端點。全域模式不需要目前 APO 的端點中繼資料。 |
| 跟到錯誤端點音量 | 要跟 APO 實際端點時使用 `Binding Single`；只有刻意讓所有實例共用 Windows 預設 Multimedia 音量時才使用 `Binding All`。 |
| 校準按鈕無法播放 | 將所選端點設為 Windows 預設 Console 播放裝置並確認音量可讀；使用 `Binding All` 時，它也必須是預設 Multimedia 裝置。之後重新開啟校準。 |
| 校準不會跟隨硬體旋鈕 | 改用手動音量，並在類比增益改變時更新數值；Windows 無法偵測該旋鈕。 |
| VST 外掛無法載入 | 確認是 x64 音訊效果外掛，且音訊服務帳戶能讀取外掛及其外部檔案。 |
| Windows 警告安裝程式不明 | 目前發布版本未簽章；只能從本儲存庫下載，並比對對應 SHA-256 檔。 |

## 從原始碼建置與測試

必要工具：

- Windows x64 與 PowerShell；
- Visual Studio 2022，並安裝 **Desktop development with C++** 與 Windows SDK；
- Git、Python 3、CMake，以及下載產生式相依項目所需的網路連線。

建置腳本會在 `third_party` 的忽略目錄中準備固定版次的 vcpkg baseline、Qt 6.10.1 與 NSIS 3.11。

```powershell
git clone https://github.com/xup61069/loudness-correction-apo.git
Set-Location .\loudness-correction-apo
python -m unittest discover -s .\tests -p "test_*.py" -v
.\scripts\build-installer-x64.ps1 -Configuration Release
.\scripts\test-runtime-loudness.ps1 -Configuration Release
git diff --check
```

安裝程式與檢查碼會輸出為 `Setup\EqualizerAPO-x64-<version>.exe` 與 `Setup\EqualizerAPO-x64-<version>.exe.sha256`。

主要原始碼位置：

- `filters/loudnessCorrection/`：公式參數表、響應擬合、端點追蹤及執行期 DSP；
- `Editor/guis/`：濾波器控制、舊設定轉換及校準介面；
- `Setup/` 與 `scripts/`：安裝程式、相依項目準備、檔案暫存與執行期檢查；
- `tests/`：公式、安全契約、翻譯、安裝、更新及發布流程測試；
- `third_party/`：受版本控制的第三方原始碼與產生式相依項目位置。

貢獻規則請見 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 安全性、權利聲明與授權

只有最新版本會收到安全性修正；若要通報弱點，請依 [SECURITY.md](SECURITY.md) 使用非公開方式。

為了讓 Configuration Editor 能儲存變更，安裝程式會授予 Windows 本機 Users 群組對共用 `config` 目錄的「完全控制」權限。因此在多人共用電腦上，任何本機標準使用者都能修改系統層級音訊設定；部署時應將此納入電腦的信任模型。

儲存庫擁有者已確認，所包含的響度輪廓資料與實作可以隨原始碼和二進位檔公開散布；詳見 [NOTICE.md](NOTICE.md)。本儲存庫沒有隨附該授權的公開證明文件。

程式碼採 GPL-3.0 授權，請見 [LICENSE](LICENSE)。受版本控制及建置時產生的相依項目各自保留原授權；重新散布自行建置的二進位檔前，請參閱 [third_party/README.md](third_party/README.md) 與各原始碼目錄中的授權檔案。
