# 本体Shellでの4モード比較

最新の実装・観察・復元と再書込み・未解決事項は[2026-10-21 引継ぎ総括](handoff-report-20261021.md)を参照。

Sketch専用パネルでは、WriterとSketchを同じ選択のまま比較できなかった。
本版は本体画面のShellとUSB Shellの両方に `lut` コマンドを追加する。

| 本体Shellで入力 | Writer等 | Sketch |
| --- | --- | --- |
| `lut 1` | A | A |
| `lut 2` | D | A |
| `lut 3` | D | D |
| `lut 4` | E1 | E1 |
| `lut` / `lut status` | 現在の状態を表示 | 選択を変更しない |

本体に接続したキーボードでShellから入力する。USBホスト接続は不要。
選択成功は `fastest-lut err=0` と `mode`／`scope`／`sketch_d_trial` で確認する。
その後LauncherからWriter／Sketchを開く。Shellに戻って変更すると、他のアプリにも反映される。
選択はアプリ切替後もRAMに保持する。再起動時は既定のD（Sketch A）へ戻る。NVSには保存しない。
既存の長形式 `display test display0 fastest-lut ...` も両方のShellで使用できる。

`scripts/build_papermono_standalone_lut.sh` は本版からSketch専用パネルflagを0にする。
専用パネルの開始／停止／suspendによるモード上書きを避け、既存Driverの選択を維持する。
専用パネルのキャンバス初期化／約20更新の自動停止は本版にはない。
前モードの残像が比較へ影響するため、比較前に同じ画面初期化操作を行い、長時間の連続試験は避ける。
物理的な濃さ・白化・追従性は実機で別途確認する。

既存のUSB限定判定をTerminal/Portの受理に変更し、4選択、状態表示、無効入力、無効IOの拒否をHost fixtureで確認する。
LUT配列、電圧、送信シーケンス、Partition構成は変更しない。
新Firmwareの書込み前には、実機と起動slotを再確認し、新規全Flashバックアップを取得する。
Application-only書込み後は全Flash読戻しと保護領域照合を行い、その後に起動確認する。
PlatformIO Uploadは使用しない。2026-10-21の利用者指示により、専用branchをCodexからpushする。

[Application-only書込み・本体Shell経路確認結果](flash-result.md)。修正版は実機に書込み済み。
