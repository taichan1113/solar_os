# RAM転送方式×LUT版 Application-only書込み

2026-10-21（JST）、ユーザーの明示指示「flashしてください」により実施。

- COM39、ESP32-S3、既知の実機MAC、16MiB Flash、Secure Boot／Flash encryption無効を照合。
- Writerがsuspendedで残っていたため、ユーザーの保存済み確認後、標準 `session close` で終了。最新session一覧でWriter／Sketchなしを確認してからresetした。
- Running=Boot=ota_0、実機TableとBuild表の一致、raw otadata CRC／選択slot、Application容量・消去境界を監査。
- fresh全16MiB backupと元Applicationのchecksum／内部digest照合を完了してから、**ota_0 @ 0x10000だけ**へ書込み。
- 実装Commit：`8dd1c43b089872ee0cc88084035a06be296d5945`。Application **3,465,104 B**、SHA-256 `a4cd6d82dd28978fbf35421bb204892576865a1848a7284a4199be615cc95a7c`。
- 書込み後の全16MiB読戻しでApplication一致と**対象Application partition外の全バイト不変**を確認。Bootloader／Table／NVS・BLE bond／otadata／PHY／非対象OTA／FATの7領域も一致。照合完了後に1回だけ通常resetした。
- USB ShellでRunning／Boot、`ram status`、`lut status`を読取り確認。**RAM rows-dual、D／SketchのみA、Driver error=0**。LUT／RAMモード切替や自動描画試験はしていない。
- 目視確認：PASS_DISPLAY_ONLY。6条件の画質・黒保持比較は未実施。

復元用Applicationは **3,465,424 B**、SHA-256 `b1e78b6eba3639dff58b89cdab834e60ae89703db4eacdfe88d64a1350b1e218`。サブPCのLUT可変版の既知SHAと一致した。

private原本：`E:\ESP32\M5PapserMono-SolarOS\.research\ram-path-lut-trial\archive\ram-lut-flash-20261021-233453`。`application-rollback.bin`、`application-before.bin`、全Flash前後原本、監査JSON、USB／bootログ、`ROLLBACK.md`を保存。NVS／bond／保存文書を含む原本はGitへ追加しない。

将来戻す場合は、現在の文書を保存・終了してfreshなport／実機／partition／起動slot監査とbackupを取り、元Applicationだけを書き戻す。全Flash原本の丸ごと復元は後日の文書・bondを失うため行わない。保護照合は復元直前の状態を基準にする。

既存のstartup／NVS設定は変更していない。通常復帰は本体Shellの `ram rows` → `lut 2`。Full-RAMの光学効果はユーザーによる定性的な実使用で確認する。
