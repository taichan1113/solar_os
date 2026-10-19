# PaperMono E1：Ubuntu実機試験と親PCへの引継ぎ（2026-10-20）

作業branch：`test/papermono-e1-ubuntu-20261020`。
基点：`13c1cc1f31ed94cbf5e3a4978fde0421067d27ce`、Firmware実装Commit：`b918c36a1f0b5d1014062149197d4670fde718f1`。
本branchは実機試験資料を追加する。Firmwareソースの修正・mainやIntegrationへのmergeは行っていない。

**本PCの試験は終了。通常D／Sketch A、E1未選択のままUSB接続を終了した。**

## 親PCへ返す主要結果

1. **Sketchの実際のD波形は未評価。** 現行FirmwareはDからSketchを除外している。ユーザーが希望したSketch Dの試験用明示選択追加は、親PCへ引き継ぐ。
2. **Sketch E1は初回の線は十分濃いが、継続描画で急激に薄まった。** 最初の3回と追加32回はすべてE1の正常終了で、設定変更やcleanupを挟んでいない。初回の黒さを継続描画の黒保持PASSとは扱わない。
3. **Writerは文字サイズによる違いがある。** E1のz4は前回同様だが、z7では入力に伴い薄まる。Dのz7はE1より薄まりが小さいというユーザーの印象。
4. **Writerでは連続1文字削除より範囲選択削除の方が黒が復活する傾向。** 操作時刻とTraceを対応付けておらず、文字サイズ・更新領域・更新回数の影響は分離できていない。

[構造化した試験結果](evidence/fastest-e1-ubuntu-20261020/results.json)と[取得済みTraceの抜粋](evidence/fastest-e1-ubuntu-20261020/trace-excerpts.log)を同梱する。実更新数はキー入力数や線の本数ではなく、UI更新を含むパネルの画像更新回数である。

## BuildとApplication-only Flash

- Ubuntu、Python 3.14.4、PlatformIO Core 6.2.0、pioarduino 55.3.312、ESP-IDF 5.5.5、GCC 14.2.0+20260121、esptool 5.4.0。
- `m5paper_mono / writerdeck / ota`。D・Dirty Hold・SPI20・Hybridを有効、Waveform Trial・Sketch Latency Trialを無効。手動手順書の指定構成でBuild成功（513.09秒）。このPCでHost回帰は再実施していない。
- Application：3,461,424 bytes、SHA-256 `9936d825cd0924f1f9a1e41d31b487edb233b93c579cbaca9530529808e4109d`。
- 実機ESP32-S3 revision v0.2・Flash 16MiBを確認。新しい全Flashバックアップと実機表の照合・Running=Boot・raw otadata CRC/slot・消去sector容量の監査を経て、実機から求めた`ota_0 @ 0x010000`だけへ書き込んだ。
- 書込み範囲：`0x00010000–0x0035dfff`。標準PlatformIO Upload・factory・bootloader・partition table・otadataの書込みは行っていない。
- 初回と再書込みの両方で、Reset前の全Flash読戻しが`OFFLINE_READBACK_AND_PROTECTION_PASS`。Application一致、対象Application partition外は全バイト不変。Bootloader・Partition Table・NVS/bond・FAT・otadata・phy_init・非対象OTAの個別SHAも一致。
- 新FirmwareのELF SHA prefix `b281c0799`、USB Shell、通常D、E1未選択、Startup `auto`、保存ファイル一覧、6項目Launcherとタッチを確認。物理表示・タッチはユーザー確認。

バックアップ・Firmware binary・保存文書やbondを含むFlash・生USBログはGitに追加しない。Ubuntu PCのローカル保管先：

```text
/home/bp/Documents/papermono-e1-backup-20261019-235628
/home/bp/Documents/papermono-e1-reflash-backup-20261020-003931
/home/bp/Documents/papermono-e1-comparison-20261020-001309
```

親PCへはこれらのファイルを自動転送していない。Flash復旧時は各回のApplicationバックアップと、その直前の新規バックアップを使う。古い全Flashを丸ごと戻さない。

## 初回の目視結果

比較条件はFASTEST / tile / rows-dual / 20MHz。各条件の初期化はA選択→cleanup予約→次のpresentのOTP実更新をTraceで確認。Writerは保存不要のuntitled文書に`BLACK`と`012345`、3行目で`x`約1秒長押しとBackspace約1秒（空になったら離す）を指示した。入力時間はユーザー実測なし。写真・動画・ROI解析はしていない。

| 条件 | ユーザー目視 | 試験区間の実更新 | 備考 |
|---|---|---:|---|
| Writer A | 全体が薄まる。読めるがはっきり薄い。削除残像は気にならない。速度良好 | A 24回、no-op 1件 | 初期表示に対する薄まり |
| Writer D | 十分濃い。一瞬白化するが黒に復帰。残像は気にならない。速度良好 | D 22回 | 全記録`err=0 / restored=1` |
| Writer E1 | Dと大差がないように思える | E1 24回 | Dとの差は判定困難。個別項目の同等性を断定しない |
| Sketch A | 線は薄い。描くごと全体も薄まる。濃さ以外は良好 | A 87回、no-op 2件 | Traceは直近32件のみ。全87回の個別記録は残っていない |

Writer D/E1とSketch Aの上記区間でDriver failure/errorはなかった。黒さは目視の定性的評価であり、黒濃度や寿命の数値にはしない。

**D選択中のSketchはAである。** Sketch D波形を評価したという意味で報告しない。E1だけがSketchにも実験波形を適用する。

## Sketch E1：無反応報告と再書込み

最初のE1試験ではユーザーから画面無反応の報告があった。USB Shellは応答しており、確認時点ではSketch sessionがなくWriterがactive、Touchは`ready=no`だった。E1選択後のSketch記録はno-opだけで、SketchのE1物理描線更新は確認できなかった。全体のFirmware hangやE1光学効果の失敗と断定する証拠にはしない。原因未確定。

ユーザーが保存不要の試験Writerを閉じることを許可し、Shellだけになったことを確認してから、別archiveで新規バックアップを採って同じApplicationを再書込みした。保護領域一致と起動・Launcher・タッチ正常を確認。修正Firmwareを導入したわけではなく、再発原因を解決したとは扱わない。

## 再書込み後のSketch E1

新しいSketchをactiveにして、E1選択前後のTouch readyを確認した。Aでcleanupし空Canvasを作った後にE1を選択。黒・freehand・2px・solidの初期設定を使い、最初は短い線1本だけを依頼した。

| 区間 | ユーザー目視 | E1実更新 | RAM転送とQueue |
|---|---|---:|---|
| 最初の一筆 | 描けた。線は十分濃い。追従性は追加確認したい | 3回、seq72–74 | 192,000B×2回と900B×1回。Queue submitted +14 / replaced +11 |
| 追加描画 | 最初のゆっくりした直線で、線やその他の黒部分が急激に薄まった | 入力区間全体32回、seq75–106。累計35回 | 主に局所行転送、seq105で192,000Bの全RAM再同期。Queue submitted +32 / replaced +0 |

両区間の設定・LUTはE1のまま。間のcleanupやOTP更新はなし。全35記録で`source=sketch mode=E1 0x20=1 err=0 restored=1`。`restored=1`は更新後にA LUTをRAMへ戻したことで、E1選択解除やAでの追加物理更新を意味しない。

更新の反復回数と全RAM/局所行転送の割合が変わっている。32更新周期のRAM再同期も記録された。ただし薄まりの瞬間を特定のseqへ対応付けていないため、反復・RAM経路・その他の要因のどれが原因かは未確定。追加32回全部を「薄まった直線1本」の更新回数と断定しない。**最初の一筆の十分な黒さから、E1の継続描画時の黒保持を推定してはいけない。Driver成功は光学品質のPASSではない。**

急激な薄まりを記録した後は追加入力を止め、物理更新を伴わない`candidate_d`で通常D／Sketch Aへ戻した。追加cleanup・自動再試行はしていない。

## Power/USBで残すべき事項

- 通常のpyserial openでUSB resetを観測したため、比較用consoleはDTR/RTSとbaudを変えずraw fdで開き、HUPCLを無効にして継続接続した。
- 初回Flash後のbootでPower IC register read失敗が1件あり、Touch ready・Launcher起動・ユーザーのタッチ正常確認まで進んだ。再書込み後のbootでは同エラーなし。
- 最終追試の準備中（01:00頃）にユーザーがKEYを押したことを確認。同時刻にPower IC read失敗2件、FT6336 enable cleanup失敗、Suspend/Resume処理、のちのTouch readyを観測した。KEY操作との時間的対応はあるが、I2Cエラー自体の原因は未確定。Driver error counterは0だった。

## 最終再現確認と終了状態

ユーザー依頼で濃さの再現を確認した。D/E1の独立した再確認前はA選択・cleanup・初期文書をそろえたが、最後のD z7はE1からの連続切替であり、同一初期状態の比較ではない。

- D選択時のSketch（実波形A）：cleanupを確認して再試験。ユーザー評価「線は薄い。Aで薄まるのは既知」。A実更新34回、D/E1更新0回、Driver error 0。直近32件のTraceはすべて`source=sketch mode=A`。ユーザーの希望は実際のD波形をSketchで試すことであり、この再試験はその評価になっていない。
- Writer Dの濃さ再現：再現。ユーザー「前回と同様。何度か書き試しした」。十分な黒さ、一瞬白化して黒に復帰、残像・速度良好という前回評価を再確認した。区間内D実更新134回、no-op 1件、Driver error 0。個別Traceは直近32件のみ。入力は約1秒を1回と指示したが、ユーザーが追加の試し書きを行っており、反復回数・時間は未計測。厳密な時間一致比較や長期安定性の証明とは扱わない。
- Writer E1の濃さ再現：文字サイズに依存する目視傾向を発見。ユーザー「z4では前回同様、z7だとタイプするにつれて薄くなった」。区間内E1実更新236回、Writer no-op 5件、Driver error 0。z4/z7の境界時刻・各サイズの更新回数は記録できていないため、全236回を片方のサイズへ割り当てない。入力時間と反復回数は未計測。
- Writer Dのz7追加確認：ユーザー「Eに比べると薄まりが小さい気がする。1文字ずつ連続で消去するより、範囲選択して文字を消したときの方が黒が復活する傾向」。D実更新146回、Writer no-op 2件、Driver error 0。この区間のA/E1/OTP追加更新は0回。E1からz7・同文書のまま連続切替しcleanupなしのため、同一初期化条件での独立比較ではない。削除操作の時刻とseqも対応付けていない。D優位や削除方法による復帰を定量的に証明した結果とは扱わない。
- Shell A/D/E1、固定文字保持の独立試験：未実施。ユーザー指示でSketchを優先した。

試験終了時の表示選択は`mode=D scope=all-apps-except-sketch`、Driver error/failed/queue errorは0。Writerのuntitled試験文書がactive、Sketchの試験Canvasはsuspendedのまま保持し、自動保存・強制終了・追加cleanup・Resetはしていない。USB consoleはDTR/RTSを変えず終了した。長期影響・定量濃度・各操作条件を分離した因果は未確認。

試験記録だけをcommit/pushし、既存の未追跡`.vscode/`は開始時SHAと一致した状態で保持する。`.vscode/`をcommitしない。再確認のためのFirmware再Buildや改修は行っておらず、実機Applicationは本報告のSHAのままである。

現行FirmwareにはSketchでDを明示選択するCLIがない。`epd_ssd1677_writer_d.inc`の`origin.is_sketch && !e1_selected`条件でAへ分岐する。実際のSketch D評価には試験用の明示選択を追加し、再Build・検証・新規バックアップ付きApplication-only Flashが必要。現時点でFirmware改修はしていない。

ユーザーは「WriterのD／E1再確認を終え、Sketch D対応は親PCへ引き継ぐ」を選択した。本PCでSketch D対応を追加しない。Writer再確認用のApp切替時にもPower IC read失敗を1件記録したが、Writer作成・描画は進み、Driver error counterは0だった。Touchを使うSketchからWriterへの切替ではTouchの停止も記録されるため、Writer active時のTouch `ready=no`だけを故障の証拠にしない。

## 親PCのCodexへ

本branchを取得し、この報告と元の`doc/evidence/fastest-e1-trial/manual-flash-and-test.md`を読む。親PCへ自動Flashや追加試験を依頼したことにはしない。

親PC側の既存変更・HEADを保護し、既存のmain/Integrationへ直接mergeせず独立したcheckout/worktreeでこのbranchを読む。

次の検討事項：

1. 通常bootのD／Sketch A方針を維持しながら、USB Shellから試験時だけSketch Dを明示選択できる手段を追加する。選択状態と実際の`source=sketch mode=D`の更新実績を区別して検証する。本PCでその改修はしていない。
2. Sketch E1の「初回は濃いが繰返しで急激に薄まる」を切り分ける。`epd_ssd1677_reference.inc`のcold/full-RAM入口・rows-dualの局所行転送・32更新周期再同期・反復回数と、実パネルの黒保持を対応付ける。
3. Writerのz4/z7、1文字ずつの削除/範囲削除を、初期化・同じ文書・操作時間・反復回数をそろえて別条件として比較する。dirty領域の高さ・RAM転送量・物理更新数も記録し、ユーザーの目視タイミングとTraceを対応付ける。今回のサイズ依存という目視だけで、LUT単体やフォント自体を原因と断定しない。
4. KEY周辺とApp切替時のPower IC/FT6336エラー、最初の無反応報告は光学問題から区別して扱う。無反応報告時はSketch sessionがなかったため、E1の描線更新失敗と断定しない。

LUT値変更・pacing追加・RAM処理変更を現時点の正解とは決めていない。実機試験を再開する場合はユーザーの準備を確認し、Flashが必要なら毎回freshバックアップ・slot/partition監査・Application-only・起動前の全保護領域照合を行う。
