# PaperMono Dirty Rectangle 実機評価

2026-10-16。Branch `experiment/papermono-dirty-rectangle-hardware`、Worktree
`E:/ESP32/M5PapserMono-SolarOS/.research/dirty-rectangle-hardware/worktree`。
通常の統合WorktreeとLauncher用Worktreeは変更していない。

## A/B の結果

同一trial firmware、SolarOS 4.16.1、`refresh=fastest`、tile、baseline LUT、
実効SPI 20 MHz。`dirty-window=off` と試作 `dirty-window=on` のRAM転送だけを切り替えた。
`0x20` は各更新1回、BUSY timeoutとSPI errorは0。下表はOTP cleanup後の
Shellコマンド入力からWriter起動までのログ区間であり、Writer Aの本文試験全体と
Writer Bの本文試験を対比したものではない。

| 条件 | 更新 | RAM転送合計 | RAM転送中央値/回 | SPI data時間中央値/回 | BUSY中央値/回 | 全処理中央値/回 | 目視 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| A・全画面RAM | 27 | 2,592,000 B | 96,000 B | 39.009 ms | 51.418 ms | 123.906 ms | Shell/Writerの操作・保存・再読込は正常。後続入力で既存文字は薄まった |
| B・厳密矩形RAM | 28 | 79,052 B | 168 B | 0.189 ms | 51.478 ms | 56.671 ms | Shell入力中の1行に、読めないほどの字画欠け。Writer本文試験は中止 |

B末尾にはWriter起動時の広い75,000 B矩形が含まれる。したがって合計転送量の
削減率を一般化しない。小さい入力更新の転送時間は大きく減るが、BUSY約51 msは
変わらない。詳細な生ログは
`.research/dirty-rectangle-hardware/artifacts/A-dirty-off.log`、
`B-dirty-on.log`。集計スクリプトは同ディレクトリの`analyze_trial.py`。

欠けはユーザーの記憶では入力中のShell 1行に集中し、文字の上下・左右の一部が
欠けた。他の表示は正常。これは矩形のY窓・行転送境界の不整合でも、
更新された画素のold/new RAM内容が期待と違う場合でも起こり得る。
Hostモデルは物理的なMode-2 RAM保持／ping-pongを証明しないため原因未確定。
SSD1677 Rev 1.0の§8.2–8.5は0x11、0x44、0x45、0x4E、0x4Fによる
窓・カウンタを規定するが、0x20後のRAM面保持は保証していない。

## 復元と保護

COM39、MAC `28:84:85:44:79:68`、16 MB flash、OTA0 0x10000–0x70FFFFを
確認した。試験前の16 MB全体をバックアップし、application partitionのみtrialを
書き込んだ。異常後、バックアップのOTA0（7,340,032 B、SHA-256
`d35b5caa03bc58f0c50740a17fd94b58d10254e8241c5bb1fe38d5e689c70855`）を
同じapplication partitionへ復元し、esptoolの書込みhash照合がPASS。
ユーザーは通常画面の復旧を確認した。

復元後のreadbackでpartition table、otadata、PHYは書込み前と完全一致。
NVSは94 B、FATは5,288 B変化した。試験中の起動・A文書保存による更新を含み、
これらのpartitionをflash操作で上書き・消去していない。BLE bondや保存文書の
内容別の完全照合は未実施。

## 列全高の限定診断

欠けが文字の一部分に局在したため、Xは変更列に絞り、Yは全高480行にする
`dirty-window=columns`を追加。厳密矩形の`dirty-window=on`はShellの選択肢から
外した。初回は全RAMを同期し、次回から全高の列窓で0x24更新・0x26後同期する。
HostにX=768–775、Y=479–0の窓と480 B/planeの回帰を追加してPASS。
ESP32-S3ビルドもPASS（firmware SHA-256
`c5b9ce39e13ae2563257dce0079c5a87cb0a19b2134918b9c63dbc66b5c13dc8`）。
列全高版もapplication-onlyで書き込んだ。LauncherとShellは初期表示正常。
Shellで列全高を選択後、入力 `a` は正常、`b` に伴う最初のfastest更新は
全RAM同期（144,000 B、`0x20` 1回、496.371 ms、BUSY 134.021 ms）。
続く `c` では列全高転送（X=0–615、Y=0–479、73,920 B、`0x20` 1回、
109.373 ms、BUSY 51.101 ms、実効SPI 20 MHz）を行った。ログ上のSPI error・
BUSY timeoutは0。しかし停止画面で `a` と `c` は正常、既存の `b` はほとんど
白く欠けた。生ログは
`.research/dirty-rectangle-hardware/artifacts/columns2.log`。

これにより、**厳密矩形のY範囲指定だけを原因とする仮説は支持されない**。
全高でも欠けるため、X窓、転送時のアドレス進行、Mode-2内部RAMの保持・
ping-pong、0x26後同期の意味などが残る。ログと目視だけでは単一原因を確定
できない。HostのRAM保持モデルは実Controllerを代表していない可能性がある。
追加更新は行わず試験を中止し、同じOTA0バックアップをapplication-onlyで復元。
esptoolの書込みhash照合がPASSし、ユーザーもLauncher/Shellの正常表示を確認した。
通常版へのmergeはしない。

## 変更と検証

- `c09b435c`：既存Dirty Host MVPを統合版へ適用。
- `64d076b6`：Mode-2技術確認を適用。
- `941c6761`：20 MHz trialを適用。
- `082236c8`：RAM-only切替とSPI・BUSY計測を追加。
- `88c3bebf`：Y全高の列窓診断とHost境界テストを追加。

Host `epd_ssd1677_dirty_hold_test` PASS、ESP32-S3/ESP-IDF 5.5.5 build PASS。
初回trialのWriter Aは保存・再読込まで確認。Writer Bと列全高方式のWriter、
Sketch/TypeMouse等の回帰は異常停止のため未実施。**今回の二つのRAM部分転送
方式は実機表示品質に失敗し、実用版統合は不可**。Dirty Rectangle一般の
可能性まで否定する結果ではないが、次の試作にはMode-2の実RAM状態を確認する
別手段、または更新ごとのRAM全面再送を保ったまま転送量を減らせる設計根拠が必要。
前試験はこの時点で終了した。

## 全幅・行限定の追加切り分け（実機結果）

列全高の失敗だけでは、Xアドレス窓の挙動を除外できない。そこで
`dirty-window=rows`を追加した。変更Y行だけを選び、Xは常に0–799の全幅へ
0x24を書き、更新後に同じ範囲の0x26を同期する。初回は従来どおり全RAM同期、
通常起動時は`dirty-window=off`。LUT・VCOM・電圧・更新回数は変更しない。
Hostで切替時の全画面fallbackと、X全幅・Y=400–401の窓／転送量を検証してPASS。
ESP32-S3/ESP-IDF 5.5.5ビルドもPASS。Firmware SHA-256は
`e10ea6683c45beb5936e3a583805eb5e5436236c2885f134b15a08731996f86e`。
実装Commitは`0da2f861`。以下が実機評価の結果。

COM39のESP32-S3（MAC `28:84:85:44:79:68`）のOTA0へapplication-onlyで
書き込み、書込みhashを確認した。Shellから20 MHz、`refresh=fastest`、
`fastest-path=tile`、`dirty-window=rows`を選択した。実機ログでは、最初の
fast更新は全RAMの144,000 B、次の2更新は各20,600 B（100 byte/row ×
103 row × 2 plane）で、いずれも`0x20=1`、BUSY約51 ms、エラー0。
ユーザー目視で`a`、`ab`、`abc`は一度すべて正常だった。

続けて通常のShell操作を行うと表示が崩れ、`writer`入力の末尾`r`が
スペースキーを押した際に欠けた。ログにはX方向のdirty領域が32 pxでも
RAM転送3,400 B（100 byte/row × 17 row × 2 plane）の更新があり、
X全幅の行限定方式が実際に動いていた。最小200 Bの更新も発生した。
取得ログ内のfast更新は計27回で、`0x20`も27回。カーソルの点滅など
入力文字以外の小さな更新が含まれるため、27回を入力文字数と解釈しない。
SPI error、BUSY timeout、`0x20`未発行は記録されていない。途中で
ディスプレイのpower off/onと正規OTP fullを経た後も、次のfast更新は
全RAM同期してから行限定更新へ戻っている。ログは
`.research/dirty-rectangle-hardware/artifacts/rows-input.log`。

字画欠けの時点で試験を中止し、試験前に保存したOTA0 application
partition（7,340,032 B、SHA-256
`d35b5caa03bc58f0c50740a17fd94b58d10254e8241c5bb1fe38d5e689c70855`）
を同じ領域へ復元した。esptoolの書込みhash照合はPASS。NVS、FAT、
otadataを対象にした消去・書込みは行っていない。復元後の目視確認は
ユーザー回答でLauncherまたはShellの文字と白背景が正常に戻ったことを確認。

**判定：全幅・行限定も実機表示品質に失敗。** `abc`の短い成功だけでは
通常入力を保証できない。列全高と行全幅の双方で欠けたため、単なる
片軸の窓不足と断定できない。RAM内容、Mode-2の面状態、更新後の
0x26同期、窓アドレス進行のどこに不整合があるかは未確定。現在の
HostモデルのPASSは物理ControllerのRAM保持を検証していない。
今回の方式は実用版へ統合しない。追加の実機更新試験は行わず、
ControllerのRAM状態を検証できる方法または仕様上保証された方式を
特定してから次の試作を判断する。
