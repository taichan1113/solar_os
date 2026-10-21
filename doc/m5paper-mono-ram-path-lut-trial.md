# PaperMono：RAM転送方式 × LUT 独立切替試験

2026-10-21（JST）。本体Shellで選択し、人手で実使用するための試験版。自動描画・カメラ測定・定量試験・新しい波形は追加しない。

## A. 実装と開始点

- Branch：`feature/papermono-ram-path-lut-trial`。
- 実装・最終Build Commit：`8dd1c43b089872ee0cc88084035a06be296d5945`。
- 開始Commit：`b163088fbdcb11231e4486df520377bd46431d14`（`origin/feature/papermono-standalone-lut-trial`）。本体Shellの既存 `lut 1..4`、Sketch A例外、Sketch D opt-in、E1、区間Traceを含むため、この版を採用した。
- 親PCIntegration `aa08bbfd` の独立したSTC8H／TFT上流更新は、この試験に不要なので取り込まない。Integration・mainへのmergeはしない。
- Worktree：`E:\ESP32\M5PapserMono-SolarOS\.research\ram-path-lut-trial\worktree`。
- 今回の範囲は実装・Host検証・Build・実験BranchへのPush。COM39は将来Flash時の候補として記録するが、COM接続・Flashは行わない。

既存rows-dualを無効にせず、DriverにRAM内だけの `dirty_hold_force_full` を追加した。同期方式・LUT適用条件は従来のrows-dualのまま、転送窓だけ全画面へ広げる。`dirty-window=off` は使用しない。

転送は既存 `ssd1677_write_bands()`／`ssd1677_write_tile_window()` を再利用する。Shadowの昇格、BUSY待ち、電圧・LUT送信、物理更新、更新後の両RAM同期、LUT復元の順序は維持する。新しいwait／pacing／Queue／Task／NVS設定は追加しない。

主な変更ファイル：

| ファイル | 変更 |
|---|---|
| `src/drivers/epd_ssd1677.h` | RAM選択boolとbarrier付きAPI宣言 |
| `src/drivers/epd_ssd1677_writer_d.inc` | `epd_ssd1677_ram_mode()`。LUT状態を変更しない選択／取得 |
| `src/drivers/epd_ssd1677_reference.inc` | RAM同期済み判定と転送窓の判定を分離。full温間更新でも更新前は1面のみ |
| `src/shell/solar_os_shell_writer_lut.inc` | `ram` の短い1行応答 |
| `src/shell/solar_os_shell_commands.c/.h`、`src/apps/solar_os_shell.c` | builtinとhelp登録。既存 `lut` の動作は維持 |
| `doc/manual/commands.md` | 組込みmanual登録 |
| `tests/host/ram_path_lut_cases.inc`、既存Driver／Shell／Async fixture、Makefile、`run_ram_path_lut.sh` | 実Driver・SPI捕捉・既存RAMモデルによる検証 |
| `.gitattributes` | 新旧試験runnerと既存Build入口をLF指定。Windows checkoutでbash/shがCRLFにより停止する問題を防止 |

## B. 本体Shell操作

本体に接続したキーボードから入力する。`ram` は既存の `ramfs` と別名で、既存builtinとの衝突はない。

```text
ram rows
ram full
ram status
```

成功応答は `RAM: rows-dual` または `RAM: full` の1行だけ。引数なしの `ram` もstatus。異常・未対応状態は短いエラーを返し、選択を変更しない。

既存LUTコマンドは維持する：

| 選択 | 通常App | Sketch |
|---|---|---|
| `lut 1` | A | A |
| `lut 2` | D | A |
| `lut 3` | D | D |
| `lut 4` | E1 | E1 |

通常設定への復帰：

```text
ram rows
lut 2
```

Driver再作成／再起動でもRAMはrows-dual、LUTはD／Sketch Aへ戻る。Sleep/WakeはRAM内選択を保持し、既存の復帰・全面準備経路を使用する。NVSや `/.shell/startup` は変更していない。

試験には `refresh=fastest`、`fastest-path=tile`、SPI20、`dirty-window=rows-dual` が必要。これらは既存機構であり、`ram` 自体は勝手にモードを変更しない。既存startup等で他refreshになっている場合は、試験前に本体Shellから必要に応じて以下を選ぶ。

```text
display mode display0 refresh=fastest
display mode display0 fastest-path=tile
```

`dirty-window=off/rows/columns` 等の旧診断設定を使わず、rows-dualを維持する。設定不一致・fault時には `ram full/rows` の変更は拒否される。

## C. RAM転送・波形の独立性

| 状態 | 更新前 | 物理更新 | 更新後 | 画像データ量 |
|---|---|---|---|---:|
| 同期済み温間rows-dual | `0x24` 全幅×変更行 | `0x20` 1回 | `0x24`＋`0x26` 同じ変更行 | 300B×変更行数 |
| 同期済み温間full-RAM | `0x24` 全面 | `0x20` 1回 | `0x24`＋`0x26` 全面 | 144,000B |
| 初回custom・未同期・32回周期の準備 | `0x24` 新画像＋`0x26` 旧画像の全面 | `0x20` 1回 | `0x24`＋`0x26` 全面 | 192,000B |
| No-op | なし | 0回 | なし | 0B |

データ量は800×480・1bit面48,000Bに基づくRAM画像payloadで、SPI command・address・LUT等のbyteを含めない。dirtyが全高の場合はrows側も全面量になる。

- A／D／E1の110B配列は開始版と全byte一致。LUT0はA=`00 00`、D=`2F F0`、E1=`3B F0`。実際の `0x32` 送信105BもHostで照合する。
- fullはdirty同期機構を有効に保つため、D／E1の既存条件を外さない。`ram` はLUT選択・Sketch例外を変えず、`lut` もRAM選択を変えない。
- 切替は既存ExecutorのREAD control barrierを使い、進行中のBUSY・両RAM同期・LUT復元まで待つ。Pendingは破棄せず、元のAppタグを保持して、実行開始時のRAM／LUT選択を使う。
- 切替そのものではSPIも `0x20` も発行しない。ただしキー入力や結果を画面に描く通常Shell処理は表示更新を発生させ得る。
- 両面同期が既知ならrows↔full切替だけで同期を無効化しない。既に未同期なら全面準備を維持する。32回周期はfullでも維持し、余分な物理更新は増やさない。
- 既存Traceの `mode`／`ram_path`／`ram_window`／`periodic_resync`／`err` をそのまま使う。fullの実更新をrowsとは記録しない。新しい測定サブシステムは追加しない。

## D. Host検証

入口：`sh tests/host/run_ram_path_lut.sh`。既存runnerに追加ケースをつないで使う。WSLでは必要に応じて `CJSON_DIR=/mnt/c/Users/takec/.platformio/packages/framework-espidf/components/json/cJSON` を指定する。

結果：PASS（既存46実行ファイル＋Sketchパネルfixture、追加Async 7モード、Python 13件）。最終回帰はソースを固定して実行し、全件成功。

検証範囲：

- A／D／E1 × rows／fullの6条件。実LUT105B、電圧byte、更新前後の面・byte数、単一activation、Shadow一致、範囲外固定画素、R0／R2。
- 既存の保持／交換の2つの明示RAMモデルで、表示時のNew／Old一致と更新後の両面一致を検査。これは実Controllerの内部RAM挙動をReadbackで証明したものではない。
- rows→full→rows、冷間custom、未同期、32回周期、周期直前No-op、Sleep/Wake、OTP full／partial。
- fullでSketch A例外、明示Sketch D、E1を維持。fault時は選択を拒否し、自動retry・追加refreshをしない。
- 実Async workerでBUSY中のrows↔full切替、post-sync／LUT restore待ち、Pendingの画像・元App・E1選択維持、余分なactivationなし、fullでLUT復元失敗時のPending停止。
- 本体／USB Shellで短い1行応答、4種類LUTとの独立性、引数・IOの拒否、setterのSPI送信なし。
- 既存Writer／Sketch／Launcher／Files／Reader／Clock／TypeMouse／Touch／BLE／Asyncの回帰。

Host結果・Build結果・Commit・LUT hashは [検証manifest](evidence/ram-path-lut-20261021/manifest.json) に記録する。Windowsで既存standalone runnerがCRLFにより最初に停止したため、該当スクリプトにLF指定を追加して再実行した。光学評価のPASSとはしない。

付属テキストログは行末空白のみ正規化したもの。未加工の原本はローカル `.research/ram-path-lut-trial/` に保持する。

## E. Build・成果物

既存入口 `scripts/build_papermono_standalone_lut.sh` の内容は維持する（LF指定のみ追加）。Linux／WSLの既存PlatformIO環境用で、Buildのみ行いUploadはしない。Windows親PCでは同じPaperMono／writerdeck／OTA設定でPlatformIO Buildを実行する。

ESP-IDF 5.5.5、GCC 14.2.0+20260121。主要設定はWriter D／Dirty hold／SPI20／Hybridコンパイル有効、waveform trial／Sketch latency trial／Sketch LUT専用パネル無効。Hybridコンパイル有効と実際の `refresh=hybrid` 選択は別であり、今回の比較はfastestを使う。Display Async・fastest+tileを維持する。

最終BuildはPASS（12.11秒。初回Build約300秒）。Applicationは **3,465,104 B**、各OTA容量は **7,340,032 B**。

SHA-256：`a4cd6d82dd28978fbf35421bb204892576865a1848a7284a4199be615cc95a7c`。

両OTAの容量・実装Commit・有効Compile Flagsは検証manifest参照。参照用Partition TableはFlash用ではない。Firmware／ELFはローカル `.research/ram-path-lut-trial/` に保持し、個人の全Flash・文書・bondデータはGitへ追加しない。

## F. Gitと作業保護

Remoteは `origin=https://github.com/taichan1113/solar_os.git`。実験Branchだけへ通常Pushする。main／Integrationへmergeせず、upstreamへPushしない。

元の36 WorktreeのBranch・HEAD・status・未コミット内容のhashを確認する。親PCの未追跡 `.vscode/` と `doc/papermono-chatgpt-handoff-20261021.md` も保持する。無関係な上流変更・競合解消は混ぜない。

## G. Flash後にユーザーが行う定性比較

**現段階では未Flash。COM39は候補であり、別途Flash指示が出た時点で実機・port・partition・起動slotをfresh確認する。** 全Flash backup、Application-only、読戻し、NVS／FAT／BLE bond／otadata等の非対象領域照合を行う。標準Uploadによる書込みは自動実行しない。

Flash後の本体Shell操作は以下。各行は別の比較条件を表し、一括実行用scriptではない。

| LUT | RAM | 本体Shell操作 | 注意 |
|---|---|---|---|
| A | rows-dual | `lut 1` → `ram rows` | 全App A |
| A | full | `lut 1` → `ram full` | 全App A |
| D | rows-dual | `lut 2` → `ram rows` | SketchもDで比べる時だけ `lut 3` |
| D | full | `lut 2` → `ram full` | SketchもDで比べる時だけ `lut 3` |
| E1 | rows-dual | `lut 4` → `ram rows` | Sketch含む |
| E1 | full | `lut 4` → `ram full` | Sketch含む |

同じApp・フォントサイズ・似た入力操作で、黒画素が少ない画面／多い画面を見比べる。既存黒の薄まり、白化、新しい字の黒さ、Backspace残像、タイピング／手書き追従性を観察する。今回黒画素数や濃度の数値計測はしない。

`ram status` は1行。既存 `lut status` やTraceは多行出力を伴い、Shellの描画履歴・スクロールを変え得るため、毎入力で自動表示しない。切替だけではOTP cleanupを行わず、直前画面の履歴が残る。独立比較の初期化条件はユーザーの実使用記録と区別する。

通常設定に戻すには `ram rows` → `lut 2`。急激な悪化時は長時間試験を続けない。faultで切替が拒否された場合は、繰返しrefreshで押し切らず、既存の復旧／Application-only rollback手順を用いる。

## H. 未確認事項・解釈

full-RAMでの実機画質、黒画素量と白化の相関、高速入力時の黒保持、物理更新間隔の寄与、Dirty Rectangleの光学影響、長期パネル影響はいずれも未確認。

fullで改善してもDirty Rectangleの不具合が確定するわけではない。転送量が増えると物理更新までの自然な時間も変わるため、RAM窓・整定時間・描画履歴・駆動負荷をまだ分離できない。今回は6条件を実運用で選べる状態を完成させる作業であり、原因確定や波形採用判断ではない。
