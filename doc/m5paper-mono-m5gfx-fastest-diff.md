# PaperMono `fastest` と M5GFX `epd_fastest` の比較

比較基点: SolarOS `82c87470e6cad25626bbdfb36a23ad4632b3ccfa`、M5GFX `347a23274081a105cb9fc0fc3bbf0e15c78e79c3` の [Panel_SSD1677.inl](https://github.com/m5stack/M5GFX/blob/347a23274081a105cb9fc0fc3bbf0e15c78e79c3/src/lgfx/v1/panel/Panel_SSD1677.inl) と [Panel_SSD1677.hpp](https://github.com/m5stack/M5GFX/blob/347a23274081a105cb9fc0fc3bbf0e15c78e79c3/src/lgfx/v1/panel/Panel_SSD1677.hpp)。M5GFXはSolarOSの実行時依存ではない。SolarOSはU8g2描画と独自SSD1677ドライバを使い、以前のWriterDeck環境のM5GFX 0.2.28からLUTを移植した（`doc/m5paper-mono-writer-strategy.md`）。現行統合WorktreeのESP-IDFビルドは5.5.5で、依頼文の5.1.2とは異なる。

## 差分

| 項目 | M5GFX | SolarOS | 黒濃度への含意 |
| --- | --- | --- | --- |
| 通常の最速LUT | `lut_fastest_SSD1677`、110バイト | `ssd1677_reference_lut`、110バイト | **110/110バイト一致**。HOLD黒・HOLD白は全ゼロ。黒→白 `6A A0`、白→黒 `95 50`、2フレームの逆極性予備駆動を含むTP/RP、フレーム設定 `8F`×5、電圧 `17 46 A8 32 30` も一致。LUT差による改善余地はない。 |
| RAM/更新 | 新画像を0x24、表示済み画像を0x26へ、各全画面48,000バイト。0x32に波形105バイト、0x03/0x04/0x2Cに電圧。0x21=00、0x22=0C、0x20を1回。 | 同じ内容・順序。コールド時のみ0x22=CC。 | 温間の差分更新では同じ4遷移分類。固定黒はHOLDで再駆動しないため、更新を重ねた光学的薄まりは残り得る。 |
| 表示履歴なし | Mode-2の`lut_fast_SSD1677`で絶対値更新し、履歴を作る。 | `refresh=fastest`でも正規OTP fullを先に行い、shadow・baselineを確立する。 | 初回だけ異なる。既存の黒が後続入力で徐々に薄くなる現象の直接原因とは言えない。OTPを別方式へ置換するTrialは今回行わない。 |
| 履歴とMode-2 | 4階調のLSB/MSBと表示済み2面を管理。変更範囲外は旧画像で埋める。両RAMを毎回上書きし、Mode-2 faceは追跡するが書込先は入れ替えない。 | 単色の完了済みshadowと新snapshotを比較。両RAMを毎回上書きし、成功したBUSY後だけshadowを昇格。 | 単色の通常更新では同じ新旧画素になる。face追跡の有無は物理RAM内容の入替えを意味しない。 |
| tile/scalar | M5GFXの行単位転送。 | tileは8×8転置を32行バンドで送信。scalarは1行ずつ転置。いずれも全画面の0x24/0x26転送。 | Hostで3条件の全コマンドとデータの一致を確認。SPI転送時間は異なるが、波形・更新回数は同じ。 |
| BUSY/電源 | 0x20後2ms待ってからBUSYを監視。最速では最小更新時間を強制せず、電源を維持。 | 0x20後最低1 RTOS tickを待ってBUSYを監視。温間はreset/sleepなし。コールドのみ初期化・電源ON。 | 待機時間とコールド手順は異なるが、正常完了した温間波形の黒保持を改善する根拠はない。 |

LUTおよびM5GFXの更新処理は上記`.inl`の149–181、760–856、913–971行、初期化は`.hpp`の102–117行を参照。SolarOSは`src/drivers/epd_ssd1677_reference.inc`の10–45、86–199行、`src/drivers/epd_ssd1677.c`の278–333、346–436、490–580行を参照する。SolarOSの通常ビルドでは`SOLAR_OS_SSD1677_WAVEFORM_TRIAL=0`なのでdose/holdは選択されない。統合Worktreeの直近ビルドは`SOLAR_OS_SSD1677_SPI20_TRIAL=1`なのでfastestの要求クロックは20MHz。依頼文の約114ms、40MHzは過去の測定値であり、今回の実機速度として再測定していない。

## 検証と判定

公式の指定CommitからLUTを読み、SolarOSの配列を数値に展開して110バイトを比較したところ差は0。既存の`epd_ssd1677_test`に、固定黒を保つ追加・消去・広範囲の3条件について、温間tileとscalarの**全SPIコマンド・ペイロード**を比較するHostテストを追加しPASS。既存テストの転置全入力、差分範囲、完了後shadow、BUSY失敗時の非昇格もPASSした。

**Trialは実装・Flashしない。** 通常の最速波形と温間更新シーケンスには、黒保持を改善できると合理的に期待できる公式との差がない。scalarへ切り替えるだけでは画素の駆動を変えず、114ms級の速度を損なう可能性がある。固定黒のHOLDはLUT上無駆動であり、薄まりは物理的な保持特性、更新履歴または光学的条件が候補だが、ここから因果関係や恒久的劣化を断定しない。

次に進むなら、波形変更の前に、同一画面・同一更新数・同一SPIクロックでtile/scalarの光学差が本当にあるかを**少数回**の実機A/Bで確認する。差がなければ更新回数を減らす上位描画経路を優先する。今回の調査では実機更新もFirmware変更も行っていない。
