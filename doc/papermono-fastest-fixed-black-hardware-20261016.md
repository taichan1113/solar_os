# PaperMono fastest 固定黒切り分け（2026-10-16）

## 結論

20 MHz、baseline LUT、fastest tile の限定試験では、**表示を変えずに `0x20` を6回発行したBで固定黒が薄くなった**。同じ画像を12秒間保持し、fast更新を0回にしたAでは、目視で濃度変化はなかった。したがって、この条件では右側画素の変更は薄まりの必要条件ではなく、fastestの表示更新そのものとの相関が強い。SSD1677のどの内部駆動が寄与するか、経時的なパネル劣化があるかまでは判定できない。

C（右側に文字を追加）は左半分のframebuffer hash不一致で最初のfast更新前に安全停止した。Cの光学結果は**未測定**とする。エラー後に追加の更新試験は行っていない。

## 条件と結果

各条件はOTP全画面白→同じ初期画像をOTP全画面表示→T0→12秒の試験区間→T1→OTP cleanup。USB Serial Shellから実行し、通常Writerの更新経路は変更していない。黒画像のT0とT1はそれぞれ同じ条件内で比較した。

| 条件 | 変更画素 | fast `0x20` | 実行 | 目視 | 写真の相対コントラスト |
| --- | ---: | ---: | --- | --- | ---: |
| A・12秒待機 | 0 | 0 | 完了 | 固定黒は変わらず、背景正常 | 0.665→0.672 |
| B・同一画像再更新 | 各回0 | 6 | 完了 | 固定黒が薄くなった、背景正常 | 0.592→0.542 |
| C・右側へ追記 | 未確定 | 0 | 初回fast前に安全停止 | T1評価なし | 測定しない |

Aのログでは2秒間隔6ステップで累積 `fast_0x20` が不変。Bでは各ステップの `changed=0`、`fast_0x20=1`、`spi_hz=20000000` を確認し、計6回。Bの初回はcold setup込みで475 ms、以後は約132 ms（BUSY約51–52 ms、SPI約39 ms）。OTP fullは各回約4.29秒。BUSY timeout、反転、強い残像は観察されなかった。

写真は元解像度3840×2160の `A_T0.jpg`、`A_T1.jpg`、`B_T0.jpg`、`B_T1.jpg` を使用。黒四角内側の緑成分平均Kと、同じ高さの近傍白地の平均Wから `(W-K)/W` を計算した。AはK 61.5→59.8、W 183.3→182.3、BはK 89.2→102.3、W 218.9→223.2。AとBのT0で露出・照明が異なっているため、**AとBの絶対値は直接比較しない**。同条件内のT0/T1の傾向のみ、ユーザーの目視所見を補助する値として扱う。写真とROIの測定は厳密な測光ではない。

## 安全停止と保護

最初のB試行は3回目の後に左framebufferのhash不一致で停止した。Launcherが同じU8g2 bufferを変更し得るため、Launcherを閉じ、各fast更新前に試験画像を再構成するよう試験専用コードを修正した。Bの完走時は `buffer_interference=0`。Cでは `buffer_interference=0` だが、右側の追記後に左hashが不一致となり、`ESP_ERR_INVALID_STATE`、`completed=0` で停止した。Cの原因は未特定であり、共有bufferへの競合か描画・hash検査の問題かを追加診断なしに断定しない。今後Cを再試験するなら、描画前・初期画像再構成後・右文字追記後を別々に記録し、Hostで左側不変性を先に検証する必要がある。

COM39のESP32-S3、16 MB flash、起動slot `ota_0`、partition tableを照合し、実用アプリを7 MBのpartition全体としてバックアップしてから、application-onlyでtrialを書き込んだ。試験後は同じバックアップを`ota_0`へapplication-onlyで戻し、esptoolの書込hash検証に成功した。partition table、otadata、PHYは前後でbyte一致。NVSとFATは書込対象外だが、通常起動によるbyte変化があったため前後の完全一致とは報告しない。復元後はUSB ShellでSolarOS 4.16.1・`refresh=fastest`を確認し、ユーザーがLauncherと白背景の正常表示を確認した。

試験firmwareは通常版に残していない。新規LUT、電圧、VCOM、物理駆動シーケンスの変更はない。A/Bから言える実務上の優先案は、画像が変わらないときのfast更新を確実に省くことと、更新回数そのものを減らすこと。これらの効果やDirty Rectangleの採用可否は本試験だけでは確定しない。

## 証拠と検証範囲

USBログ：`.research/fastest-fixed-black-cause/artifacts/A3.log`、`B2.log`、`C.log`、`flash-rollback.log`、`restored-usb.log`。写真はユーザーPCの `C:\Users\takec\Pictures\Camera Roll` にあり、リポジトリには複製しない。

試験機能のHost driver testは試験前にPASS。最終の試験専用Shell変更を含むESP32-S3 firmware buildはPASS。Cの左側不変性と最終版Host再実行は未確認として残す。通常Writer・Sketch・TypeMouseの復元後の実機回帰は今回実施せず、Launcher表示とUSB Shell応答まで確認した。
