# PaperMono SSD1677 Dirty Rectangle / HOLD — Host MVP

実施日: 2026-10-15。判定: **CONDITIONAL**。RAM転送とController RAMモデルの整合性はHostで成立した。物理的な駆動範囲、黒濃度、BUSY時間は未確認である。実機Flash・EPD駆動試験は行っていない。

## 更新経路と変更

Writer / Sketch / Type/Mouse → GFX → Display Service → 非同期Executorの不変Snapshot → `epd_ssd1677_present_snapshot()` → SSD1677 Driver。今回変えたのはDriverのfastest RAM書込みだけで、App・Service・Executor・LUT・0x22/0x20・SPI設定・電源制御は維持した。前回のWriter/Sketch描画要求削減MVPは独立しており、このブランチへ取り込んでいない。

従来fastestは各更新で0x24へ新画像、0x26へShadowの旧画像を**両方全画面**送信し、0x20で更新を始める。HostのShadowはBUSY成功後に進む。候補はコンパイル時の`SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL=1`でのみ有効となり、通常ビルドではOFF。PaperMonoのfastestかつTile buffer使用可能なときに限る。初回・RAM同期不明時は従来の両面全画面書込みを行い、0x20成功後に0x26を新画像へ全画面同期する。両RAMが新画像と一致した次回から、0x24をDirty Rectangleだけに書き、旧画像が既に入っている0x26を使って0x20を実行し、完了後に同じRectangleの0x26を新画像へ同期する。Snapshotは最後まで不変で、Shadowは全工程成功後だけ進める。

Dirty Rectangleは既存のTile/Shadow実画素差分を使用し、Xを8画素のRAM byte境界へ拡張する。Yは論理上からControllerのY減少アドレスへ変換する。単一Rectangleのため離れた変更は間も含む。範囲不正時は書込みを拒否する。初回、Reset、Suspend/Resume、Shadow無効、モード切替、SPI・0x20・BUSY・更新後同期の失敗では部分経路を使わない。故障時は既存のfault latchが自動再試行を止め、再起動後の正規OTP全画面経路から復帰する。途中失敗でShadowだけを進めることはない。

変更ファイル: `src/drivers/epd_ssd1677_reference.inc`、`src/drivers/epd_ssd1677.c`、`src/drivers/epd_ssd1677.h`、`src/CMakeLists.txt`、`tests/host/epd_ssd1677_dirty_hold_test.c`、`tests/host/epd_ssd1677_test.c`、`tests/host/Makefile`。Branch: `experiment/papermono-ssd1677-dirty-hold-host`。

## 同一Frame系列のBefore / After

白の正規OTP baseline後、最初のfastest更新、X/Yの8画素境界、四隅、離れた2領域、同一位置の白黒反転を含む**10回**の更新を両経路へ入力した。値はHost SPI mockで捕捉した実送信byte数であり、EPD駆動量ではない。

| 指標 | 従来 | 候補 |
| --- | ---: | ---: |
| 0x24データ | 480,000 B | 70,886 B |
| 0x26データ（更新後同期を含む） | 480,000 B | 118,886 B |
| 全SPI byte（command/address/LUTを含む） | 961,571 B | 191,360 B |
| Tile→Panel変換対象byte（RAM窓とTile境界から計算） | 960,000 B | 191,584 B |
| 0x20更新命令 | 10回 | 10回 |
| Host Driver CPU時間（1回の参考値） | 1.059 ms | 0.469 ms |

候補の初回は0x26の更新後全画面同期が加わるため、従来より48,000 B多い。2回目以降の小領域で節約する。上記系列には76 byte×301行の大きい単一Rectangleも含む。共通のDirty Bounds算出はHost上500回で平均11.782 µs/回だった。CPU値はOS負荷・mockの影響を受ける参考値であり、ESP32上の時間やBUSY時間を表さない。

## Host検証

SPI commandを解釈する最小Controller RAMモデルで、0x24・0x26、0x44/0x45、0x4e/0x4f、書込み窓、旧/新画像、Shadow、各0x20時点の4遷移（黒保持・黒→白・白→黒・白保持）を追跡した。全更新時に0x24=要求Frame、0x26=直前の論理表示Frameを確認し、候補では更新後に両RAM=新Frameも確認した。無変更Frameは0x20なし。X/Y境界・四隅・離れた変更・同一領域反転・再初期化・Shadow無効化・モード切替・Suspend/Resume・部分書込み途中/0x20/BUSY/更新後同期の失敗を確認した。

既存SSD1677（通常・waveform trial）、Display Async、Writer保存・品質・Checkpoint、Sketch Canvas/Lifecycle、Type/Mouse Engineの主要Host回帰はPASS。新規Controller RAM試験はAddressSanitizer/UBSanでもPASS。PaperMono構成（`SOLAR_OS_BOARD=m5paper_mono`、`SOLAR_OS_FLAVOR=writerdeck`、`SOLAR_OS_LAYOUT=ota`、Sketch latency trial=0、waveform trial=0）でDirty HOLD trial=0/1の両方のESP32 compile/linkは成功した。生成されたbuild選択記録のboard/flavor/layout/waveform trial=0と、`compile_commands.json`のDriverにおけるDirty HOLD trial=1の定義を確認した。最終成果物はtrial=0へ戻した。生成Firmwareは**実機へ書き込んでいない**。

## 仕様上の境界と次の判断

[SSD1677 Rev 1.0 §8.3–8.5](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf)は0x44/0x45をRAM書込み窓、0x4e/0x4fをRAMアドレスカウンターとして説明し、0x22/0x20を表示更新の指示として区別する。**RAM書込み窓がGate走査やVCOMの物理影響を同じ範囲へ限定するとは記載していない。** [M5GFXのSSD1677 Mode-2実装](https://github.com/m5stack/M5GFX/blob/master/src/lgfx/v1/panel/Panel_SSD1677.inl)はDirty情報を使いつつ0x24/0x26に全画面分を送り、Mode-2のRAM面状態を毎回作り直している。したがってHostモデルで成立したRAM保持とHOLD遷移を、実Controllerの全条件や更新対象外の黒画素に印加される電圧の保証とは扱わない。

実機評価に進む価値はRAM転送量の観点ではあるが、現在の個体は黒の薄まりが早くなった懸念がある。まず別途、仕様上のRAM保持・Mode-2面切替を確認し、実機試験を行うなら通常版へのapplication-only rollbackを準備したうえで、正規cleanup後に少数回だけ比較し、反転・残像・濃度悪化・BUSY異常で即停止する。**この文書は実機試験やflashの承認・実施記録ではない。**
