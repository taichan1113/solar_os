# PaperMono Dirty Rectangle 常用評価版（2026-10-16）

実用Firmwareを退避したうえで、既存の `rows-dual` Dirty Rectangle試作を常用評価できる形にした。導入前の32回上限は、自動OFFではなく32回ごとのコントローラRAM全面再同期に変更した。この再同期はSPI上のRAM書込みであり、OTP全画面表示更新ではない。LUT、電圧、VCOM、更新命令回数は変更していない。PaperMonoの起動時は `rows-dual` を選び、USB Shellの `display mode display0 dirty-window=off` でRAM最適化を即時停止できる。

実装Branchは `feature/papermono-dirty-rectangle-live`、変更Commitは `6e26aee95bda3624360744651b0a558520210418`。基点は限定実機試験版 `148ea7e8`。元の実用WorktreeとLauncher Worktreeは変更していない。

Hostテストは `epd_ssd1677_dirty_hold_test`（RAM面の保持／入替え両モデルで66更新、32回周期の全面再同期）、`epd_ssd1677_test`、`epd_ssd1677_spi_clock_dirty_20_test`、`epd_ssd1677_hybrid_trial_test` がPASS。ESP-IDF 5.5.5のESP32-S3ビルドもPASS。ビルド条件は PaperMono / writerdeck / OTA、Dirty trial=1、SPI20 trial=1、Hybrid trial=1、waveform trial=0。実機のpartition tableとビルド版は一致した。

書込み前にCOM39のESP32-S3（MAC `28:84:85:44:79:68`、16MB）を照合し、全Flashを `.research/dirty-rectangle-live/archive/20261016-practical-before/full-flash.bin` に保存した。起動中の `ota_0` は `0x10000`、実用アプリの7MB領域SHA-256は `d35b5caa03bc58f0c50740a17fd94b58d10254e8241c5bb1fe38d5e689c70855`。同じArchiveにアプリのみの復元イメージ、今回のDirty版イメージ、領域別SHA-256と復元手順がある。Archiveはユーザーデータを含むためGitには含めない。

Dirty版は `ota_0` の `0x10000` にapplication-onlyで書き込み、esptoolの検証と読戻し全バイト照合がPASS。Dirty版アプリ（3,454,112 B）のSHA-256は `dbde348d87a0e09381d6b682b5028cc925c0e296395caed6b32eb83909620195`。partition table、otadata、PHY、FATは書込み前後で全バイト一致。NVSでは63バイトが変化し、起動時に既存キー `tai_ceiling` の新しい記録が追加された。esptoolが消去・書込みした範囲はアプリ領域のみ。

起動後はUSB Shellで `refresh=fastest`、実効SPI 20MHz、`dirty-window=rows-dual` の受理を確認した。ユーザー目視ではLauncherの文字・白背景が正常。LauncherからWriterへ入り、`abc` を1文字ずつ入力しても字画欠け・反転はなく、保存せずLauncherへ戻れた。ZMK入力とLauncherタッチも正常。今回の少数入力は長期の表示品質・速度を保証しないため、常用しながら確認する。異常が出た場合は入力を止めて画面を残し、まずDirtyをOFFにする。必要ならArchiveの `RESTORE.md` に従い、実用アプリをapplication-onlyで復元する。
