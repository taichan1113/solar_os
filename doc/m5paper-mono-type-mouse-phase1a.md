# PaperMono Type/Mouse Phase 1A — FT6336G complete frame

2026-10-14。Phase 1Aの実装とHost検証を完了。実機の2点入力を検証したという判定ではない。独立レビュー後にPhase 1Bへ進むための成果物である。

## A. Git・作業環境

| 項目 | 結果 |
|---|---|
| 実装branch | `experiment/papermono-type-mouse-phase1a` |
| 実装commit | `c08889e8f9e33fde77ce928c4131d9f607315d28` |
| 分岐元HEAD | `31abd6db216bd94833ae092f6948438f8f1e4256` |
| 専用worktree | `E:\ESP32\M5PapserMono-SolarOS\.research\type-mouse-phase1a\worktree` |
| 元worktree | `E:\ESP32\M5PapserMono-SolarOS`、Sketch計測branchを維持 |
| 参照元 | `E:\ESP32\M5Paper_Writerdeck`、`gt911-task-experiment`、`86877a881a89b4867c561899f42bdfecd973af1b` |

本報告書とHost runnerのLF指定は実装commitの後続commitに収録する。最終HEADはこのbranchで `git rev-parse HEAD` から取得できる。

元worktreeの既存 `?? .vscode/` を維持した。参照元の既存変更 `.vscode/extensions.json`、`include/WriterDeckBleUtils.h` も維持し、書き込みは行っていない。stash/reset/clean/強制checkoutは使用していない。専用worktreeは既存の `.research/` ignore配下に作成したため、元worktreeへ新たな未追跡項目は追加されない。

変更ファイル：

- `src/drivers/ft6336.{c,h}`：新しいreaderの追加のみ。従来reader・初期化・停止処理を維持。
- `src/drivers/ft6336_frame.{c,h}`：純粋decoder、raw frame/contact型。
- `src/services/solar_os_input_touch_core.{c,h}`：純粋なID比較、状態破棄・再同期結果。
- `packages/solar_os_packages.toml`：既存FT6336 packageにdecoderのCファイルを追加。
- `tests/host/ft6336_frame_test.c`、`input_touch_core_test.c`：新規Host試験。
- `tests/host/ft6336_test.c`：既存fixtureを維持し、13バイトreaderとlegacy互換試験を追加。
- `tests/host/Makefile`、`run_touch_phase1a.sh`、`.gitattributes`：Hostリンク・実行登録とrunnerのLF指定。
- 本報告書。

## B. APIと状態管理

### Reader / Decoder

```c
esp_err_t ft6336_read_frame(ft6336_frame_t *frame);
ft6336_frame_result_t ft6336_frame_decode(
    const uint8_t *data, size_t length, ft6336_frame_t *out);
```

Readerは既存 `solar_os_bus_i2c_read_reg()` で `0x02` から13バイトを1回だけ読む。新規I2C初期化・GPIO操作・再試行はない。従来 `ft6336_read()` は従来どおり6バイト、非0点数なら先頭点だけを返す。旧APIが以前から許容していた非0の異常点数・予約flag・ID Fの解釈も変更していない。

`ft6336_frame_t` は13バイトのraw全体、切り詰めない報告点数、2個のcontact、解析結果、理由bitsetを持つ。contactは4bit ID、12bit X/Y、event、`reported`、`valid` を持つ。未報告slotも復号してraw情報を残すが、active contactとして使わない。weight/miscもraw内に保存する。回転、画面範囲へのclamp、hit-testは行わない。

`valid` はcontact単体の構造判定であり、Frame全体の採用可否ではない。例えばUP contactの `valid` がtrueでもFrameはUNCERTAINとなる。利用側は必ずReaderの戻り値と `frame.result` の両方を検査する。

| 状況 | Reader戻り値 / Frame結果 | 方針 |
|---|---|---|
| 正常0/1/2点 | `ESP_OK` / `FT6336_FRAME_OK` | 報告範囲のcontactのみ使用可能 |
| count 3〜15 | `ESP_OK` / INVALID | countを2に縮めず、COUNT理由を保持 |
| 同じIDが2件 | `ESP_OK` / INVALID | DUPLICATE_ID。両contactをvalid=false |
| 報告slotのID F | `ESP_OK` / INVALID | ID_SENTINEL。raw値を残し保守的に拒否 |
| 報告slotのRESERVED | `ESP_OK` / INVALID | RESERVED_EVENT |
| 報告slotのUP | `ESP_OK` / UNCERTAIN | UP_COUNT_UNKNOWN。UPを消去せず後段へ判断材料を返す |
| I2C失敗 | その `esp_err_t` | 呼出元Frameを一切更新しない。空Frameに変換しない |
| 未初期化 / NULL出力 | INVALID_STATE / INVALID_ARG | I2Cを実行せず、出力を変更しない |
| DecoderのNULL入力 / 13未満 | INVALID + ARGUMENT / LENGTH | 非NULL出力には初期化済みの失敗結果を返す |

複数の異常理由は同時に残る。INVALIDはUNCERTAINより優先する。Frame結果enumのゼロ値はINVALIDであり、ゼロ初期化しただけのFrameを正常な空Frameと誤認しない。

I2C失敗後は出力が以前のFrameのままであるため、そのFrameを「今回取得できた」として再利用してはならない。単一burstの成功も、controller内部で全13バイトがラッチされた証明にはならない。

### Pure contact core

`solar_os_input_touch_core_init()` と `solar_os_input_touch_core_apply()` はI2C、FreeRTOS、Input Serviceに依存しない。入力はdecoderが作ったFrameで、rawを再検査して公開構造体の不正なcount/fieldで配列アクセスが破綻しないようにしている。出力snapshotはID順、started/moved/endedも最大2件ずつの固定長である。

- `[] → [3] → [3,7] → [7,3] → [7] → []` を追跡する。配列順だけの変更はedgeを生成しない。
- 同じIDの座標が変わればMOVE、消えればEND、新しく加わればSTART。DOWNが繰り返されても毎回STARTしない。
- 一部IDの入替は別接触としてSTART/ENDを区別する。同じ座標でも同じ指とは推定しない。
- 非空から非空へID集合が完全に入れ替われば `CANCEL_REQUIRED / ID_DISCONTINUITY`。通常のEND/STARTは生成しない。
- `frame == NULL` は読取失敗を表す。INVALID/UNCERTAINも含め `CANCEL_REQUIRED` と理由を返し、保存接触を破棄する。通常のENDは生成しない。
- 障害後最初の正常Frameは `RESYNCED` と現在snapshotのみを返す。過去接触に対するMOVEや新たなSTARTを捏造しない。その次から通常の比較を再開する。

これは状態比較結果でありイベント配送ではない。後段が取消を無視してsnapshotだけを読む実装は契約違反となる。実際のCANCEL配送、epoch、受理再開条件、全離指gateはPhase 1Bの責務。ここでは全離指gateやMouseのdebounce/graceを実装していない。

coreは現在のpackage sourceへ登録しておらず、既存Worker/Inputからも呼ばれない。Phase 1Bの接続時にsource登録と所有者を決める。decoderだけは新readerのリンク依存のため既存driver packageに登録した。

## C. 一次資料との照合・未確定仕様

| 根拠 | 確認範囲と限界 |
|---|---|
| [FocalTech FT6336G v1.0](https://www.orientdisplay.com/wp-content/uploads/2022/08/FT6336G_v1.0.pdf)、Features、§2.4.1 | 2点対応とI2Cを確認。公開資料には座標registerの詳細表がない。 |
| [FocalTech FT6X36資料に添付されたFT6x06 CTPM app note](https://focuslcds.com/wp-content/uploads/Drivers/FT6236.pdf)、app note pp.8–10（PDF pp.26–28）、§3.1 | familyのstatus、6バイトrecord、XHのevent、YHのID配置を照合。G専用firmwareの全挙動を保証する資料ではない。 |
| [FocalTech公式Linux driver](https://github.com/focaltech-systems/drivers-input-touchscreen-FTS_driver/blob/master/ft5x06.c)、`fts_read_Touchdata` / `fts_report_value` | raw IDとeventの復号、ID Fを終端扱いする実装、DOWN/CONTACTとUP系の分離を照合。GPLコードは移植していない。 |
| 参照元M5GFX 0.2.28 `src/lgfx/v1/touch/Touch_FT5x06.cpp`、`_read_data` / `getTouchRaw` | 0x02のcount、6バイト間隔、座標とIDの位置を照合。flagを評価しないためUP/countの根拠には使えない。 |

実装で使うfamily形式はstatus=`0x02`、P1=`0x03..0x08`、P2=`0x09..0x0E`。XH[7:6]はDOWN=0、UP=1、CONTACT=2、RESERVED=3、XH[3:0]/XLはX、YH[7:4]はID、YH[3:0]/YLはYとして扱う。

IDをslotの0/1へ置換しない。ID 0〜14は構造上受理し、Fは関連公式driverのsentinel使用を根拠に保守的に拒否する。これはFT6336GでFが必ず不正だと実測確認した主張ではない。

正常なcount=0は完全な空Frameとして扱う。未報告slot内のUPや古い座標はrawとして保持し、active点に昇格させない。一方、countに含まれるUPは「countが現在active点数なのか、有効record数なのか」というG固有の整合契約を確定できないためUNCERTAINとする。countからUP数を推測で引いたり、UPを単に捨てたりしない。

未確定事項：13バイト中のラッチ/coherence、離指時のUP配置とcount、近接・交差・再接触時のID安定性/F、未使用slotの内容、実際の2点座標範囲。M5GFXは複数read比較を使っているが、今回は取得周期・read再試行のポリシーを変更しない。これらはPhase 1B以降の限定実機確認対象であり、Host PASSとは分離する。

## D. Host試験結果

環境：WSL `Ubuntu-24.04`、C11、通常 `-O2 -Wall -Wextra -Werror`。新規decoder/coreはESP-IDF stubも不要。以下のコマンドを専用worktreeで実行した。

```powershell
wsl -d Ubuntu-24.04 -- sh /mnt/e/ESP32/M5PapserMono-SolarOS/.research/type-mouse-phase1a/worktree/tests/host/run_touch_phase1a.sh
wsl -d Ubuntu-24.04 -- sh /mnt/e/ESP32/M5PapserMono-SolarOS/.research/type-mouse-phase1a/worktree/tests/host/run_touch_phase1a.sh --sanitize
```

| C試験 | 件数 / 結果 |
|---|---|
| `ft6336_frame_test` | 16ケース PASS。0/1/2点、順序入替、重複/F、全異常count、flag、UP/未報告UP、複数理由、短いbuffer、NULL、長いbuffer。ID/event 1,024組の総当たりを1ケースに含む。 |
| `input_touch_core_test` | 11ケース PASS。要求された点数遷移、順序入替、片方/双方MOVE、IDだけの変化、ID集合完全入替、5種類の不正Frame、read失敗、復帰、構造体不一致、NULL、繰返しDOWN。 |
| `ft6336_test` | 6ケース PASS。既存初期化fixture、legacy 6バイト互換、1回の13バイト取得、途中書込みを伴うI2C失敗でも出力不変、解析結果、NULL/未初期化。 |
| `ft6336_lifecycle_test` | 既存1 executable PASS。service stubは引き続き6バイトreadを要求し、新readerへ切り替わっていないことも検証。 |

新規/拡張3プログラムは計33ケース。通常と `-fsanitize=address,undefined -fno-omit-frame-pointer` の両方で上記4プログラムがPASSし、sanitizer診断なし。sanitizer後に通常ビルドでも再確認した。

関連回帰はWSL内の `tests/host` で以下を実行した。

```sh
make input_test sketch_canvas_test sketch_lifecycle_test sketch_scale140_lifecycle_test sketch_legacy_lifecycle_test sketch_trace_core_test
./input_test
./sketch_canvas_test
./sketch_lifecycle_test
./sketch_scale140_lifecycle_test
./sketch_legacy_lifecycle_test
./sketch_trace_core_test
```

上記6 executableはすべてPASS。通常Sketch、UI140%、legacy同期描画、PNG保存/再読込/Import、trace core、Inputの既存試験を含む。

worktree rootで以下も実行した。

```sh
python3 -m unittest discover -s tests -p test_board_manifests.py
python3 -m unittest discover -s tests -p test_flavor_packages.py
python3 -m unittest discover -s tests -p test_sketch_trace.py
git diff --check
```

Pythonは33 + 42 + 8 = **83テスト、PASS、FAIL 0**。差分の空白エラーなし。全Host/全Python suiteを実行したという意味ではない。ESP32 firmwareのbuild/link、実機USB、flash、2点の物理入力試験は未実施。

## E. 既存挙動・回帰リスク

`ft6336_read()` の本文は分岐元から変更なし。`solar_os_ft6336.c` のWorker、10ms待機、電源/reset/IRQ経路、pointer生成も変更なし。Input、main、Sessions、Writer、Sketch、BLE、GFX、SSD1677、Powerのソースに差分がないことを確認した。NVS/FAT/otadataや実機設定へアクセスしていない。

リンク依存としてdecoderを追加したが既存readerは使用しない。新APIを既存Workerへ統合する変更もないため、現在の1点入力経路の挙動を変更しない構成である。Host範囲では回帰を検出していない。target buildや実機動作の新たなPASSは主張しない。

## F. Phase 1Bへの契約と引継ぎ

以下は接続方法の契約であり、今回は未実装。

1. 取得担当はReader失敗を空Frameへ変換せず、coreへ失敗を通知する。取得成功でもFrame INVALID/UNCERTAINを採用しない。取得Frame全体を一単位として扱う。
2. `TRANSITION`、`CANCEL_REQUIRED`、`RESYNCED` の違いを保持する。取消要求で通常のキー解放/クリック/drag終了を代用しない。再同期snapshotから古い接触のMOVEや新PRESSを捏造しない。
3. 新Touch Frame Eventに取得時刻・sequence・source epoch・recipient契約を加える。bounded FIFOのoverflow時もCANCEL/再同期を必要とする。個々のPointer Event2件へ分解して配送しない。
4. 接触管理coreの所有者と初期化/resetタイミングを定義し、session切替・suspend・source closeで取消、epoch失効、必要な全離指gateを設ける。Mouse特有のgrace等は別レイヤーで扱う。
5. Sessionのopt-in配送とlegacy primary projectionを実装する。Sketchには従来1本のpointer系列を維持し、primaryの選択/消失と二重回転を検査する。原Frameを投影処理で破壊しない。
6. Workerが将来新readerを選ぶ際、13バイト取得、UP/count、ID/F、順序変更、2点座標、coherence、失敗復帰を実機で確認する。今回の不確実性を推測で消さない。

Phase 1B、Type/Mouse App、BLE HID、Sketch Phase 2の変更・実機計測には着手していない。
