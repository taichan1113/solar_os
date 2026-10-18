# PaperMono Launcher統合版 Application-only Flash報告

2026-10-19。ユーザーの「Application-onlyでflashしてください」に基づき実施。

| 項目 | 結果 |
|---|---|
| Firmware | 指定6項目Launcher＋Files/Reader/Clock Touch＋既存Sleep/Wake連携 |
| コードCommit | `16b635c7b26a5e62fb2377cf4b285ef5ac1b84ab` |
| Flash前Integration | `8239268ce5dfa2d1f9f321d51af65366492bb4c5` |
| 対象 | COM39、ESP32-S3、MAC `28:84:85:44:79:68`、16MB |
| 書込み先 | `ota_0` / `0x10000`、Applicationのみ |
| Application | 3,460,928 B |
| 読戻しSHA-256 | `667a9c2461ec93f34efba07d502af7890fe24b86055897fbd10a80571e7e9d31`、候補イメージと一致 |
| Flash前バックアップ | 16MB全体・Application partition全体・現行Applicationを保存／照合 |
| 全体バックアップSHA | `1ef7069afbad8a038a6d0a161b380b8e340aac46d31e7b01e5b6dfd7ef72a9bb` |
| 復元Application SHA | `3374c584f1424829f262aeea61245e5bb5fc24fcd2bdd941619aef00a2004845` |
| 保護7領域 | bootloader、partition table、NVS、otadata、PHY、FAT、非対象OTA：書込み後・再起動前にすべて一致 |
| 起動 | USB Shell・Launcher active、同じota_0で起動を確認 |
| 表示設定 | `refresh=fastest`、共通Candidate D／Sketch Baseline Aを維持。startupの内容も前後一致 |
| SPI | SPI20ビルドを維持。初期化時は10MHzのログ。fastest更新時の20MHz実測は今回未実施 |
| Display Async | enabled=1、busy=0、pending=0、suspended=0、error=0 |
| 自動操作 | 文字入力・描線・Sleep操作・連続Refresh試験は実行していない |
| 目視・Touch・Sleep/Wake | 6項目Launcher、Keyboard/Clock/Files/Readerのタッチ操作・復帰、Shell復帰・Sketch黒線・復帰は正常。Sleep/Wakeは1回正常。USBはSleep後ケーブル再接続で復旧。PC BLE HID実入力・Writer保存再読込・Sleep反復は未確認 |
| 最終状態 | 新しいLauncher統合版を実機に残した。Rollbackは未実施 |

本体に未保存編集を持つWriter／SketchセッションがないことをUSB Shellで確認してから、読取りと書込みを行った。Flashツールからは、保存文書・BLE bondを含むNVS/FAT領域を書込み・消去していない。保護比較は**Application書込み後・再起動前**の一致であり、起動後のOSのNVS更新やReaderの位置保存等は通常機能として動作する。

## 実機確認結果

- 6項目のLauncher表示・濃さ・白背景：ユーザー確認正常。
- Keyboardのタッチ起動・42キー表示・ExitでLauncher復帰、Clock表示・B長押し復帰：ユーザー確認正常。
- Filesの行選択・選択行再タップでフォルダを開く・Upで親へ戻る・Launcher復帰：ユーザー確認正常。
- Readerで文書を開く・本文左右タップ・上部Filesで選択画面へ戻る・Launcher復帰：ユーザー確認正常。
- Shellのタッチ起動・入力なしでB長押し復帰、Sketchの黒線・保存せずB長押し復帰：ユーザー確認正常。
- Sleep中のTouch無反応→KEY1でWake→最初のTouchでClock起動→Launcher復帰：**1サイクル**をユーザー確認。USB復旧後の`power status`でもLight sleep count=1、Last sleep=16秒、EXT1 wake mask=`0x4`（GPIO2）を確認。
- 最終USBログでLauncher active、cdc0 Shell active、Async busy/pending/error=0。D使用30回・成功30回・エラー0、物理更新はA=30／D=30／OTP=7。TraceでSketchはA、その他はDを確認した。これらは起動以降の累計で、手動操作数とは一致しない。

このFirmwareでのPCへのBLE HID実入力・TypeMouse固定／反転、Writer保存／再読込、Readerスワイプ、Sleep/Wakeの反復は未確認。未実施項目はPASSと扱わない。Host／Build結果は[実装報告](m5paper-mono-launcher-touch-grid-20261019.md)を参照。今回はApplication書込みと手動回帰であり、追加の波形・黒濃度試験は行っていない。

## Sleep後のUSB接続について

今回のLight sleepからのWake後、COM39は列挙されたままだったがUSB Shellへの読取専用照会には応答しなかった。**USBケーブルを抜き差しすると、本体を再起動せずShell応答が復旧した。** サービス／セッションとstartupは維持されていた。

これはESP-IDF v5.5.5の[USB Serial/JTAG Console — Sleep Mode Considerations](https://raw.githubusercontent.com/espressif/esp-idf/v5.5.5/docs/en/api-guides/usb-serial-jtag-console.rst)に記載された、Light sleep中のUSBクロック停止と復帰後の再接続が必要になる場合がある挙動と整合する。自動Light sleepの接続ガードは手動Sleepを防がない。今回USBの連続接続維持はPASSとせず、再接続後の復旧を確認した。追加Sleep試験やFirmware修正は行っていない。

## Archive／Rollback

Archive：`E:\ESP32\M5PapserMono-SolarOS\.research\launcher-touch-grid\archive\launcher-touch-grid-flash-20261019-003127`

`application-rollback.bin`が今回実機から読み取った元の共通D／Sketch A版。`full-flash-before.bin`は16MB保管用で、日常のRollbackに全領域を復元しない。

Rollback時はUSB接続と未保存編集なしを確認し、`device_query.py`で新しい`pre-restore-usb.log`を取得後、`restore_guarded.py --saved-and-exclusive`を使用する。新たなpartition／起動slot・現在Application・保護領域を確認し、Applicationのみ復元して読戻しと保護領域を照合する。デプロイ時の古いFAT/NVSを戻さない。

使用ヘルパーは`.research/launcher-touch-grid/`とArchiveに保存。USBログ、Flash要約、バックアップ照合、デプロイ監査は`doc/evidence/launcher-touch-grid-flash-20261019/`にも保存する。実機バックアップの大容量バイナリはGitへ含めない。
