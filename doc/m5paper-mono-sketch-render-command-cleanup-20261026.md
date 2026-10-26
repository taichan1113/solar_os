# PaperMono Sketch描画方式比較・旧実験コマンド整理

作業日：2026-10-27 JST。ファイル名は依頼書の指定を使用。

## 結論

Sketch候補は依頼書の **B（旧全画面描画のみ）**。過去の局所描画高速化は現行版に取り込み済みで、別の高速化候補は見つからなかった。Firmwareへ遅い第二方式や形式的な`sketch-render`切替を追加せず、既存の全画面fallbackと局所経路のHost比較を追加した。現行局所描画・F適用を維持する。

ShellはF既定にHelp/Usage/Statusを合わせた。旧aliasは非表示・非推奨の互換入口として残し、A/F復旧、Trace、OTP cleanup、Async診断を保持。LUT・Driverの更新処理や選択条件は変更していない。

実機Flash、COMアクセス、startup/NVS変更、SD書込み、Integration/mainへのMergeは実施していない。本体のFirmwareは変更していない。

## A. 候補の同定

| Candidate / Branch | 調査したHEAD | Canvas/GFX・現行との差 | 比較価値 |
|---|---|---|---|
| `feature/papermono-sketch` | `7bccc2657bc640395aa3c4ac847e7da4ac254590` | 毎renderでCanvas全域XBM＋全画面GFX/UI描画。owned touch lease・Async submitは既存 | B。局所化前のCPU処理量の基準 |
| `experiment/papermono-sketch-latency-measurement` | `31abd6db216bd94833ae092f6948438f8f1e4256` | 上記とCanvas実装blob一致。journal計測点を追加したもの | 独立した描画方式ではない |
| `feature/papermono-sketch-latency-live` | `82d20c242897187366b7bdbb255c97e1a0ae0589` | brush領域XBM、局所GFX、同一MOVE抑制、RELEASE末端処理 | 現行に取込済み。再実装不要 |
| `feature/papermono-sketch-d-diagnostics` | `aacd0f819d2b5e008f0029c98ddd44ceb49d6c39` | 上記と局所Canvas実装blob一致。波形・診断の追加 | 描画アルゴリズムの候補ではない |
| `integration/papermono-dirty-sketch` | `aa08bbfdfa65c72ada0a0bc7737d36b47a521186` | 上記とCanvas/App blob一致。後続に色表示対応・波形診断・CANCEL修正など | 現行局所描画の祖先 |
| 今回の基点 | `2720779c7c58899bb673a7f2a33c5a20306727ac` | 実装Commit `587680765b851dde2db35b0adc40958e092f6e4f` を含むAcceptance報告HEAD | FIX-01修正を継承して作業 |

局所描画導入`0b302eeb05f05f62f6a4c1f0958af4e685544266`と統合側`fbbb7475d8eacdddd3f94c5c05698d1dc3ca45b6`のSketch App/Canvas差分は空。Canvasの局所版blobは`2c9838bb0d19b578f7ab616c4867ff27b8264e32`。RELEASE末端修正は`82d20c24`と統合側`23adf310`で追跡できる。`31abd6db`の計測準備報告は[Phase 1](m5paper-mono-sketch-latency-phase1.md)であり、高速化の実装報告とは区別した。

現在のコード経路：

- `src/apps/solar_os_sketch.c:808` `sketch_render_paint()`：brushの未反映領域をXBM変換し、白fill＋黒bitmap、既存Async submit。成功後だけpendingを解除。
- 同`:867` `sketch_render()`：UI変更、modal、shape preview、relative pointer、非MONOなどは既存の全画面fallback。最初のdirty印更新も全画面経路。
- `src/apps/solar_os_sketch_canvas.c:339`：regionの絶対座標でBayer位相を維持。`:377`付近の全域変換も同関数へ委譲。
- `src/apps/solar_os_sketch.c:1169`：RELEASEの異なる終点をCanvasへ反映。同座標MOVE/RELEASEは余計な描画要求を出さない。Canvasに複数MOVEを反映してからTickでまとめて提出する。
- 同`:1522–1595`：既存CANCEL処理とlease保持。FIX-01のSave通知OK/Esc後の継続を維持。
- 保存は同じ論理2bpp Canvasから既存PNG経路を使用。局所化でPNG形式や画素内容を変更しない。

Snapshotは両経路とも既存GFX/Display Asyncのimmutable提出を使用。CPUの描画範囲を局所化しても、snapshot全体のコピー量やSSD1677の物理駆動範囲が同率で減るとは限らない。RAM rows-dualの書込み範囲と物理的更新範囲も別物である。

## B. Host比較

追加：`tests/host/sketch_render_compare_test.c`。実Sketch callbacks/Canvasを既存lifecycle fixtureで動かす。旧方式を表す比較側には**現在もある全画面fallbackを毎回要求**する。古いFirmware全体を再ビルドした比較ではなく、色表示など後続変更を共通化して全域対局所の差だけを見る。

同じ800×480配置、736×392 Canvas、4 stroke、各stroke内8 MOVEを2件ずつ集約し、異なるRELEASE終点まで描く。1/8px brush、3階調、消しゴム、byte未整列の領域を含む。各frameのCanvas全バイト一致と、局所XBMを前frameへ合成した**画面Canvas全域**の全画素一致を検証する。

| 実Appの24描画（初回App起動描画は除外） | 全画面fallback | 現行local |
|---|---:|---:|
| submit回数 | 24 | 24 |
| GFX全画面clear | 24 | 1（dirty印変更） |
| XBM/bitmap対象画素の累計 | 6,924,288 | 300,939 |
| XBM出力byte累計 | 865,536 | 37,771 |
| 永続Canvas / preview / XBM buffer | 72,128 / 72,128 / 36,064 B | 同左 |
| Canvas・合成XBM・最終線分 | PASS | PASS |

XBM出力量はこの入力で約95.6%減少。**SPI転送byte数やEPD更新回数の削減率ではない**。同じ3個の永続bufferを使い、メモリ削減はしていない。

別のCPU小計測（WSL/Linux x86_64 GCC、`-O2`、実Canvas変換関数、各200回、同じ階調Canvas）：

| 処理 | 平均Host時間 |
|---|---:|
| 全域736×392 XBM | 343.709 µs/回 |
| 局所53×19 XBM（座標7,9） | 1.332 µs/回 |
| Canvas線分53×18、太さ8、2,000回 | 7.012 µs/回 |

これは単一runのCPU wall-clock平均。後者のROIは小計測用で、上の24描画の各領域と同一ではない。GFX bitmap/UI描画時間はstubのため評価していない。ESP32のCPU時間、queue待ち、SPI、BUSY、実0x20回数、入力から光学変化までの遅延、黒品質は今回**未測定**。Host値から実機速度を推定しない。

Firmwareでの方式切替は追加しない。今後、独立候補が見つかった場合にRAM-only切替を検討する。現行方式の維持であり、新方式の本採用判定ではない。

## C. Shellコマンド台帳

以下は基点ソースの実在する入口を確認した結果。`display test display0`を以下`DT`と略す。

| コマンド・設定 | 区分 | 旧状態 → 今回の状態・理由 |
|---|---|---|
| `DT fastest-lut status` / `trace` | KEEP | 既存維持。Aのscopeを正しくall-apps表示し、physical内訳にFを明示。読出しで更新を起こさない |
| `DT fastest-lut baseline` / `disable` | KEEP | Aを全体で選択する既存RAM-only復旧。`disable`はF/Dを無効化するaliasで、表示サービスを停止する意味ではない |
| `DT fastest-lut candidate_f` | KEEP | Fへの復帰。Driverの20MHz/tile/rows-dual等の前提チェックは変更しない |
| `DT writer-lut …` | HIDE | 動作互換は維持。通常Helpから旧名を外し、呼ばれたときに`legacy alias; use fastest-lut`を表示。調査した追跡対象`scripts/`に旧名利用なし。本体上のユーザースクリプトは調査していないため削除しない |
| `DT fastest-lut candidate_d` | EXPERIMENTAL | default-Fでは既存Driverが拒否する。通常PaperMono Helpに出さず、呼出時にA/Fの利用を案内。非PaperMono/legacy設定では既存D選択を残しHelpにも記載 |
| `display mode display0 refresh=cleanup`（full/partial/auto含む） | KEEP | OTP復旧・通常運用。変更なし |
| `display mode` / `display list` / `layouts`等 | KEEP | Controller mode表示・設定、表示status、layout管理を維持 |
| `display async <target> [wait]` | KEEP | 既存診断・待機を維持し、欠けていたusage行を追加。`wait`は明示的な待機であり、非待機statusと区別 |
| `refresh=fastest`、`timing`、`spi-mhz`、`fastest-path`、`dirty-window` | KEEP（詳細診断） | parser/候補一覧とも変更なし。rows-dual等の既定や32回同期を維持。診断選択肢を削って復旧・他Board互換性を壊さない |
| `refresh=typing`、`typing-frame-ms/cleanup-updates/idle-ms` | KEEP（既存詳細モード） | 通常Buildでも存在する旧pacing機能。今回は選択・処理・補完を変えず、既定pacing OFFを維持。通常のfastest設定と混同しない |
| `fastest-waveform=baseline/dose/hold` | EXPERIMENTAL | 既にWAVEFORM_TRIALのみ。通常Buildは0で非公開・選択不可。追加削除不要 |
| `refresh=hybrid`、`hybrid-idle-ms/max-updates` | EXPERIMENTAL | 既にHYBRID_TRIALのみ。通常Buildは0 |
| `DT ab-a/ab-b baseline/dose`、`DT writer-quality …` | EXPERIMENTAL | 既にWAVEFORM_TRIALのguard内。通常Buildに強制反復を追加しない |
| `sketchtrace start/stop/status/dump …` | EXPERIMENTAL | 既にSKETCH_LATENCY_TRIALのguard内。通常Buildは0 |
| `launcher`、`shell`、Files、`wifi`、`ntp` | KEEP | 通常操作と手動同期を維持。Wi-Fi/NTP自動起動方針は変更しない |
| REMOVE対象 | 該当なし | 安全に未使用と確定できる実在コマンドなし。大量削除しない |

E1、Candidate B/C/Dの単発・反復試験、Voltage Hold等の**独立した名前付きコマンドはこの基点の通常Shellにはない**。過去の別Branchを現在の削除対象として数えない。`lut 5`も追跡対象の組込みShell command登録には見つからず、本体上のscript等は今回未調査。

Helpの「All apps except Sketch default to D; Sketch uses A」を除去し、PaperMonoでは「F defaults to all apps」とA/F操作を表示。Sketchのsubmit関数に残る古いBaseline専用コメントも、source identityの説明へ訂正しただけで挙動変更はない。

根拠：`src/shell/solar_os_shell_commands.c:275,650,663,787`、`solar_os_shell_writer_lut.inc:2,17`、`src/drivers/epd_ssd1677_writer_d.inc:108`、`epd_ssd1677.c:1176`以降のmode値/parser、`src/apps/solar_os_shell.c:515,543,3862,6605`。Shell補完はDriver mode値を使う既存経路で、今回変更なし。他Board向けcommand parserを狭めていない。

## D. Git・検証・Build

- Worktree：`E:\ESP32\M5PapserMono-SolarOS\.research\sketch-render-command-cleanup\worktree`
- Branch：`feature/papermono-sketch-render-command-cleanup`
- Base：`2720779c7c58899bb673a7f2a33c5a20306727ac`
- `c1353eea723703d8c07a1d3470ceeec67178130d`：既存SD Host fixtureが必要とする`tests/host/sd_card_stubs/sdkconfig.h`を追跡。旧Worktreeだけにignoredファイルとして存在し、新規checkoutでテストがコンパイルできなかったため。SD Firmwareの変更ではない。
- `66c2d40c631eeaf54834d90fa372f4939a22b1ee`：Shell Help/Status/alias。
- `0f59dfb4cd989a387684c656029bfef142dbb431`：Sketch Host比較、Productionへの比較・PaperMono Help分岐テスト追加、古いSketchコメント訂正。

Host：Production **32 binaries＋Async追加3条件 PASS**。追加関連6 binaries（Sketch lifecycle / Baseline source / legacy synchronous / touch projection、expansion registry、FT6336 lifecycle）PASS。PythonはBoard manifests / manual generation / Wi-Fi TUIの**52 tests PASS**。

ShellテストはPaperMono・legacy両分岐の実Help関数、旧alias、A/D/F、status/traceを実Driverと接続し、成功・拒否を含む全操作でSPI command発行0を確認。Productionの実Driver/Executor試験で他App/Writer/SketchのF、A/F復帰、restore失敗停止を継続検証した。Sketch FIX-01のSave通知OK/Esc、CANCEL、再入場、Sleep/Wake、lease retryもPASS。SD保存FAILは今回修正しておらず、Host PASSを実カードPASSと扱わない。

再現コマンド（WSL、makeに加え既存FatFS/cJSONソースが必要）：

```sh
make -C tests/host -j4 \
  FATFS_DIR=/mnt/c/Users/takec/.platformio/packages/framework-espidf/components/fatfs \
  CJSON_DIR=/mnt/c/Users/takec/.platformio/packages/framework-espidf/components/json/cJSON \
  test-production
python3 -m unittest tests.test_board_manifests tests.test_generate_manual tests.test_wifi_tui_scan_flow
```

Firmware Build **SUCCESS**（225.52秒）。既存`scripts/build_papermono_production.ps1`を使用。PowerShellの実行ポリシーにより直接実行は拒否されたため、`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build_papermono_production.ps1`で当該プロセスだけ実行した。システムの実行ポリシーやSDKは変更していない。

環境：PlatformIO `esp32_s3_devkitc1_n16r8`、pioarduino 55.3.312、ESP-IDF **5.5.5**、Xtensa GCC **14.2.0+20260121**、Board `m5paper_mono` / Flavor `writerdeck` / Layout `ota`。

Build flags：`WRITER_D=1`、`SPI20_TRIAL=1`、`DIRTY_HOLD_TRIAL=1`、`HYBRID_TRIAL=0`、`WAVEFORM_TRIAL=0`、`SKETCH_LATENCY_TRIAL=0`（先頭の`SOLAR_OS_SSD1677_`、Sketchのみ`SOLAR_OS_`を略記）。古い名前のflagは現行F/rows-dual経路でも必要なため改名・削除しない。

| 成果物 | Size | SHA-256 |
|---|---:|---|
| `firmware.bin` | 3,520,464 B | `cfeac6d8a33a2bd859aabda9d36096ad96124fa29756d3430ab64354226d0b64` |
| `firmware.elf` | 30,985,920 B | `b1d94ad6fd03a6ed18206803efed4a3df293256242ead6f30f46f2ad792ca5b8` |
| `partitions.bin` | 3,072 B | `fd976d7c1eaa8f9d0a01ac87a8d05fc1cb1e59d3c5970954b30ad0c3aa12a488` |

Firmware絶対パス：

`E:\ESP32\M5PapserMono-SolarOS\.research\sketch-render-command-cleanup\artifacts\0f59dfb4\firmware.bin`

同じarchiveにELF、partition比較用binary、`host-production.log`、`python-tests.log`、`firmware-build.log`を保存。Worktree内の元出力は`.pio/build/esp32_s3_devkitc1_n16r8/firmware.bin`。コードCommit **`0f59dfb4`** でBuildし、その後の変更は報告・台帳のみ。archiveはローカルのignored領域で、Gitには報告書とテスト/ソースをPushする。

リンク時RAM 55,400 / 327,680 B、Flash 3,514,447 / 7,340,032 B。partition binaryは基点の既知SHAと一致し、layout/Bootloaderソースも変更なし。Build時にfactory結合imageも自動生成されるが使用していない。生成App binary内の`F defaults to all apps`を確認し、古い`All apps except Sketch default to D`とlegacy用D選択Help文字列がないことも確認した。

元Worktreeは`integration/papermono-dirty-sketch` / `aa08bbfdfa65c72ada0a0bc7737d36b47a521186`のまま。元の`.vscode/`と4件の未追跡文書一覧も維持。Driver/Service/Board/Build scriptの基点との差分は空、Sketch Appはコメント訂正だけ。作業Branchだけをoriginへ通常Pushし、Merge/force-pushはしない。

## E. 次のAcceptance（今回は未実施）

将来、別途Flash承認を受けた場合の限定手順：

1. Application-only手順で旧Appをバックアップし、対象slot/保護領域を照合して投入。Launcher、F、fastest/tile/20MHz/rows-dual/Async/pacing OFFを確認。
2. Shellの`display` usageでF既定・正規fastest-lut名・async行を確認。`DT fastest-lut status`→`baseline`→`status`→`candidate_f`→`trace`でA/Fとscopeを確認。診断読出し自体はRefresh要求を出さないが、画面Shellに文字を表示する描画や既にqueueにある描画と区別する。OTP cleanupは必要時に明示実行。
3. Sketchで短線・長線・ジグザグ・曲線・連続筆記・離指末端・消しゴムを各少数回。今回device上の比較selectorはないため、local内での回帰確認とする。旧方式の実機比較が必要なら別途目的を明確にして用意する。
4. 新規PNGでSave→OKタップ→描画→Save→Esc→描画。終了/再入場、再読込、Sleep/Wake後の初回タッチを確認。既存PNGを試験で上書きしない。
5. Writer保存/Checkpoint、TypeMouse固定・反転/HID、Manual touch、Launcher、USB Shellを短く回帰確認。
6. 入力→描画要求、CPU、queue、SPI/BUSY/実0x20、光学的な線の出現は、それぞれ別に測定する。Host値で埋めない。

旧Firmwareに対する機能差はShellの説明・診断出力・旧alias案内のみ。Sketchの操作・描画方式・画像保存は同じ。実機の黒品質、追従性、パネル寿命の新たな評価結果はない。

startupの4行削除とreboot正常はユーザー報告として完了扱い。本体ファイルの確認・編集はしていない。USB Shell行等それ以外のstartup内容が変更済みとは仮定しない。
