# PaperMono Candidate D — Writer実使用Trial Host / Build完了

2026-10-18。判定：**Writer Trial実装・Host・Build完了。実機評価可能。**

今回はCOM列挙・アクセス、Flash、実機Writer Trial、Integrationへのmerge/pushを実施していない。現在の実機Firmwareと保存領域は今回の作業で変更していない。W-1〜W-6は未判定。

## 開発状態と成果物

| 項目 | 結果 |
|---|---|
| 基点Integration | `391925abff2d261d1d5856711aa0a1f101ac435a` |
| Branch | `experiment/papermono-writer-black-hold-d` |
| Worktree | `E:\ESP32\M5PapserMono-SolarOS\.research\writer-black-hold-d\worktree` |
| 実装Commit | `029724b0f3738acbdede42c476976656ca52865e` |
| ビルド修正Commit | `695809f65d572bba550b06b4c22dda0363a4bc3a` |
| 最終ソース/計数修正Commit | `251232824da2f5350dbdf9184e0398b434a61497` |
| Application-only成果物 | `E:\ESP32\M5PapserMono-SolarOS\.research\writer-black-hold-d\artifacts\firmware-app-only.bin` |
| SHA-256 | `517250fa47fd1d702fccf28e5f3da6d4557fca978bd6f9e953d589ed6b29a253` |
| 実用版のRollback参照SHA | `278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6` |

既存28 WorktreeのBranch、HEAD、status、変更済み/未追跡ファイル内容のSHAは作業前後で一致。元Worktreeの未追跡`.vscode/`も維持した。[比較証跡](evidence/writer-black-hold-d-20261018/other-worktrees-after.json)。実験Branch以外へCommitしていない。

## A. 実際の更新経路

WriterはLVGLを経由せず、独自GFX/U8g2で描画する。入力・描画スケジュール・フォント・レイアウトは変更していない。

| 段階 | 実装・今回の扱い |
|---|---|
| BLE→Input | `solar_os_ble_keyboard.c:1621`の`solar_os_input_write_key`。Inputがキー/文字を保持し、`main.c:1210`の`dispatch_char_to_input_focus`がCHARイベントを入力先へ配送。変更なし。 |
| Writer | [writer_event](../src/apps/solar_os_writer.c#L1668)でCHAR配送時刻を記録。通常の編集・parse/layout pending処理を維持し、[writer_render](../src/apps/solar_os_writer.c#L1419)がU8g2へ描画。 |
| GFX→Display | [solar_os_gfx_submit_writer](../src/services/solar_os_gfx.c#L1700)→[solar_os_display_submit_writer](../src/services/solar_os_display.c#L1893)。明示的なWriter識別と時刻だけを追加。 |
| Snapshot Queue | [submit_snapshot](../src/services/solar_os_display_async.c#L167)が既存2枚のPSRAM bufferへ画像と識別情報を一緒にコピー。activeは不変、pendingは最新画像で置換する既存方式。新しいtask/Queue/Mutexは追加していない。 |
| Executor | [worker](../src/services/solar_os_display_async.c#L49)→[execute_frame](../src/services/solar_os_ssd1677.c#L70)→[epd_ssd1677_present_snapshot](../src/drivers/epd_ssd1677.c#L1033)。active snapshotの識別情報だけをDriver呼出し中に設定し、完了後に消す。idle maintenanceにはWriter識別を引き継がない。 |
| Dirty計算 | [ssd1677_reference_refresh](../src/drivers/epd_ssd1677_reference.inc#L170)が実snapshotと完了Shadowを比較。通常のtile→controller行変換、byte境界、R0/R2、rows-dual、32回ごとの全RAM同期を維持。 |
| RAM/LUT/更新 | 通常の順序：RAM 0x24（必要時0x26も）→0x32 LUT→0x03/04/2C→0x21=0→0x22=0x0C（coldでは0xCC）→0x20一回→BUSY→既存の両面RAM同期。D時のみ最後にBaseline 105 Bを0x32へ戻し、Shadowを昇格する。 |

Writerが前面という推測は使わない。同期fallbackやoverlayなどWriterタグを通せない経路にはDを適用しない。非Writerがpending Writerを置換した場合もWriterタグを消す。Writer同士のpending置換では、入力数と最初/最後の入力時刻を集約し、最新画像を使う。

### RAM転送範囲と物理作用範囲

rows-dualは全幅・変更行の**RAM転送**を削減する。0x44/45/4E/4FはRAMアドレス用であり、今回Driverは独立した物理更新範囲制限を設定していない。0x21=0でRAMを通常使用し、0x22/20でMode-2更新を開始する。[SSD1677 Rev1.0 §6.5、§7 R20/21/22/37、§8.2〜8.5](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf)を参照。

ソース上は0x24がNew、0x26がOldを表し、固定黒のOld/New=0/0がLUT0へ対応する。更新後は両コマンド面を現在画像へ同期する。0x37 F6による内部ping-pongの実設定はreadbackしていないため、Hostでは保持/入替の両モデルを検証した。内部物理RAMの入替を確定したとは扱わない。

**推論**：RAM転送範囲外でも保持される黒画素がLUT0の対象となる可能性がある。しかしWriterでの実際の駆動・黒化作用は未確認であり、全画面への補償を保証しない。Hostの全画面RAM分類と固定画像Trialは、実パネルの空間的作用範囲を証明しない。将来試験では更新行内/外の固定文字を別ROIで観測する。

## B. Trialの動作

新規flagは`SOLAR_OS_SSD1677_WRITER_D_TRIAL=1`。未指定/0ではTrial API・状態・LUTをコンパイルしない。flag有効のFirmwareも起動時はBaseline。NVSや`/.shell/startup`へ試験モードを書かない。

Candidate Dの110 B配列だけを既存DAD成果から再利用した。旧固定画像診断、強制全RAM更新、同一画像強制更新は取り込んでいない。

| LUT | 内容/SHA-256 |
|---|---|
| A | LUT0全ゼロ。`e8a7abbb2110cb8d941e80cc2869bbe1d0814e1d4a38e5d0cc5173b10f39ce50` |
| D | LUT0=`2F F0 00 00 00 00 00 00 00 00`。`9be4bea8b9a07fef33834293d25979a9b0f3fc820d3e9912a1fc106532ef1832` |

差は配列のbyte 0,1だけ。byte 2〜109、LUT1〜4、Timing/FR、電圧値は一致。合計10フレーム。実効DCバランス、残留電荷、寿命、メーカー検証は未確認。

### USB Shellコマンド

本体Shellを描き換えないよう**USB Shell限定**。Writerを開いたままUSBから操作する。

```text
display test display0 writer-lut status
display test display0 writer-lut baseline
display test display0 writer-lut candidate_d
display test display0 writer-lut disable
display test display0 writer-lut trace
```

`baseline`と`disable`はいずれもD選択を解除する。これらの操作だけではRAM/LUT送信や0x20を発行しない。`candidate_d`はPaperMono、正常Driver、既存executor、fastest+tile、20MHz、rows-dualという条件を検査する。他refresh/転送方式では拒否し、設定を自動変更しない。

モード確定とD予約は、実変更を見つけた**Driver実行時、RAM送信前**。[writer_d_reserve](../src/drivers/epd_ssd1677_writer_d.inc#L19)。要求投入時にはA/Dを固定しない。切替コマンドは既存control barrierを使い、activeのBUSY・両面同期・Baseline復帰まで待つ。pendingは保持し、次の実行時に新モードを選ぶ。変更のない画像は従来どおり省略し、D予算を消費しない。

初回OTPやcustom cold entryはAで実行し、D予算へ含めない。温間状態になった実Writer更新からDが使える。再同期が必要な場合の既存全RAM fallbackも保持するが、診断用に全RAMへ強制しない。

### 上限と異常停止

[epd_ssd1677_writer_d.inc](../src/drivers/epd_ssd1677_writer_d.inc)のboot-global atomic予約で最大6。失敗後も返却せず、選択解除・再選択・Driver再初期化・App切替でも上限は戻らない。6回目の成功時にはBaselineへ復帰後、cap到達ログと`cap_to_A`記録を残す。7回目以降は通常A、D再選択は拒否する。これ以降の記録はD評価に混ぜない。

LUT送信・BUSY・同期・Baseline復帰の失敗ではDを無効化してfaultを保持し、Shadow/Driverを無効化、executorがpendingを破棄して新規更新を拒否する。Baselineが不明なまま正常Aへ復帰したと仮定しない。物理再試行や復帰のための追加0x20はない。

`attempt`は予約数、`D0x20`はSPI送信成功を確認したDの0x20数、`success/error`は復帰まで含めた結果。0x20送信失敗時に実電気動作が絶対ゼロだったと保証はしないため、予約を返却しない。回数はキー数から推定しない。

電源OFF専用の0x22=0x03→0x20は`power0x20`へ分離し、Writer/他Appの描画更新数へ混入させない。電源制御シーケンス自体は変更していない。

全体再起動でRAMカウンタはリセットされる。同じ試験で再起動を使い予算を増やさない。**カーソル点滅・header/dialog等もWriterの物理更新ならD予算に含む。** これらの通常挙動は試験のために抑制していない。

## C. 計測とHost結果

32件の上書き式RAM記録にgeneration、Writer CHAR受付数、pending置換数、入力/描画/Queue/Driver実行時刻、RAM開始/完了、0x20直前、BUSY解除、LUT送信時間、Baseline復帰時間、RAM payload、dirty境界、エラー、A/D/OTPを保存する。各入力で同期テキストログを大量出力せず、USB `trace`で後から取り出す。累積カウンタは記録32件を超えても継続する。

入力受付→描画要求、Queue待ち、RAM書込み区間、pixel SPI時間、BUSY、Driver総時間、LUT復帰コスト、最初/最後のCHAR受付→完了を比較可能。`ram_bytes`と`spi_us`は既存Driverのpixel RAM payload/転送計測で、command/address/LUTを含む全SPIワイヤ総量ではない。0x32の初回送信と復帰は別時間に記録する。追加復帰のワイヤ下限は20MHzで106 B×8/20MHz＝42.4µsであり、実処理時間は実機ログを使う。

BLE受信時刻→Writer受付の個別対応、編集完了の独立時刻、光学的な画素変化開始時刻は今回追加計測していない。CHAR数は配送数で、編集文字数やBLE report数ではない。初期描画/カーソル等は入力数0になり得る。異なるApp由来の置換やcancelがあれば、個々のキーとの完全対応とは扱わない。

| テスト | 結果 |
|---|---|
| 既存主要19実行ファイル | PASS。Driver/Dirty/SPI20/Hybrid、Sketch各経路、Display Async、Writer品質/Buffer/File、Launcher、TypeMouse、Shell起動。 |
| Sketch Python9件 | PASS。 |
| `writer_d_driver_test` | PASS。LUT同一性/10frames、実RAM payloadとコマンド列のA/D同一性、追加0x20なし、No-op、rows-dual、R0/R2の実U8g2描画、RAM保持/入替両モデル、Shadow、6回制限/7回目A、再選択/再生成非返却、起動時A、LUT/BUSY/同期失敗。 |
| `writer_d_async_test` | PASS。実Driver＋実executor＋pthread排他を接続。D更新中のWriter追加/集約、D→A切替を復帰まで保留、A→D切替、BUSY中の他App要求、App終了相当の既存PRESENT/SLEEP barrierによるpending cancel、6回到達。Writer停止後もsnapshot/識別情報はアプリのメモリを参照しない。実機でのApp遷移は別途回帰する。 |
| 同上`restore-fail`別process | PASS。実0x32復帰送信失敗を注入し、Driver/Queue停止、pending破棄、予算非返却。 |
| `writer_d_shell_test` | PASS。実コマンドhandler、USB限定、mode/status/trace、cap拒否、コマンドだけでは物理送信なし。 |
| 追加既存11実行ファイル | PASS。Waveform、Input、Input action、HID report、BLE scan/reconnect/Service/multi-peer/NimBLE/HID/server。 |

合計33実行ファイル＋異常系1別process＋Python9件。Hostは画質/寿命/実パネルBUSYの実測ではない。[主要ログ](evidence/writer-black-hold-d-20261018/host-main.log)、[追加/新規ログ](evidence/writer-black-hold-d-20261018/host-extra.log)。

## D. ESP32-S3 Build

| 項目 | 結果 |
|---|---|
| Board / Flavor / Layout | `m5paper_mono / writerdeck / ota` |
| SDK / Compiler | ESP-IDF 5.5.5 / GCC 14.2.0 |
| compile/link | PASS（最終ビルド約41.4秒） |
| 有効flag | `WRITER_D_TRIAL=1`, `DIRTY_HOLD_TRIAL=1`, `SPI20_TRIAL=1`, `HYBRID_TRIAL=1`（Integration機能維持） |
| 無効flag | `WAVEFORM_TRIAL=0`, `SKETCH_LATENCY_TRIAL=0`、旧BLACK_HOLD固定画像診断flag未使用 |
| 比較時の設定 | `refresh=fastest`, `fastest-path=tile`, `dirty-window=rows-dual`、20MHz。D選択は別途明示が必要。実効SPIは今回は未実測。 |
| Appサイズ / OTA枠 | 3,458,400 B / 7,340,032 B、余裕3,881,632 B |
| 最終build warning/error | 0 / 0（最終PlatformIOログ） |

最初のbuildでは既存`writer`マクロと新しい識別field名の衝突を検出した。`is_writer`へ改名し、関連Hostを再実行後に最終buildを成功させた。失敗ログも保存し、最初から成功したとは扱わない。

成果物はApplicationだけを独立ディレクトリへ複製した。PlatformIOが生成したfactory/bootloader/otadata imageをFlash対象としていない。manifestには参照partition表も記録しているが、将来の実機書込み先をこれだけで確定してはいけない。[最終manifest](evidence/writer-black-hold-d-20261018/manifest.json)、[buildログ](evidence/writer-black-hold-d-20261018/build.log)、[構成](evidence/writer-black-hold-d-20261018/build-selection.txt)。

変更ファイル群：Writerの識別/時刻、GFX→Display→Queue→SSD serviceの識別配送、DriverのD選択/復帰/上限/記録、USB Shell handler、flag設定、3新規Host＋共有fixture hook。Sketch、TypeMouse、Launcher、BLE/Inputの実装、baseline LUT、電圧値、既存Dirty計算と通常更新シーケンスは維持。[全変更ファイル](evidence/writer-black-hold-d-20261018/changed-files.txt)。

## E. 別承認後のWriter実機試験

1. 別途Flash承認を受け、競合/未保存編集を確認。実機のport/chip/Flash容量/partition/現在の起動slotを新規確認する。過去のCOM39・ota_0・0x10000を無検証で使わない。
2. 現Application/全Flashを新規退避し、SHAとRollback可能性を確認。NVS/FAT/otadata/PHY/partition/BLE bondの保護領域を退避・照合する。書込みは確認済みApplication領域だけ。Application readbackと保護領域一致、Shell正常を確認する。
3. USB Shellで設定と`writer-lut status`を取得。通常設定をfastest+tile+rows-dual/20MHzに揃え、Dを無効のまま初期文書を準備する。必要なOTP fullによる初期化回数も記録し、A/Dで揃える。試験対象は新しい一時文書。既存文書を編集/保存しない。
4. 固定文字`TEST HEADER / AAAA BBBB CCCC`を残し、下の入力領域を変える。A条件で数文字追加→Backspace→再入力→追加入力を少数ずつ実施する。実0x20、noop、集約数、時間、入力内容を記録する。
5. 同じ初期条件を作り、Writerを開いた状態でUSBから`candidate_d`を選択。条件Dを行う。操作数ではなく`D0x20/attempt`を監視し、最大6物理更新。自動A移行後はD評価を終了する。ノイズを避けるためstatus/traceの頻繁な取得を入力中に繰り返さず、区切りで取得する。
6. 固定露出/Gain/WB/FocusのInsta360 Linkで黒クリップを避け、固定文字・新規文字・削除領域・白背景を観測する。更新行の内/外の固定文字を分け、黒四角が飽和する場合は文字ROIを主指標にする。集約や物理回数が異なるA/Dを厳密な同回数比較と扱わない。
7. 既存と同程度の短時間白化だけでは不合格にしない。持続的異常/反転/背景汚染/Driver faultでは追加更新を止め、状態を保存してユーザーへ報告する。再起動で同一試験のD予算を追加取得しない。
8. 試験後は保存データを巻き戻さず、退避した実用Integration Applicationへ戻す。Application SHAと保護領域、USB Shell、ユーザー目視を再確認する。W-1〜W-6を実測から判断し、Integration採用は別承認とする。

今回の到達点は、通常Writerの非同期Dirty経路へDを適用する実験Firmwareの準備完了。固定黒改善、実入力遅延、背景品質、実機安定性はまだ確認していない。
