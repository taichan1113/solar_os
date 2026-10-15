# PaperMono Launcher / TypeMouse 実装結果

実施日: 2026-10-15 (JST)。作業先: `E:\ESP32\SolarOS-Launcher`、ブランチ: `feature/papermono-launcher-typemouse`、開始HEAD: `29b2e236a70ce677758e47bc023ccc3734a8c819`。開始時の未追跡 `.vscode/` は変更していない。

## 実装

- PaperMonoでLauncher packageが有効なら本体の起動画面をLauncherにする。起動失敗時はShellへ戻し、表示なしのheadless Shell分岐を維持する。USB Shellのサービス・接続経路は変更していない。
- Launcherは `touch0` のexpansion enable leaseを開始・復帰時に取得し、中断・停止・state破棄時に解放する。取得失敗を開始エラー／復帰終了として通知する。
- TypeMouse packageが有効な場合、設定解析後にメモリ上だけでKeyboardアイコン付きTypeMouseを補う。空きセルを使い、満杯なら行または列を増やす。JSONの32項目上限を維持し、実行時の追加分だけ33項目を許容する。
- 既存 `launcher.json` は書き換えない。既存TypeMouse項目は重複追加せず、名前・コマンド・位置を保ちながら描画アイコンをKeyboardにする。新規ファイルの従来デフォルトにも同じメモリ上の補完を適用する。
- Keyboardは共有GFXの図形で描画する。enum末尾への追加により既存223個のアイコン番号を保持する。EPD driver、LUT、SPI設定、表示更新方式は変更していない。
- タップ起動は既存pointer配送と `SOLAR_OS_LAUNCH_CHILD_RETURN` を使用する。TypeMouseのタッチExitとHID解放処理を維持し、中断時にはExitのタッチ待ち状態もクリアする。Session/InputのCANCEL・generation・全離指gateは変更していない。

## Host検証

変更前に `input_touch_service_test` (26ケース)、`input_touch_adapter_test` (7ケース)、`launcher_layout_test` を実行しPASS。実機でShellから既存Launcherを起動する確認は未実施で、Hostによる配送経路確認を代用した。

最終状態で次の17 HostテストがPASS:

```text
launcher_touch_test          type_mouse_lifecycle_test
type_mouse_engine_test       launcher_layout_test
gfx_icons_test               input_touch_service_test
input_touch_adapter_test     writer_buffer_test
shell_startup_policy_test    shell_launch_test
input_test                   ft6336_frame_test
input_touch_core_test        ft6336_lifecycle_test
ble_keyboard_reconnect_test  sketch_canvas_test
ble_hid_test
```

追加Launcherテストは実アプリの処理と実JSON parserを使い、64通りのグリッド、32項目、独自TypeMouse項目、lease失敗・解放、子アプリ起動、既存JSONのバイト保持、設定不正時の終了要求、開始・復帰を検証する。TypeMouse lifecycleテストは実アプリと実engineにBLE等のHost fixtureを組み合わせ、未接続起動、タッチExit、CANCEL、中断・復帰、HID解放を検証する。無線通信や実機の完全なアプリ遷移を検証するものではない。

Pythonは `test_launcher_app.py` (10件)、`test_script_gfx_icons.py` (2件)、`test_papermono_sdkconfig.py` (5件)、`test_flavor_packages.py` (42件) がPASS。`git diff --check` もPASS。

検証上の未解消項目:

- `test_foreground_app_state.py`: 4件中1件が既存の `src/apps/solar_os_sketch.c:25` の `sketch_canvas_version` をeager mutable stateとして検出してFAIL。このファイルは本作業で変更していない。
- `writer_checkpoint_test`: Windows/MinGWに `fsync` と `mkdtemp` がないため既存テストをコンパイルできず、未実施。Host全テスト一括PASSとはしていない。

Host compilerはQMK_MSYSのGCC/G++ 13.2.0。LauncherテストはSDKのcJSONを使用するため、再実行時に必要ならパスを指定する:

```text
make -C tests/host test-launcher-touch CC=gcc CJSON_DIR=C:/Users/takec/.platformio/packages/framework-espidf/components/json/cJSON
make -C tests/host type_mouse_engine_test type_mouse_lifecycle_test CC=gcc CXX=g++
```

## PaperMono build/link

このWorktreeの `.pio` にて最終ビルドSUCCESS。他Worktreeのビルド出力は再利用していない。PlatformIOの共有ツール・SDKを使用した。

```powershell
$env:SOLAR_OS_BOARD='m5paper_mono'
$env:SOLAR_OS_FLAVOR='writerdeck'
$env:SOLAR_OS_LAYOUT='ota'
& C:/Users/takec/.platformio/penv/Scripts/pio.exe run -e esp32_s3_devkitc1_n16r8
```

生成設定・CMakeCacheを照合し `m5paper_mono / writerdeck / ota`、pointer機器 `touch0`、orientation 270、Launcher/TypeMouse/Writer/Sketch/Shell/BLE/OTA有効、waveform trial無効を確認した。

- RAM: 53,144 / 327,680 bytes (16.2%)
- Flash: 3,443,723 / 7,340,032 bytes (46.9%)
- 出力: `.pio/build/esp32_s3_devkitc1_n16r8/firmware.elf`、`firmware.bin`、`firmware.factory.bin`
- `firmware.bin` SHA256: `AC7FFFD4F1E2F0F782C6B46B6EF1DAB34A675B4D4B653BC21C0476EE17446FCB`
- 最終ログ: `.pio/launcher-build.log`

## 実機確認待ち

Flash、元Worktreeへのmerge、GitHub pushは行っていない。NVS/FAT、BLE bond、保存文書にはアクセスしていない。

別途Flash指示後に、ZMK OFFでのLauncher起動→TypeMouseタッチ起動→Exitタッチ復帰、BLE HID未接続表示、USB Shell応答、Writer/Shell/Sketch回帰、切替直後の残留タッチ・押下を確認する。これらの実機受入項目は未実施でありPASSとはしていない。
