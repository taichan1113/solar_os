# PaperMono fastest 固定黒切り分け（2026-10-16）

## 結論

20 MHz、baseline LUT、fastest tile の限定試験では、**表示を変えずに `0x20` を6回発行したBと、右側だけに文字を追加しながら6回発行したCの両方で、左の固定黒が薄くなった**。同じ画像を12秒間保持し、fast更新を0回にしたAでは、目視で濃度変化はなかった。したがって、この条件では右側画素の変更は薄まりの必要条件ではなく、fastestの表示更新そのものとの相関が強い。BとCの薄まり量の厳密な差、SSD1677のどの内部駆動が寄与するか、経時的なパネル劣化があるかまでは判定できない。

## 条件と結果

各条件はOTP全画面白→同じ初期画像をOTP全画面表示→T0→12秒の試験区間→T1→OTP cleanup。USB Serial Shellから実行し、通常Writerの更新経路は変更していない。黒画像のT0とT1はそれぞれ同じ条件内で比較した。

| 条件 | 変更画素 | fast `0x20` | 実行 | 目視 | 写真の相対コントラスト |
| --- | ---: | ---: | --- | --- | ---: |
| A・12秒待機 | 0 | 0 | 完了 | 固定黒は変わらず、背景正常 | 0.665→0.672 |
| B・同一画像再更新 | 各回0 | 6 | 完了 | 固定黒が薄くなった、背景正常 | 0.592→0.542 |
| C・右側へ追記 | 各回37～63バイト | 6 | 完了 | 固定黒が薄くなった、背景正常 | 0.579→0.520 |

Aのログでは2秒間隔6ステップで累積 `fast_0x20` が不変。Bでは各ステップの `changed=0`、`fast_0x20=1`、`spi_hz=20000000` を確認し、計6回。Cでも各ステップの `changed=37～63`、`fast_0x20=1`、`spi_hz=20000000` を確認し、計6回。Cの初回はcold setup込みで476 ms、以後は約132 ms（BUSY約51～52 ms、SPI約39 ms）。Bの時間も同程度だった。OTP fullは各回約4.29秒。BUSY timeout、反転、強い残像は観察されなかった。

写真は元解像度3840×2160の `A_T0.jpg`、`A_T1.jpg`、`B_T0.jpg`、`B_T1.jpg`、`WIN_20261016_09_47_26_Pro.jpg`（C-T0）、`WIN_20261016_09_48_27_Pro.jpg`（C-T1）を使用。黒四角内側の緑成分平均Kと、同じ高さの近傍白地の平均Wから `(W-K)/W` を計算した。AはK 61.5→59.8、W 183.3→182.3、BはK 89.2→102.3、W 218.9→223.2、CはK 92.35→104.36、W 219.50→217.32。A/B間は外光の影響で露出・照明が異なっているため、**条件間の絶対値は直接比較しない**。ユーザーによるとA/BのT0の実機の見た目は同程度だった。同条件内のT0/T1の傾向のみ、目視所見を補助する値として扱う。写真とROIの測定は厳密な測光ではない。

## 安全停止と保護

最初のB試行は3回目の後に左framebufferのhash不一致で停止した。Launcherが同じU8g2 bufferを変更し得るため、Launcherを閉じ、各fast更新前に試験画像を再構成するよう試験専用コードを修正した。Bの完走時は `buffer_interference=0`。最初のC試行もhash不一致でfast更新前に安全停止した。段階別ログでは、初期画像の復元直後は一致し、右側文字の描画直後だけ不一致だった。実機のU8g2回転は `R2` で、論理左半分はnative bufferの後半400列にある。旧検査がnative前半400列を誤って左側としてhashしたことが原因と判明した。回転に応じてhash対象を選び、初期48 KB frameを各更新前に正確に復元し、右側の描画を論理右半分へclipするよう、試験専用コードだけを修正した。R0/R2で固定左が不変になるHostテストを追加してPASS。修正版Cは `buffer_interference=0`、`err=0`、`completed=6` で完走した。

COM39のESP32-S3（MAC `28:84:85:44:79:68`）、16 MB flash、起動slot `ota_0`、partition tableを照合し、実用アプリを7 MBのpartition全体としてバックアップしてから、application-onlyでtrialを書き込んだ。C再試験の直前には全flashを追加で読み取り、実用アプリのpartitionがバックアップと一致することを確認した。試験後は同じバックアップ（SHA-256 `d35b5caa03bc58f0c50740a17fd94b58d10254e8241c5bb1fe38d5e689c70855`）を`ota_0`へapplication-onlyで戻し、esptoolの書込hash検証に成功した。partition table、otadata、PHYはC前後でbyte一致。NVSとFATは書込対象外だが、通常起動によるbyte変化があったため前後の完全一致とは報告しない。復元後はUSB ShellでSolarOS 4.16.1・`refresh=fastest`、Launcher起動を確認した。

試験firmwareは通常版に残していない。新規LUT、電圧、VCOM、物理駆動シーケンスの変更はない。A/Bから言える実務上の優先案は、画像が変わらないときのfast更新を確実に省くことと、更新回数そのものを減らすこと。これらの効果やDirty Rectangleの採用可否は本試験だけでは確定しない。

## 証拠と検証範囲

USBログ：`.research/fastest-fixed-black-cause/artifacts/A3.log`、`B2.log`、`C.log`、`C2.log`、`C3.log`、`flash-C3-rollback.log`。写真はユーザーPCの `C:\Users\takec\Pictures\Camera Roll` にあり、リポジトリには複製しない。

試験機能のHost driver testに加え、最終の試験専用Shell変更を含むR0/R2 frame不変性HostテストはPASS。ESP32-S3 firmware buildもPASS。通常Writer・Sketch・TypeMouseの復元後の実機回帰は今回実施せず、Launcher表示とUSB Shell応答まで確認した。
