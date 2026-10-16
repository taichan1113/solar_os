# PaperMono Dirty Rectangle 両面同期の限定実機評価

## 判定

**CONDITIONAL。** 以前の `dirty-window=rows` は通常のShell入力で字画欠けを起こした。今回の `dirty-window=rows-dual` は、変更行について `0x20` 後に `0x24` と `0x26` の両方を現在画像へ同期する。Shellで32回、Writerで32回のfast更新を行い、目視の文字欠け・反転・残像はなかった。各33回目には自動で従来の全RAM転送へ戻り、表示も正常だった。これにより両面同期は有望だが、実Controllerの面入替えが原因と確定したわけではなく、長期使用と保存・再読込の回帰は未評価。通常版へ統合しない。

## 構成と保護

- 専用Branch: `experiment/papermono-dirty-rectangle-hardware`、独立Worktree: `.research/dirty-rectangle-hardware/worktree`。
- M5Paper MONO / ESP32-S3、COM39、MAC `28:84:85:44:79:68`、16 MB、起動OTA0 `0x10000`–`0x70ffff`。試験前OTA0のSHA-256は `d35b5caa03bc58f0c50740a17fd94b58d10254e8241c5bb1fe38d5e689c70855`。試験前に `0x0`–`0x70ffff` を読戻してバックアップとの一致を確認した。
- 試験ビルド: Board `m5paper_mono` / Flavor `writerdeck` / Layout `ota`、Dirty/HYBRID/SPI20 trialはON、Waveform trialはOFF。SPI実効20 MHz、`refresh=fastest`、`fastest-path=tile`、baseline LUT。32回上限版app imageは3,453,376 B、SHA-256 `80ae581231cd8075351db4176b43484cc394e89d0b7a89ec852f1fbd5c948320`。
- 2回の試験版書込みと2回の復元はOTA0へのapplication-only。`esptool`の書込みハッシュ照合はすべてPASS。最終的に上記バックアップOTA0を復元し、ユーザーが通常画面の正常表示を確認した。NVS/FAT/otadata/PHYを対象とする消去・書込みは行っていない。復元後の読戻しでpartition table、otadata、PHYは試験前と完全一致。NVSには起動をまたぐ188バイトの変化があるため、NVS内容やbondの完全一致は主張しない。FATの試験後読戻しも未実施。

## 実機結果

| 条件 | 両面同期のfast更新 | RAM転送量合計 | 中央更新時間 | BUSY中央値 | 33回目 | 目視 |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| Shell入力・消去・Space | 32 | 530,700 B | 61.10 ms | 51.37 ms | 従来全RAM 96,000 B | `writer alpha beta gamma`まで字画欠けなし |
| Writer入力・改行・削除・画面切替 | 32 | 572,400 B | 65.93 ms | 51.65 ms | 従来全RAM 96,000 B | `BLACK BLACK 0123456789` と次行 `tes`、Shell往復後も正常 |

各条件で `REF err=0`、`0x20`は試験32回＋自動解除後1回、SPI/BUSY timeout記録なし。全RAM方式なら32回で3,072,000 Bなので、同じ回数のRAM転送量だけを比べるとShell約82.7%、Writer約81.4%削減。画面内容とcold条件が揃った厳密な速度A/Bではない。BUSY時間はほぼ維持された。

先行の6回上限版でもShell入力6回は正常で、7回目に全RAMへ自動復帰した。32回版では以前の行限定方式で欠けが出た約27回を越えて正常だった。Writerでは保存していないRAM文書を使い、既存文書・bondを変更する操作はしていない。

## 実装根拠と残る課題

`src/drivers/epd_ssd1677_reference.inc` のpost-activation同期で変更行の両RAMへ現在画像を書き、32回後に試験モードを解除する。初回や状態不明の更新は全画面RAMへフォールバックする。Hostの「RAM保持」と「0x20で物理面が入れ替わる」両モデルは前CommitでPASSし、今回の上限変更後はESP32-S3/ESP-IDF 5.5.5ビルドがPASSした。上限変更そのもののHost再実行は行っていない。

SSD1677の実RAMを読戻していないため、面入替え仮説は実機で未証明。今回の目視合格はShell/Writerの限定操作だけで、Sketch、TypeMouse、保存・再読込、長期の表示品質は未確認。Dirty RectangleはRAM転送量削減であり、`0x20`回数や固定黒の物理的な薄まりを減らすとは言えない。次に通常版への統合を判断するなら、Controller RAMの状態確認手段または同等の限定回帰を追加し、既存の画面品質・アプリ機能を別途検証する。

生ログは作業元の `.research/dirty-rectangle-hardware/artifacts/dual32-input.log` と `dual32-writer.log`、集計スクリプトは `analyze_dual_trial.py`。
