# PaperMono LUT可変版・Ubuntu実機試験の引継ぎ報告

更新日: 2026-10-21（日本時間）。対象branch: `feature/papermono-standalone-lut-trial`。
基点: `feature/papermono-sketch-d-diagnostics` / `aacd0f819d2b5e008f0029c98ddd44ceb49d6c39`。

## 1. 現在の状態

本体ShellでA／D（Sketch A）／D（Sketch D）／E1を選べるFirmwareを再書込み済み。
実装commitは `1e0408f88eefa7f6f02ae848bbe77ac2989dd8bf`。以後は報告書のみ変更し、実装や波形を修正していない。
現在の既定モードは **全アプリD・SketchのみA**。選択はRAMに保持し、再起動するとこの既定へ戻る。

Application読戻し・Application外の全バイト不変・個別保護領域のSHA照合は成功。
起動ログにLauncher switch=ok／Touch readyを確認し、利用者もLauncher表示・タッチ正常を確認済み。
USB consoleは閉じている。再書込み後の光学比較は未実施。

**切替機能と書込み・起動は確認済みだが、白化・黒保持の光学評価は未解決。**
利用者の「時間が経つと白化が収まる」という追加観察を受け、同じ可変版を再書込みした。改善版Firmwareを作ったわけではない。

## 2. 実装内容

### 本体Shell／USB Shell共通の4モード

| 入力 | Writer・Shellなど | Sketch | Driverの選択 |
| --- | --- | --- | --- |
| `lut 1` | A | A | `baseline` |
| `lut 2` | D | A | `candidate_d` |
| `lut 3` | D | D | `candidate_d_sketch` |
| `lut 4` | E1 | E1 | `candidate_e1` |
| `lut` / `lut status` | 状態表示のみ | 状態表示のみ | `status` |

- 本体接続キーボードで操作でき、USBホストは不要。
- 従来USB限定だった長形式 `display test display0 fastest-lut ...` を本体Terminalでも受け付ける。
- 短形式をShellのbuiltin一覧と組込みmanualへ登録した。無効な番号・引数・IOは拒否する。
- 既存DriverのRAM上の選択を変更する。選択成功は `err=0` と `mode`／`scope`／`sketch_d_trial` で確認する。
- D（Sketch A）とD（Sketch D）は、Shell／Writer上では同じD。Sketchへ入ったときに適用が分かれる。
- 今回のShell追加でLUT配列・電圧・Driver送信シーケンス・Partitionは変更していない。

主なファイル: `src/shell/solar_os_shell_writer_lut.inc`、`src/shell/solar_os_shell_commands.c/.h`、`src/apps/solar_os_shell.c`、`doc/manual/commands.md`。

### Sketch専用パネルの試作と無効化

先に `be95f5b6` でSketch専用タッチパネルを実装した。A／D／E1の選択、確認後の新規Canvas初期化、既存trial統計の開始・停止、一筆終了時に約20物理更新を超えた場合の停止、終了・suspend時の既定復帰を含む。
ただし長い一筆は20更新を超え得る。保存済みPNGを削除する機能ではない。

利用者の要求はShellから選びWriter／Sketchへ引き継ぐことだったため、専用パネルでは不足していた。
特に専用パネルの停止・suspendでD（Sketch A）へ戻る動作が全アプリの選択維持を妨げるため、現在のBuildでは **`SOLAR_OS_SKETCH_LUT_PANEL_TRIAL=0`** とした。
試作コードとHost fixtureは調査用にbranchへ残るが、現在のFirmwareでは専用パネルと自動停止は動作しない。

### Flash前監査の修正

`scripts/papermono_e1_flash_audit.py` のsession解析を修正した。
`writer untit`／`Sketch * - u` のようにTITLEに空白があるsessionでもAPPとSTATEを識別し、開いているWriter／Sketchを見逃さず拒否する。
空白・tab・複数語Shell titleの回帰例を追加した。未保存文書を勝手に保存・破棄して書き込む運用にはしない。

## 3. 実機試験・書込みの経緯

| 順序 | 版／操作 | 確認・利用者観察 |
| --- | --- | --- |
| UI追加前 | 診断版 `aacd0f81`、USB経由でSketch A／D比較 | A区間16物理更新。Dは最初の2更新でやや濃く白化なし、その後多数描画して36更新では白化あり。E1未実施。詳細は既存記録参照。 |
| 専用パネル試作 | `be95f5b6` のApplicationをBuild・Flash | 書込み保護と起動を確認。要求の全アプリ切替を満たさず、Shell版へ変更。専用UIの光学合格は確認していない。 |
| 本体Shell版 | `1e0408f8` のApplicationをFlash | 本体Shell実行経路から1→2→3→4→2を選択し、USBで状態を読んで全選択成功・Driver error=0を確認。光学合格の証拠ではない。 |
| 利用者評価 | 同Shell版 | 全モードで白化、特にShellで顕著。黒化（黒保持）もしなくなったとの報告。試験中止・復元を依頼。 |
| 復元 | UI追加前の既存診断版Applicationを復元 | Application-only照合後起動。D／Sketch Aを確認。復元後の光学評価は行っていない。 |
| 追加観察 | 利用者から時間経過で白化が収まった現象も報告 | 経過時間・更新回数・操作・再起動との対応は未計測。原因未確定。 |
| 再書込み | 同じShell版Applicationを再利用 | 新規backup・読戻し・保護領域照合成功。Launcher／Touch readyのログと利用者の目視・タッチ正常確認。以後の光学比較は未実施。 |

UI追加前のD区間は予定の約20更新を超えた36更新であり、規定の20更新比較として扱わない。
本体Shellの初回確認はUSB `session send` で本体Shellへ実行を依頼したもので、人のキーボード操作・濃さ評価の代用ではない。
再書込み時にはLauncherの背後のShellがsuspendedだったため、固定診断の `session send 0 lut` は拒否された。USB `lut status` は正常。
これを切替失敗や新しい光学試験として数えない。

## 4. 白化・黒保持について未確定な点

- 全モードでの白化・黒保持消失と、時間が経つと白化が収まったという観察を、どちらも保持する。
- `lut` は詳細統計を多行出力する。Shellのスクロール・更新範囲が試験条件に影響する可能性はあるが、今回の原因と確定していない。
- 既存の32更新ごとのfull-RAM再同期は光学的な全面クリーニングと同義ではない。白化の瞬間との対応を測定していないので原因と断定しない。
- 起動・モード切替・Canvas初期化・描画履歴・経過時間の寄与を分離できていない。
- 起動時にPower ICの `update_reg(34): register read failed` が出ることがある。直近の復元・再書込みでも各1件あり、その後Touch readyとLauncher起動が記録された。光学症状との因果関係は未確認。
- 前回回答の「Shellの多行出力が白化の要因」という説明は仮説。短い表示への修正は提案しただけで、実装・検証していない。

次の比較を行う際は、起動からの経過時間、モード切替・画面初期化の順序、操作内容、実更新数と白化発生時点を揃えて記録する必要がある。
現在の版を黒保持改善済み・常用可能と結論付けない。

## 5. Build・検証済み範囲

現在のBuild入口: `scripts/build_papermono_standalone_lut.sh`（Buildのみ、Uploadしない）。
設定はPaperMono／writerdeck／ota、Writer D・dirty hold・SPI20・hybridを有効、waveform試験・latency試験・Sketch専用パネルを無効。

| 成果物 | bytes | SHA-256 |
| --- | ---: | --- |
| 現在のShell版Application | 3,465,424 | `b1e78b6eba3639dff58b89cdab834e60ae89703db4eacdfe88d64a1350b1e218` |
| ELF | — | `919ebf2e776a9c0fcc8c882c646dfaa0cdf480432adbbd338bcc20d5ec9ee505` |
| 照合用Partition Table | 3,072 | `fd976d7c1eaa8f9d0a01ac87a8d05fc1cb1e59d3c5970954b30ad0c3aa12a488` |
| 復元に用いた診断版Application | 3,464,704 | `a16010b78be790704abc42dd890c4575d90a0d172600c739aea97b93ab96b325` |

- Shell版Firmware Build成功: 124.590秒。初回は新コマンドのmanual登録漏れで停止し、登録追加後に成功。
- Shell版Host `writer_d_shell_test` 成功: Terminal／USBの4選択・状態表示・無効入力・無効IO、選択処理自体によるSPI送信なしを確認。本体Shell描画のSPIまで発生しないという意味ではない。
- 先行パネル版では専用fixture、既存46 executable target、追加Asyncモード、既存Python 9件、Flash監査Python 4件を確認済み。Shell変更後に全件を再実行したとは主張しない。
- HostはUbuntuパッケージを `/tmp` に展開したGCC/G++15.2.0とmakeを使用。既存TypeMouse C++警告のみ `-Wno-error=maybe-uninitialized`、新C fixtureは `-Werror`。
- 今回の総括作成では実装修正・新規Build・追加試験・実機操作を行わない。

## 6. Flash保護と証拠

各書込み・復元・再書込みでfresh preflightと全16MB backupを取得し、実機MAC・security・容量・実TableとBuild表・Running=Boot・otadata CRCとslot・容量と消去境界を照合した。
今回確認されたApplication先は `ota_0 @ 0x10000`、容量7MiB。ただし次回もfresh監査が必要で、ここを決め打ちして書かない。

Application-only書込み後、全16MB読戻しでimage一致と対象Application partition外の全byte不変を確認。
Bootloader／Partition Table sector／NVS（BLE bond）／otadata／PHY／非対象OTA／FATの各SHAも一致してからresetした。
標準PlatformIO Upload・factory書込み・全消去は使用していない。

全Flash・NVS／bond・文書・生USBログ・Firmware／ELFはGitへ含めない。
このUbuntuではprivate `.research/manual-shell-lut-20261020/` と `.research/standalone-lut-20261020/`、UI前の `.research/sketch-d-diagnostics/ubuntu-20261020/archive-1650/` に保管する。
親PCにprivate backupが必要な場合は別途安全に受け渡す。Gitだけから既存backupや同一binaryを取得できるわけではない。

## 7. 親PCへ引き継ぐ資料・履歴

- [現在の操作手順](README.md)
- [Shell版Build manifest](build-result.json)
- [初回Shell版の書込み・経路確認](flash-result.md)
- [白化・黒保持不良と診断版への復元](rollback-report.md)
- [最新再書込みと利用者の起動確認](reflash-20261021.md)
- [UI追加前の実測と目視結果](../standalone-sketch-lut-20261020/prior-ubuntu-observations.md)
- [先行Sketch専用パネルの記録](../standalone-sketch-lut-20261020/README.md)

主要実装commit: `be95f5b6`（専用パネル）、`37b0a7a3`（session監査修正）、`1e0408f8`（Shell 4モードとパネル無効化）。
以後の観察・Flash・復元・再Flash・利用者確認はcommit履歴に保持。
古い資料の「未Flash」「手動push」はその資料を作成した時点の状態であり、現在は最新再書込み記録と本報告を優先する。
2026-10-21の利用者指示により、この専用branchの実装・報告書をpushする。元checkoutのbranchと未追跡 `.vscode/`、既存報告書の未コミット編集は保護する。
