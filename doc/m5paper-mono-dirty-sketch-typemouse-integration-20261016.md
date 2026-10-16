# Dirty Rectangle＋Sketch高速化＋TypeMouse長押し固定 統合（2026-10-16）

## 統合内容

既存の `integration/papermono-dirty-sketch` に、実機確認済みのTypeMouse変更 `d93f23ded692fbf5498507d2e6f7374e7df58770` を `git cherry-pick -x` で取り込んだ。競合なし。新たなアプリ実装やDriver修正は行っていない。

| 項目 | 内容 |
|---|---|
| Branch | `integration/papermono-dirty-sketch` |
| Worktree | `E:\ESP32\M5PapserMono-SolarOS\.research\dirty-sketch-integration\worktree` |
| 取り込み前 | `460cef3a192ce1b317e24534a51fe80b530405f1` |
| TypeMouse取り込みCommit | `5c1ff064a9d2bdc92780f61b60bc7acd6c21fa5c` |
| 元Commit | `d93f23ded692fbf5498507d2e6f7374e7df58770` |

Dirty Rectangleのrows-dualと32回周期の全RAM再同期、Sketchの局所描画・最終線分反映に、以下のTypeMouse操作を加えた。

- Shift／Ctrl／Fn／Sym／Nav：単独で約1秒長押しし、通常離指すると固定。同じキーの再長押し・離指で解除。
- 短押し機能と、従来の2点同時操作による一時的な修飾・レイヤー切替を維持。
- 42キーは接触中に黒地・白文字へ反転し、通常離指で復帰。固定キーは反転を維持し、状態表示に `*` を付ける。
- CANCEL、BLE切断、モード切替、suspend、終了時には固定・押下状態を解放。

## 差分と互換性

取り込みで変更した本体コードは次の3ファイルだけ。

- `src/apps/solar_os_type_mouse.c`
- `src/apps/solar_os_type_mouse_engine.cpp`
- `src/apps/solar_os_type_mouse_engine.h`

加えて既存TypeMouseのHostテスト2本と `doc/manual/apps.md` を取り込んだ。`src` 全体は前回実機確認済みの `d93f23de` と完全一致する。今回のIntegration独自の追加内容は検証用テストと文書である。

Driver、Display／Input／BLE Service、Sketch、Writer、Launcher、Shell、Board、partition、PlatformIO設定は、取り込み前の `460cef3a` と一致する。LUT、電圧、SPI、物理更新シーケンスを変更していない。TypeMouseの反転表示に伴う画面更新は取り込んだ既存仕様であり、通常キーで表示更新しない旧TypeMouseとは異なる。

Dirty最適化の適用条件は引き続き `refresh=fastest` ＋ `fastest-path=tile`。Sketch高速化とDirtyの有効条件は別であり、`typing`／`hybrid`でDirty最適化が働くとは扱わない。

## 検証

`tests/host/run_dirty_sketch_integration.sh` のHost実行ファイル19本とPython unittest 9件がすべてPASS。取り込んだTypeMouseテストには長押し固定・解除、近接キー操作を想定した離指後入力、押下／固定反転、CANCEL・切断・suspendでの解放を含む。既存のDirty／Sketch連携、32回境界、OFF／ON、Writer／Launcher／Shell回帰テストもPASSした。

ログ：親ディレクトリの `host-tests-typemouse.log`。

ESP32-S3のcompile/linkもPASS（103.12秒）。ビルドログ中の `warning:`／`error:` は0件。

| ビルド項目 | 結果 |
|---|---|
| SDK | ESP-IDF 5.5.5、Xtensa GCC 14.2.0 |
| 構成 | `m5paper_mono`／`writerdeck`／`ota`、16MB Flash、UI 140% |
| Flag | Dirty=1、SPI20=1、Hybrid=1、Waveform=0、Sketch計測=0 |
| Sketch局所描画 | 既存の `SOLAR_OS_BOARD_SKETCH_ASYNC_PRESENT=1` を維持 |
| Application | 3,455,712 B、OTA枠7,340,032 B以内 |
| SHA-256 | `278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6` |
| Application-only成果物 | 親ディレクトリの `artifacts-typemouse/firmware-app-only.bin` |
| 設定・照合記録 | 同 `manifest.json` と `build-selection.txt` |
| ビルドログ | 親ディレクトリの `build-typemouse.log` |

上記Flagは機能のコンパイル有無。実行時にHybridを自動選択した意味ではなく、今回実機のrefresh設定や保存startupを変更していない。基準LUT 110バイトのSHA-256は `e8a7abbb2110cb8d941e80cc2869bbe1d0814e1d4a38e5d0cc5173b10f39ce50` のまま。

前の候補の `artifacts/` は残し、今回の成果物は別ディレクトリへ保存した。ソース照合・ビルド設定・オフラインpartition・容量・ハッシュの監査スクリプトは親の `audit_typemouse_candidate.py`。オフラインpartition情報だけで実機の書込み先を確定しない。

## 実機の扱い

今回、実機接続、Flash読取り・書込み、起動設定変更、NVS／FAT操作、実用Branchへのマージは行っていない。現在の実機Firmwareは変更していない。

元のTypeMouse変更はすでに実機で長押し固定、近接キー入力、再長押し解除、キー反転、Launcher復帰を確認済み。ただし、このIntegrationのビルド成果物自体を実機検証済みとは扱わない。

次回の実機確認は既存のApplication-only手順を使用する。実機・partition・起動slotをその都度確認し、直前のFirmwareをバックアップしてNVS／FAT／otadata／bond等を保護する。確認操作はLauncher→TypeMouse→A入力→Shift固定と解除→Mouse移動→Exit、およびSketchの短い線と離指後の末端確認とする。既存の詳細な保護・Rollback手順は初回統合報告のF節を参照。
