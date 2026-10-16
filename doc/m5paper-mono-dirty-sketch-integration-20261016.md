# PaperMono Dirty Rectangle＋Sketch高速化 統合候補（2026-10-16）

## 判定

**統合成功・実機評価へ進める。** Host 19本とPython 9件がPASSし、ESP-IDF 5.5.5のESP32-S3ビルドが成功した。今回、COMポート・実機Flashへのアクセス、実機状態変更、実用Branchへのマージは実施していない。実機の描画品質、追従速度、黒濃度、長期安定性は今回未評価。

**この候補は指定されたDirty基点＋Sketch 2コミットの範囲であり、直前に実機確認したTypeMouse長押しトグル・キー反転 `d93f23de` は含まない。** 同変更は専用Branchで保護されている。現在の実機を置き換えて常用する前には、この機能差を解消するか、評価中はその機能がないことを承認する必要がある。今回の旧TypeMouseコードはDirty基点から変更していない。

## A. Git統合

| 項目 | 結果 |
|---|---|
| Branch | `integration/papermono-dirty-sketch` |
| Worktree | `E:\ESP32\M5PapserMono-SolarOS\.research\dirty-sketch-integration\worktree` |
| 指定基点 | `6e26aee95bda3624360744651b0a558520210418` |
| Dirty Branchの実際の最新HEAD | `0a652729852dca910273b18809a01de162b34271`。基点との差は実機報告書1ファイルのみ |
| Dirty / Sketch Branchの共通祖先 | `0a652729852dca910273b18809a01de162b34271` |
| Sketch取り込み1 | `0b302eeb05f05f62f6a4c1f0958af4e685544266` → `fbbb7475d8eacdddd3f94c5c05698d1dc3ca45b6` |
| Sketch取り込み2 | `82d20c242897187366b7bdbb255c97e1a0ae0589` → `23adf31052bae9a89bddcfb570c942ee5a97ccb7` |
| 検証対象HEAD／テスト追加 | `dcc9b98fa46e45e338df3aa4171ac6a7192e6c59` |
| 適用方法 | `git cherry-pick -x`、指定順、競合なし |
| 統合に必要な本体修正 | なし。追加Commitはテスト専用、後続に本報告書のみを記録 |

包含関係は `148ea7e8 → 6e26aee9 → 0a652729 → 0b302eeb → 82d20c24`。既存Sketch BranchにはDirtyの変更がすでに含まれていた。新候補では指定基点から2コミットだけを再構成した。

`git diff 82d20c24 -- src` は空。候補の本体コードはSketch元Branchと一致する。`git diff 6e26aee9 -- src/drivers src/services src/board include boards partitions.csv` も空で、Driver、LUT、VCOM、Gate/Source値、SPI、更新命令、BUSY、Dirtyアルゴリズムを変更していない。Writer / Launcher / TypeMouse / USB Shellにも本体差分はない。

開始時は元Worktreeに未追跡`.vscode/`、UI Worktreeに未追跡`hardware-20261016/`が存在した。対象元のDirty、Sketch、TypeMouseトグルWorktreeはclean。元Worktreeの変更を破棄せず、新候補専用の`.pio/build/esp32_s3_devkitc1_n16r8`を使用した。Branch／worktree一覧と状態は親ディレクトリの`worktrees-before.json`、`branches-before.txt`、`graph-before.txt`に保存。

## B. 連携経路と実装維持

実際の入力はFT6336G。Driverで取得したComplete FrameをInput ServiceとSessionの1点Pointer互換経路へ配送し、SketchがCanvas上の線分を更新する。

```text
FT6336G → Input / Session Pointer投影 → Sketch Canvas線分
 → 未反映領域の合併 → 局所XBM変換 → GFX / U8g2 FrameBuffer
 → gfx_submit → Display非同期Executorの不変Snapshot
 → SSD1677がSnapshot全体とShadowの差分を検出
 → rows-dual RAM転送 → 0x20を1回 → 両RAM同期 → Shadow確定
```

- `src/apps/solar_os_sketch.c:116`の`sketch_mark_paint()`は筆幅・端点の余白とCanvas境界を考慮し、未送信領域へ合併する。
- `sketch_render_paint()`（同`:778`）は絶対Pointer・MONO1・非モーダル等の条件で局所XBMを描画する。送信要求に失敗した場合は未反映領域を保持する。
- 同一座標MOVEは描画要求を追加せず、RELEASEが最後のMOVEと異なる場合は最終線分を追加する。初期表示、dirty見出しの初回変化、Toolbar、モーダル、図形、塗りつぶし、再開は従来の全体描画を維持。
- `src/apps/solar_os_sketch_canvas.c`の局所XBMは元Canvas座標でディザ位相を決定する。任意のX開始位置・幅のbit配置はCanvasテストで全体XBMと比較。
- DriverはSketchの矩形を直接受け取らず、保持された全FrameBufferの不変Snapshotから差分を再検出する。Sketch矩形とDriver矩形の二重クリップを行わない。
- `src/drivers/epd_ssd1677.c:873`周辺の起動時`rows-dual`、`dirty-window=off`は基点のまま。
- Dirty最適化の適用条件は`refresh=fastest`＋`fastest-path=tile`。`typing`や`hybrid`を選んだ状態は今回のDirty比較条件にならない。
- `src/drivers/epd_ssd1677_reference.inc:169`以降の32回周期再同期は、次の更新のRAM転送を全画面分にする。OTP fullは追加せず、`0x20`を追加発行しない。

## C. 実行テスト

以下はすべて実行PASS。ログは`E:\ESP32\M5PapserMono-SolarOS\.research\dirty-sketch-integration\host-tests-final.log`、終了コードは`host-result.json`（0）。

| 分類 | テスト |
|---|---|
| Dirty / SSD1677：4本 | `epd_ssd1677_dirty_hold_test`、`epd_ssd1677_test`、`epd_ssd1677_spi_clock_dirty_20_test`、`epd_ssd1677_hybrid_trial_test` |
| Sketch：7本 | `sketch_dirty_pipeline_test`、`sketch_canvas_test`、`sketch_lifecycle_test`、`sketch_scale140_lifecycle_test`、`sketch_legacy_lifecycle_test`、`sketch_touch_projection_test`、`sketch_trace_core_test` |
| Display / Writer：4本 | `display_async_test`、`writer_quality_test`、`writer_buffer_test`、`writer_files_test` |
| Launcher / TypeMouse / Shell：4本 | `launcher_touch_test`、`type_mouse_engine_test`、`type_mouse_lifecycle_test`、`shell_launch_test` |
| Python：9件 | `python3 -m unittest -v tests.test_sketch_trace` |

再実行（既存Ubuntu 24.04 / GCC、ESP-IDFのcJSONソースを使用）：

```powershell
wsl.exe -d Ubuntu-24.04 --cd /mnt/e/ESP32/M5PapserMono-SolarOS/.research/dirty-sketch-integration/worktree -- env CJSON_DIR=/mnt/c/Users/takec/.platformio/packages/framework-espidf/components/json/cJSON sh tests/host/run_dirty_sketch_integration.sh
```

追加テストは実Canvas→局所XBM→実GFX/U8g2→不変Snapshot→実SSD1677コードのSPI送信をつなぎ、全Canvas描画の参照FrameBufferおよび独立したR0/R2画素座標との一致を確認する。Controllerは「RAM保持」「更新後入替え」の2つの明示モデルを用い、どちらでも表示画像・Old/New・Shadow・両RAM面の整合性を確認。複数線分の集約、全画面を横切る太線、端での筆幅8、白による消去、無変更送信省略、Dirty OFF、OFF→ONの全面RAM再同期も通過した。

Sketch lifecycleには、同一MOVEと同位置RELEASEでの要求増加抑制、および筆幅8・Canvas端・RELEASE最終線分で「実際に変化した全画素が未反映領域内に入る」検査を追加した。既存の送信失敗→再試行、PNG保存・再読込・再開テストもPASS。

結合HostテストでのRAMデータ量（LUT・アドレス等は含まない）：

| 更新番号 | RAMデータ量 | 0x20回数 |
|---:|---:|---:|
| 31 | 1,200 B | 1 |
| 32 | 1,200 B | 1 |
| 33 | 192,000 B（全面再同期） | 1 |
| 64 | 1,200 B | 1 |
| 65 | 192,000 B（全面再同期） | 1 |

これは同じ小線分シナリオをR0/R2×RAM保持/入替えで実行した値であり、実機のSPI量・時間・速度改善率ではない。既存Host MVPが出力する80.1%削減値も今回のrows-dual実機値へ転用しない。

追加Commitの変更はテスト6ファイルのみ。GFXの既存INDEX8比較警告は既存Writer品質テストと同じ`-Wno-sign-compare`を結合テストのコンパイルだけに適用し、製品コードは変更していない。新ランナー1本のLF改行指定を追加し、既存3本の指定を保持した。

Hostの限界：実Controller内部のRAM面状態、実パネルの黒品質、タッチから光学変化までの遅延、実機上での長時間使用は保証しない。InputとDisplay Executorの非同期動作は既存の個別Hostテストで確認しており、結合テスト自体はSnapshotを逐次送信する。

## D. Firmware

| 項目 | 結果 |
|---|---|
| SDK / toolchain | ESP-IDF 5.5.5、PlatformIO Espressif32 55.3.312、Xtensa GCC 14.2.0 |
| 構成 | `m5paper_mono` / `writerdeck` / `ota`、16MB Flash、UI 140% |
| Trial flag | `SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL=1`、`SPI20_TRIAL=1`、`HYBRID_TRIAL=1`、`WAVEFORM_TRIAL=0` |
| Sketch | `SOLAR_OS_BOARD_SKETCH_ASYNC_PRESENT=1`。計測用`SOLAR_OS_SKETCH_LATENCY_TRIAL=0`でも局所描画は有効 |
| Build | compile/link成功、ログ中の`warning:`／`error:`は0件 |
| Applicationサイズ | 3,454,864 B |
| 各OTA枠 | 7,340,032 B、残り3,885,168 B |
| SHA-256 | `46c6a3a5d103b9d1c34e00cf90c84ffc6247e77e9040abe30639221f6f39cffb` |
| 元ビルド | Worktreeの`.pio/build/esp32_s3_devkitc1_n16r8/firmware.bin` |
| 保存した書込み候補 | `E:\ESP32\M5PapserMono-SolarOS\.research\dirty-sketch-integration\artifacts\firmware-app-only.bin` |
| 検証記録 | 同`artifacts/manifest.json`、親の`build.log`、`audit_candidate.py` |

compile_commandsと生成Board headerで実効定義を確認。baseline LUT 110 bytesのSHA-256は`e8a7abbb2110cb8d941e80cc2869bbe1d0814e1d4a38e5d0cc5173b10f39ce50`。Driverソース全体が基点と一致する。

オフラインPartition解析では、OTA0=`0x10000`、OTA1=`0x710000`、各`0x700000`、FAT=`0xe10000`から`0x1f0000`で16MB内に収まる。**今回は実機を読んでいないため、現在の起動slot・実機hash・実機Partitionの一致は未確認。** 書込みoffsetをこの表だけで決定しない。

再ビルドは当Worktreeで以下の環境変数を設定し、`python -m platformio run -e esp32_s3_devkitc1_n16r8`を使用する。

```text
SOLAR_OS_BOARD=m5paper_mono
SOLAR_OS_FLAVOR=writerdeck
SOLAR_OS_LAYOUT=ota
SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL=1
SOLAR_OS_SSD1677_SPI20_TRIAL=1
SOLAR_OS_SSD1677_HYBRID_TRIAL=1
SOLAR_OS_SSD1677_WAVEFORM_TRIAL=0
SOLAR_OS_SKETCH_LATENCY_TRIAL=0
```

## E. 次回の限定実機評価手順（今回未実行）

承認後、以下の順で短時間だけ確認する。異常な欠け・反転・BUSY timeoutが出たら入力を止めて画面とログを残し、追加試験へ自動移行しない。

1. 他セッションのFlash使用がないことと未保存編集の保存を確認。実機識別・Partition・実行/起動slot・現行アプリを読み取り、その時点のRollback用バックアップを作る。
2. TypeMouse `d93f23de`の機能差を解消または評価範囲として了承したうえで、検証したApplication領域だけへ候補を書き込む。LauncherとUSB Shellを確認。
3. USB Shellで以下の評価条件を選ぶ。保存startupは変更しない。

```text
display mode display0 refresh=fastest
display mode display0 fastest-path=tile
display mode display0 spi-mhz=20
display mode display0 dirty-window=rows-dual
display mode display0 timing=on
```

4. Writerの新規テスト文書で数文字を入力し、欠けと表示を確認。
5. Sketchで短い直線・ジグザグ・円を各1本、短い連続手書きを行い、離指時に最終線分まで出ることを確認。
6. Saveで自動保存された`/Sketches/sketchNNN.png`を特定し、再読込・再描画を確認。既存ファイルへ上書きしない。
7. Launcherへ戻る。`dirty-window=off`に切り替え、Sketchで同じ少数の操作を確認する。Sketch局所描画は維持され、Driverだけが全RAM転送になる。
8. `dirty-window=rows-dual`へ戻す。次のfastest更新が全面RAM同期になることと、その後の局所転送をログで確認。
9. WriterとTypeMouseの起動・入力、Mouseのポインター操作、Launcherへの復帰を確認。観察終了後は`timing=off`へ戻す。

32回境界は`REF ... ram_bytes=... 0x20=1`、`REF write... spi_us...`と`Dirty RAM dual: periodic full-RAM resync after 32 updates`で観察できる。起動からの描画もカウンタへ入るので、手書きイベント数をそのまま物理更新回数として数えない。無変更要求は0x20なし。自然な短い操作で境界へ届かない場合は未確認とし、境界確認だけのために高速更新を大量追加しない。境界のRAM整合性は今回のHostで検証済み。

## F. 将来のApplication-only保護・Rollback

今回、Flashアクセス・新しい実機バックアップ・Rollbackは実行していない。過去のArchiveは参照資料であり、次回も同じ実機状態だとは扱わない。

- COM39は候補にとどめ、接続port、USB識別、ESP32-S3、MAC、16MB容量、Partition table、実行/起動slotとOTA選択をその都度照合する。
- 実行中アプリの未保存編集を保存・正常終了し、その時点の全Flash／ApplicationとSHA-256を退避する。バックアップのPartitionから復元先を確定する。
- 候補は`firmware-app-only.bin`。factory image、Bootloader、Partition table、otadata初期イメージは書き込まない。
- NVS（bondを含む）／FAT（文書・recovery・startup等）／otadata／PHY／Bootloader／Partition tableを直前と書込み直後で比較し、Application読み戻しの全バイト一致を確認してから起動する。
- Rollbackも同様に、その評価直前に退避したApplicationだけを検証済みslotへ戻す。評価中に保存した文書・bond等は全Flash復元で巻き戻さず、当時の現在値を保持する。
- バックアップにはユーザーデータを含むためGit管理へ追加しない。今回はそのようなデータを統合Branchへ追加していない。

今回の候補は独立Branchに留める。黒品質・速度の定量改善と実用統合の可否は、今後の承認された実機評価後に判断する。
