# Candidate D Writer Trial — Application-only Flash

2026-10-18。ユーザーのFlash承認と、USB接続・未保存なし・競合なし・表示正常の確認を受けて実施した。

**結果：Application-only Flash、読戻し、保護領域照合、USB Shell、通常起動の確認が完了。**

## 書込み内容と現在状態

| 項目 | 結果 |
|---|---|
| 実機 | ESP32-S3 / 16MB / MAC `28:84:85:44:79:68`、今回検出したCOM39 |
| 起動slot / 書込み先 | 実行中/boot/partition/otadataを照合した`ota_0`、`0x10000` |
| Firmware | Writer D専用Trial。実装Commit `251232824da2f5350dbdf9184e0398b434a61497` |
| Application | 3,458,400 B |
| 書込み/読戻しSHA-256 | `517250fa47fd1d702fccf28e5f3da6d4557fca978bd6f9e953d589ed6b29a253`（一致） |
| 表示設定 | `refresh=fastest`、既存`fastest-path=tile`起動設定を保持。SPI20/Dirty rows-dualを含む検証済みbuild。実効SPI時間は今回のFlash確認では未計測。 |
| Writer LUT | **Baseline A**。Dは未選択、予約`0/6`、Dの0x20は`0` |
| Writer A/D試験 | 未開始。Writer由来の物理更新`0`。画像・文字入力の試験は自動実行していない。 |
| USB / Launcher | USB Shell応答、Launcher起動、Display executorのerror=0を確認 |
| 目視 | ユーザー確認：表示正常 |
| Integration | merge/pushせず、実験Branchのみ。実機にはTrialを残した。 |

Flash承認後もDは自動有効にしていない。DはWriterの実物理更新を最大6回に制限する試験用の明示選択であり、通常起動・再起動はA。`/.shell/startup`は前後一致し、D設定や試験状態をNVSへ保存していない。

## バックアップと保護

書込み前に今回の実機から16MiB全Flashと現在のApplicationを新規退避した。Applicationは既知Integrationの原本とバイト一致した。

```text
E:\ESP32\M5PapserMono-SolarOS\.research\writer-black-hold-d\archive\20261018-writer-d-flash\
  full-flash-before.bin               今回の16MiB全退避
  application-partition-before.bin    ota_0領域全体
  application-rollback.bin            復元用実用Application
  backup-audit.json
  flash-summary.json
  deployment-audit.json
```

- 全退避SHA-256：`71144a5fa87f3c32d60fb43d635c0a106478c8a8c51bea1754fe7eea464da0d3`
- 復元用Application SHA-256：`278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6`
- bootloader、partition table、NVS、otadata、PHY、FAT、非使用`ota_1`の**7領域すべて、書込み前と書込み後・再起動前でバイト一致**。
- 書込みによるBLE bond/NVS改変はなく、文書/Sketch画像/起動設定はFATごと保持した。UI設定を変更していない。BLE実接続や保存操作の回帰は今回行っていない。
- 再起動ログにSDKのPHY校正データ保存がある。使用SDKの`components/esp_phy/src/phy_init.c:759`はNVSの`phy` namespaceへ保存する経路であり、Flash書込み時のNVS消去とは区別する。再起動後のNVS全体のバイト一致を確認したとは報告しない。追加のROM再起動/読出しは行っていない。
- bootloader/factory/partition/otadata imageは書き込んでいない。書込み命令は、確認済みApplication範囲に対する1回のみ。

バックアップのFlash imageには保存データやbondを含むため、Gitには格納していない。[Flashログ](evidence/writer-black-hold-d-flash-20261018/flash.log)、[読戻し・保護検証](evidence/writer-black-hold-d-flash-20261018/deployment-audit.json)、[USB状態](evidence/writer-black-hold-d-flash-20261018/post-flash-usb.log)を保存した。

## 実用版へ戻す場合

復元用imageは今回の実機から採取し、既知原本と一致確認済み。**今回は復元処理を実行していない**。Trialを残している。

1. Writer/Sketchの編集を保存して閉じ、他セッションの実機使用がないことを確認する。
2. 実機port/識別/実行slotを再確認し、USBで`version`、`sessions`、`ota status`を新たに取得する。過去のCOM39やslotを無検証で再利用しない。
3. 今回用の`device_query.py`で`pre-restore-usb.log`を採り、`restore_writer_guarded.py --saved-and-exclusive`を使用する。両helperは`.research/writer-black-hold-d/`に保存した。helperは現在のTrial Application、MAC、16MB、partition/otadata/slot、復元imageのSHAを再照合し、不一致なら書き込まない。
4. **Applicationだけ**を復元し、現在のNVS/FAT等の前後一致とApplication読戻しを確認する。後日編集した文書を巻き戻さないため、全Flashバックアップをそのまま書き戻さない。
5. USB Shellと通常画面を確認する。

復元helperの構文検証は実施済み。復元の実機実行は、試験終了または別途復元依頼時に行う。

## 次の段階

今回の作業はFlashと正常起動確認まで。撮影準備後に、USBからの`writer-lut`操作でWriter A/D比較を進める。初期条件・OTP初期化回数・固定文字ROI・入力手順・実0x20数を揃え、Dは最大6回。カーソル等もWriter更新なら予算に含む。Dの短い白化だけを不合格にしない一方、実機の品質・応答・背景・Driver異常は実測する。

黒濃度改善、入力遅延、W-1〜W-6、長期安全性は今回未判定。[実装・Host・Build・試験計画](m5paper-mono-writer-black-hold-d-20261018.md)を参照。
