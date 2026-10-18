# SSD1677 Candidate D：最大3回・段階実行Trialの実装準備報告

2026-10-18。**実装・Host検証・ESP32-S3ビルド完了。Flash承認待ち。**

依頼書§6「ここで必ず一度停止する」に従い、本報告で停止する。今回はCOMポートの検索・接続、Flash、EPD更新、光学測定を実施していない。実機のFirmware・FAT上のstartup・保存文書・bondは操作していない。R-1〜R-5は実機評価後の分類であり、現段階では付与しない。

## 実装・Git

| 項目 | 結果 |
|---|---|
| Branch | `experiment/papermono-black-hold-d-repeat3` |
| Worktree | `E:\ESP32\M5PapserMono-SolarOS\.research\black-hold-d-repeat3\worktree` |
| 基点 | B/D成果・報告済みHEAD `a65d8ec83b5ae2bff5ace40e8032f047657a0ffd` |
| 実装Commit | `e1935704df27d6d0e7e5cf25c7d63133c8c74faa` |
| 既存成果 | B/Dの同一画像、Display claim、immutable submission、OTP初期化・cleanup、LUT復帰、ログ、Host RAMモデルを再利用 |
| 既存作業の保護 | 既存25 WorktreeのHEAD・Branch・git status、およびsnapshot記録対象の編集ファイルhashが一致 |
| 元Worktree | `experiment/papermono-sketch-latency-measurement` / `31abd6db216bd94833ae092f6948438f8f1e4256`、既存未追跡`.vscode/`を維持 |

実装変更はDriverの診断回数管理、専用Shell診断、ビルドフラグとHostテストに限定した。Apps / Services / Board / partition / 通常の`ssd1677_reference_refresh`本体は基点と同じ。Writer、Sketch、TypeMouse、Dirty rows-dualのアルゴリズムは変更していない。実用Branchへのmerge/pushは行っていない。

変更ファイル：

- `src/drivers/epd_ssd1677.{c,h}`：起動中のA/D試行予算と問い合わせ。
- `src/drivers/epd_ssd1677_reference.inc`、`epd_ssd1677_waveform_trial.inc`：反復ビルドからB選択を除外。LUT配列変更なし。
- `src/shell/solar_os_shell_black_hold_single.inc`、`solar_os_shell_commands.c`：既存診断をA/Dの段階実行へ拡張。
- `scripts/platformio_solaros_flavor.py`、`src/CMakeLists.txt`：専用フラグを追加。
- `tests/host/Makefile`、`black_single_shell_test.c`、新規`epd_ssd1677_black_repeat3_test.c`、`black_repeat3_shell_test.c`。

## 波形・通常動作

| 対象 | LUT0 | 内容 |
|---|---|---|
| A | `00 00 00 00 00 00 00 00 00 00` | 従来Baseline HOLD |
| D | `2F F0 00 00 00 00 00 00 00 00` | HOLD×2F → −15V×2F → ＋5V×6F、計10F |

110バイト配列は既存B/D版から完全不変。BaselineとDの差はoffset 0/1のみで、LUT1〜4、Timing/FR、Gate/Source/VCOMは一致。独立デコードと実DriverのSPI送信検証で確認した。

- Baseline SHA-256：`e8a7abbb2110cb8d941e80cc2869bbe1d0814e1d4a38e5d0cc5173b10f39ce50`
- D SHA-256：`9be4bea8b9a07fef33834293d25979a9b0f3fc820d3e9912a1fc106532ef1832`

通常描画はBaseline。DはUSB Shell専用診断で予約された1更新だけに適用し、BUSY完了後にBaseline LUTへ戻す。LUT復帰のための追加`0x20`はない。各測定は`0x32`が2回（選択・復帰）、`0x03=17` / `0x04=46 A8 32` / `0x2C=30`が各1回、`0x22=0C` / `0x20`が各1回。初回customのみ既存どおり`0x22=CC`。

公称Source時間積`−15×2＋5×6=0 V・frame`は、実効DCバランス・残留電荷・寿命・メーカー適合の証明ではない。3回に制限した場合のパネルへの無害性も未確認。

## 段階実行と上限

専用コマンド（承認後に使用）：

```text
display test display0 black-hold-repeat3 A
display test display0 black-hold-repeat3 D
```

USB serial、foreground Shell、正常な800×480/R0またはR2、20MHz/tile/Baseline、Dirty rows-dualを開始前に検査する。従来のB/D単発コマンドはこの反復ビルドに登録せず、B/Cの波形配列が実Binaryにないことも確認した。

| 段階 | 処理・停止位置 |
|---|---|
| 開始 | OTP白 → `white` gate |
| 白の正常確認後に`g` | OTP共通画像 → Baseline初回custom → 完了＋5秒でT0 → `T0-next` gate |
| T0で確認後に`g` | 1回分を予約 → 操作受領＋5秒で測定1 → 完了＋2秒でT1 → `T1-next` gate |
| T1で確認後に`g` | 同じ経路で測定2 → T2 → `T2-next` gate |
| T2で確認後に`g` | 同じ経路で測定3 → T3 → `T3-cleanup` gate |
| T3の撮影・確認後 | `g`または停止操作で、正常Controllerに限り予定のOTP cleanupを1回実施して終了 |

`q` / Esc / Ctrl+Cはgateと待機中の停止操作。3回の一括自動実行、次条件への自動移行、失敗の自動再試行はない。T0/1/2/3のmarker自身は表示を更新しない。PC側capture helperも実際のgateごとに1バイトの明示操作だけを受け付け、まとめた`ggg`を拒否する（今回は構文検証のみで未接続）。

**セッションとDriver予算は別管理**：

- ShellはA/Dそれぞれ起動中1セッションのみ。途中でやめても同じ条件を再開できない。
- Driverは起動中の予約回数をA/D各3回まで保持。予約は実際の待機・送信より前に消費し、cancel/失敗で返却しない。4回目は送信前に拒否。
- 準備用Baseline初回customは測定Aの予算と別で、T0の測定カウンタは0。
- T0で予約前に停止した場合、Driver試行回数は0だが、そのShellセッションは再取得できない。
- Displayの再初期化やController resetでは予算と失敗latchを解除しない。失敗後はA/D両条件を拒否する。
- **本体全体の再起動でRAMガードは失われる。** NVSへのCandidate永続化はしない。試験途中の再起動で予算を再取得して継続する運用は行わない。

T0は準備完了＋5秒、各測定は操作者の`g`受領＋5秒、Tnは測定完了＋2秒。以前の単発試験の「準備完了＋10秒で必ず測定」とは異なり、各gateでの応答時間が追加される。A/Dの実際の更新間隔はログから算出し、一致しない場合はそのまま記録する。

| 診断内工程／条件 | 予定`0x20`回数 |
|---|---:|
| OTP白 | 2 |
| OTP共通画像 | 2 |
| Baseline初回custom | 1 |
| 測定 | 最大3 |
| OTP cleanup | 2 |
| 全3回正常完了時の合計 | 10（A/D合計20） |

通常Shell/Launcherの描画は上表と別。ログには測定成功数`completed`、起動中予約数`boot_attempts`、診断内実送信数`diagnostic_cmd20`を分けて残す。異常では予約数・実送信数・成功数が異なるため、実送信ログを監査する。

通常の無変更更新省略は維持し、診断の同一画像だけを強制更新する。全RAM強制診断のpayloadは通常144,000 B、既存32回周期再同期に該当すると192,000 B。RAM再同期が入っても測定`0x20`は1回。通常Writerの局所Dirty性能を測る試験ではない。

## Host検証

| 検証 | 結果 |
|---|---|
| 新Driver反復テスト | PASS：A/D各0→3、4回目拒否、再初期化、reserve/cancel/失敗、同一payload、R0/R2、RAM保持/面切替の両モデル、周期RAM再同期 |
| 新Shell反復17ケース | PASS：normal/r2/delay、A/D各Driver失敗・復帰失敗・submit失敗・初期待機停止・予約後cancel・T1/T2停止 |
| 既存B/D Driver・Shell10ケース | PASS：従来の反復フラグOFF動作も維持 |
| 既存Waveform Trial | PASS |
| 独立LUT Python | 2件PASS |
| 主要Host実行ファイル | 19本PASS |
| Sketch Python | 9件PASS |

主要19本はSSD1677 / Dirty / SPI20 / Hybrid、Sketchのpipeline・canvas・lifecycle・140%・legacy・touch projection・trace、Display Async、Writer quality/buffer/files、Launcher touch、TypeMouse engine/lifecycle、Shell launch。

SPI送信内容はA/Dの先頭LUT2バイト以外同一、Shadowと両RAMの整合、通常fastestへのBaseline復帰、各測定の`0x20=1`を検証した。RAM面切替についてHost両モデルのPASSは実Controller内部状態の新たな実証ではない。

初期の新規テストでmainマクロ衝突と、故障注入用BUSY状態のケース間持越しが発生し、テストfixtureを修正した。最終ログは修正後の全PASS。初回から全PASSだったとは扱わず、初期ログも別名でローカル保存している。実機試験の失敗ではない。

## ESP32-S3成果物

| 項目 | 結果 |
|---|---|
| Build | PASS、約277.6秒、Compiler/CMake warning 0 / error 0 |
| Board / Flavor / Layout | `m5paper_mono` / `writerdeck` / `ota` |
| SDK / Compiler | ESP-IDF 5.5.5 / GCC 14.2.0 |
| Application-only binary | **3,460,144 B** |
| SHA-256 | `26acd03ca96f608cf3602466c0efd239cdb6f17ac475e317e124c90904af8e4a` |
| 1 OTA slot容量／残量 | 7,340,032 B／3,879,888 B（binaryサイズ基準） |
| PlatformIO表示Flash使用量 | 3,454,127 B（ELF集計でありbinaryサイズとは別） |
| RAM静的使用量 | 53,152 / 327,680 B |
| 成果物の絶対パス | `E:\ESP32\M5PapserMono-SolarOS\.research\black-hold-d-repeat3\artifacts\firmware-app-only.bin` |

実compile command・build-selectionで確認したマクロは、接頭辞`SOLAR_OS_SSD1677_`を付けて、`BLACK_HOLD_SINGLE_TRIAL=1`、`BLACK_HOLD_REPEAT3_TRIAL=1`、`DIRTY_HOLD_TRIAL=1`、`SPI20_TRIAL=1`、`HYBRID_TRIAL=1`、`WAVEFORM_TRIAL=0`。別接頭辞の`SOLAR_OS_SKETCH_LATENCY_TRIAL=0`。HYBRIDは既存のコンパイル機能で、この測定はfastest。Sketch高速化の通常実装は基点から維持される。

反復フラグは既定0で、SINGLE backend=1との依存をビルド時に検査する。永続Waveform Trialとは併用しない。SDK CONFIG集合（FreeRTOS 1000Hzを含む）とpartition binaryは既存B/Dビルドと一致。

ビルド上の`ota_0=0x10000`、7MiBは参照情報であり、実機の書込み先を確定したものではない。ビルドが自動生成するfactory binaryは採用せず、Application-onlyだけを成果物として選択した。`partitions-reference-only.bin`は照合専用で、書込み用ではない。

## 承認後の実機手順・評価

新しいFlash承認を受けてから、競合、未保存編集、COM/ESP32-S3/16MB、実行・起動slot、partitionを現物確認する。現行全FlashとApplicationを新規バックアップし、書込み先を検証してApplication-only Flash、読戻しSHAと保護領域を照合する。Bootloader/partition/NVS/FAT/otadata/PHY/bondを保持する。

ローカル復元参照Application（今回もhash照合済み）はIntegration `391925abff2d261d1d5856711aa0a1f101ac435a`、SHA `278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6`。前回はこのIntegrationへ復元してユーザーが正常確認したが、今回の現行実機を照会したものではない。承認後は現物と新規バックアップを優先し、既存参照だけで復元先を決めない。

A/D各独立のOTP初期化後、T0〜T3を連続動画に記録し、毎回の目視結果とユーザーの次回実行判断を受けて1回ずつ進む。重大異常では追加更新を停止し、Controller正常時だけ予定cleanup、ログ保存、必要なRollbackを行う。光学外乱を理由に試行予算をリセットしない。終了後は元IntegrationへApplication-only復元し、読戻し・保護領域・USB Shellと表示を確認する。

評価予定は黒四角・固定文字を別々に、`C=(白−黒)/白`、C0〜C3、`ΔC_n=C_n−C0`、`dC_n=C_n−C_(n−1)`、`Δ改善_n=ΔC_D,n−ΔC_A,n`。各更新で局所直前C、raw最小C、3frame移動中央値の最小、安定後C、白背景driftを記録する。前回のROI・PTS照合・外乱確認手順を再利用する。

前回の4K30/固定露出・Gain/フォーカス9%/自然光混入可能は過去の記録であり、今回の撮影条件は承認後に再確認する。反射、黒つぶれ、白飛び、フレーム欠落・PTS、照明変動・温度未測定を明示し、外乱が大きい区間から効果順位を断定しない。30fpsなら約33ms刻みのためraw谷を厳密な最大白化としない。ログのBUSY値はAPI待機相当で、BUSY立上りedgeや画素変化開始の光学時刻とは別。各条件3回以下なので速度の統計的同等性を主張しない。

**実機のSPI実効値、SPI/BUSY/総時間、C0〜C3、黒濃度保持効果、ユーザー目視、実送信回数、Flash/復元結果はすべて未実施。** Host結果を光学改善・実効DC・長期安全性のPASSに代用しない。6回Trial、実用統合、新Candidateへ自動移行しない。

## 証跡

[証跡一覧](evidence/black-hold-d-repeat3-20261018/evidence-index.json)、[成果物manifest](evidence/black-hold-d-repeat3-20261018/manifest.json)、[LUTデコード](evidence/black-hold-d-repeat3-20261018/lut-decode.json)、[専用Hostログ](evidence/black-hold-d-repeat3-20261018/host-single-verified.log)、[主要回帰ログ](evidence/black-hold-d-repeat3-20261018/host-regressions.log)、[buildログ](evidence/black-hold-d-repeat3-20261018/build.log)、[既存Worktree監査](evidence/black-hold-d-repeat3-20261018/other-worktrees-after.json)。検証用PythonとShell runnerも証跡に保存する。Firmware binaryはGitに追加せず上記ローカル成果物に保持する。
