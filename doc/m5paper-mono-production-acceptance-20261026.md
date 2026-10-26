# M5Paper MONO 本採用Firmware 実機Acceptance — 2026-10-26

**Acceptance未達（FAIL 3項目、採用確定は保留）。Application-only投入とデータ保持確認は成功したが、Sketchのタッチ停止・SD保存失敗・特定手順でのSD再挿入失敗が実機で発生した。** Sleep/Wake・USB再接続はPASS。最終再起動後のLauncher表示もユーザーが正常確認した。

## 対象・投入

| 項目 | 結果 |
|---|---|
| Branch | `feature/papermono-production-integration` |
| Firmware実装Commit | `ea7b10e49a0a191947b3f227e855e9335a7e0616` |
| 開始HEAD | `ea75186a20443dc6d959c86b57abde31c481d928` |
| 統合基点 | `aa08bbfdfa65c72ada0a0bc7737d36b47a521186` |
| Firmware | `production-20261026/firmware.bin`、3,516,816 bytes |
| SHA-256 | `86b735ef56e2a5a6f4400bb5d02985ee5c127b10f85eb165e3d1337d1051973b` |
| 実機 | M5Paper MONO / ESP32-S3、USB VID:PID `303a:1001`、COM39、16MiB Flash |
| 起動先 | 実機Running/Boot、partition table、CRC付きotadataが一致：`ota_0`、`0x10000`、容量`0x700000` |
| Application-only | **PASS**。指定Binaryの形式・checksum・内蔵SHA・サイズを照合後、現在slotのみに書込み |
| 読戻し | **PASS**。投入長のSHA-256一致。16MiB全域を再読取りしApplication外の全バイト一致 |
| Rollback | 直前の実機Applicationを取得・形式検証済み。未実施。Git上の旧Binaryを原本と仮定していない |

対象の再Build・SDK更新・Firmware修正・別Binaryへの差替えは行っていない。事前Build/Host結果は[統合成果物manifest](m5paper-mono-production-integration-artifact-20261026.json)を参照。以前のHost PASSを今回の実機PASSへ繰り上げていない。

## 保護・ローカル証跡

Flash前に16MiB全域、Application原本、bootloader、partition table、otadata、NVS、PHY、FATを保存した。再起動前の照合ではbootloader・table・NVS・otadata・PHY・未使用OTA slot・FATの7領域、およびApplication外の全バイトが一致した。NVSに含まれるBLE bond・Wi-Fi設定、FAT内の文書・画像・startup・履歴へのFlash書込みはない。

FATのオフライン解析で既存59ファイルのサイズ/hash一覧とstartup原文を取得した。startupは153 bytes、SHA-256 `2ee2925a9776fd3cb0996ea9ad413bbb8856c38fb02c89047bd07f0b27ee28bd`。ファイルを編集していない。最終読取りでもstartup・partition table・otadata・指定Applicationが一致した。

最終FATは63ファイル。既存59件に削除はなく、57件はhash一致。差分2件はShell履歴とReader `positions`で、後者は元の24行を全て保持して閲覧位置3行を追加したもの。追加4件は専用Flash文書・新規PNG・Flash文書用/SD試験用のWriter checkpoint各1件。既存Writer文書・PNG等の内容差分はない。新規PNGの全chunk CRCもオフライン検証した。

NVSの物理領域は通常起動後に変化していたが、57論理値を比較すると差分は`wireguard/tai_ceiling`の正しい起動予約の増加と`phy/cal_data`のSDK校正更新のみ。19件のNimBLE bond値、記憶済みBLE peer、Wi-Fi資格情報を含む他の論理値は一致した。PHY partition自体は一致。これらは**最終読取り時点、最後の通常再起動前**の照合結果であり、再起動後の履歴・単調時刻予約を巻き戻していない。最後の再起動後にもWi-Fi・BLE再接続とUSB Shellを確認した。

私有の生バックアップ・文書・資格情報・ファイル一覧は `.research/production-acceptance-20261026/run-20261026T085105/` にローカル保存し、Commit/Push対象にしない。公開報告にはSSID、鍵、既存文書本文、機体MACを記載しない。

## Acceptance一覧

| 項目 | 判定 | 実機で確認した内容・制限 |
|---|---|---|
| SD未挿入Boot | PASS | Flash `/`、Shell/Launcher/CDC起動。SD不在をBoot失敗としない |
| Launcher | PASS | Manual/Reader/Wi-Fi、Keyboard/Sketch/Clockの3列×2行。配置・黒文字・白背景・タッチをユーザー確認 |
| USB Shell初回・抜き差し | PASS | CDC Shell 1 Session、画面Shellと共存、診断コマンド応答。ユーザーによる抜き差し後も1 Session・応答正常 |
| startup実行回数 | NOT TESTED | 画面Shell→LauncherとCDC 1 Session、実効設定を確認。USB側startup無効はソース確認済みだが、startup本文の実行回数を実機の専用counterで数えていない |
| 描画設定 | PASS | `refresh=fastest`、F/all-apps、SPI実効20MHz、Async enabled。F実行成功のDriver条件はtile/rows-dualを要求 |
| F更新の短期安定性 | PASS | 再現時のstatusでF physical=504 / success=504 / error=0、Async busy/pending/error=0。速度比較や長期波形試験は行っていない |
| 全RAM同期32回周期 | NOT TESTED | 自然操作中にperiodic full-RAM resyncログは観測。正確な32回間隔を独立計数していない |
| Wi-Fi再接続 | PASS | 既存保存先へ接続、Boot enabled、AP/NAT off。設定は変更していない |
| Wi-Fi保存OFF／接続先なしBoot | NOT TESTED | 現機は保存接続あり・ON。NVSを変更して作り出していない |
| NTP非自動起動 | PASS | `ntp-sync` stopped。startupにNTP自動行なし |
| RTC／BLEキーボード | PASS | 日付・Clock表示と保存済み外部キーボード再接続。時刻の精密校正は対象外 |
| Shell Esc | PASS | 空行EscはShell維持、入力中Escは入力取消、明示`launcher`で遷移をユーザー確認 |
| Manual一覧／Reader復帰 | PASS | スワイプ・開閉・ページ表示・位置保持・矢印/Enter操作・Launcher復帰をユーザー確認。内蔵ガイドでの試験 |
| TypeMouse | PASS | 42キー表示、A入力・反転、Shift固定/近接Z/解除、Mouse移動、Exit、Clock/B復帰をユーザー確認。全修飾キーと全Mouseジェスチャーの再試験ではない |
| Writer / Flash | PASS | 専用文書でJIS記号・改行・削除・黒品質・追従・Alt+Tab復帰・保存再読込をユーザー確認 |
| Writer checkpoint基本 | PASS | 約3秒待機・切替で内容維持。最終FATの専用Flash文書とSD試験用checkpointは形式・checksum正常、保存済みマーカー。強制電断からの復旧はNOT TESTED。SD側マーカーは試験終了時の状態であり、SD保存成功を示すものではない |
| Sketch描画・新規PNG保存 | PASS | 短線・連続線・円・消去・末端・黒品質・Saveまでユーザー確認。新規`/Sketches/sketch003.png`を実機一覧と最終FATでも確認、全PNG chunk CRC正常。再読込後の光学的な完全一致は独立確認していない |
| Sketch保存後／再入後タッチ | **FAIL** | 後述の再現あり。PNG再読込後にも発生 |
| SD認識・4bit・mount | PASS | SDHC 59,200MiB、20MHz、SSR bus_width=4。256MiB FAT32 `sd0p1`を`/sdcard`へmount。別57.6GiB領域は未対応FSとして触れていない |
| SD一覧／Unmount/Remount | PASS | カード既存一覧を私有ログへ取得、Unmount/Remount成功 |
| SD専用ファイル書込み | **FAIL** | `cp`後の目的ファイルが存在せず、Writerからも`save failed` |
| SD書込み後の内容照合 | BLOCKED | 専用ファイルの作成が成立していない |
| SDとFlashの分離 | PASS | SD mount後もFlash `/`、SD `/sdcard`、startup saved=auto / effective=flash。Flash専用Writer文書とPNGが従来側に保存された |
| SD mount中Sleep/Wake | PASS | Sleep中タッチ無反応、KEY1 Wake、Manualタッチ復帰をユーザー確認。復帰後もSD mount・Flash `/`・startup effective=flash、Async error=0。専用ファイル読戻しは保存失敗のためBLOCKED |
| SD抜去・再挿入 | **FAIL** | Unmount→確認用lsblkで再初期化→ユーザー挿抜→lsblkでセクタ読取りtimeout `0x107`、sd0p1消失、mount not found。Flash `/`とstartupは維持 |
| 完全版SD Manual／オフライン／版違い・署名 | BLOCKED | mount前後とも`External manual: built-in setup guide`。有効な署名付き外部Manualは確認できず、独自版配布基盤も未整備。`help update`や署名回避は実行していない |

## 実機で見つかった問題

### 1. Sketchのタッチ有効化leaseが消失する — 高優先度

最初の手動試験でSave後や再入後にタッチ無反応との報告があった。限定再現は、新規試験PNGを開く→Save→保存通知のOKが反応しない→Escで通知を閉じても描けない、となった。

その状態でSketch Sessionはactive、`touch0 ready=no`、`manual enabled=no`、`enable leases=0`、`requested enabled=no`を実測した。同時刻に `sketch: pointer enable failed: ESP_ERR_INVALID_STATE`（log seq=300）。Asyncはidle/error=0で、単なるEPD BUSY停止や、通知を閉じるための1タップ消費とは区別できる。Sketch終了後のClockタッチはユーザーが正常確認した。

ソース上の追跡箇所は[Sketch acquire](../src/apps/solar_os_sketch.c#L142)、[suspend/resume](../src/apps/solar_os_sketch.c#L1503)、[CANCEL adapter](../src/services/solar_os_sessions_touch_adapter.inc#L35)、[enable状態遷移の拒否](../src/services/solar_os_expansion.c#L1581)。CANCEL時のsuspend/resumeとenable遷移の再入は原因候補だが、発端と全順序は未確定。保存処理そのものやF波形を根本原因と断定していない。今回の固定Binaryに修正を混ぜていない。

### 2. SD保存が成立しない — 高優先度

カード一覧取得・mountは成功するが、未使用の専用名に`cp`しても後続`ls`/`cat`はENOENT。短い8.3名でも同じで、Unmount/Remount後も存在しない。`cp`の引数なし・存在しないsourceでは所定のエラーが返る。続いてWriterでSD専用名に`SD OK`を保存しても、ユーザーが`save failed`を確認した。

共通の保存・パス/VFS・partition mapping・カード書込み経路を追跡する必要がある。`df`実測はFAT32 total=252.0MiB / used=50.2MiB / free=201.8MiBで、空き容量不足ではない。カード故障、readonly、原因となるソース箇所は未確定。`cp`の無出力だけで成功判定していない。既存SDファイルの上書き・format・未対応partition操作・無制限再試行は行っていない。

### 3. SD挿抜後の再検出に失敗 — 高優先度

`disk umount sd0p1`は成功した。続く確認用`disk lsblk`はカードを再初期化し、SDのmount先が`-`であることを確認した。その後ユーザーが抜去・再挿入すると、`disk lsblk`で`sdmmc_read_sectors_dma`がtimeout `0x107`、partitionが列挙されず`disk mount sd0p1 /sdcard`もnot foundとなった。

この具体的な操作順でのFAILであり、全ての挿抜手順で必ず失敗すると拡張していない。[ensure_card_ready](../src/drivers/sd_card.c#L832)は検出後に`card_ready`で初期化を省略し、[rescan](../src/drivers/sd_card.c#L1293)がpartitionを読み直す。未観測の挿抜後に古いhost/card状態を使った可能性があり、追加切分けが必要。ユーザー申告では既存使用カードである。既存partitionは有効なFAT32として一覧・空き容量を読めていたため、単に「formatが必要」とは確定できない。カード内容の全面hash比較やfilesystem検査はしていない。

## startup整理候補・次の作業

実機startupにはUSB Shell生成、fastest/tile、旧`lut 5`、Wi-Fi ONの指定があり、NTP自動行はない。今回の最終実効値はF/fastestで、既定と重複する行の整理は未適用。USB自動生成の重複Sessionもない。startup行整理と、復旧・通常操作用コマンドの削除は別件とする。

推奨順は、Sketchのlease喪失修正、SD保存失敗の原因切分けと最小修正、今回のFAIL項目の再Acceptance、startup整理、Sketch二方式比較、独自版の署名付きManual配布整備。Fの寿命・DCバランスは今回評価していない。Integration/mainへのMergeは行わない。

## 最終状態・Rollback

実機は指定の本採用統合Binaryのまま（`ota_0`、SHA-256は冒頭と同じ）、SDは取り外し済み。最終照合はFlashの読取りだけで、書込み・eraseは0回。読取り後に通常再起動し、Launcher、F/all-apps、Async、USB Shell 1 Session、ZMK/Wi-Fi接続の復旧を確認した。ユーザーの最終回答は「Launcher見た目正常」。

今回のFAILはBoot不能や保護領域破壊ではないため、無断で別Firmwareへ差し替えたりRollbackしたりしていない。Rollback原本はローカルの`application-rollback.bin`（3,466,448 bytes、SHA-256 `e11da1b81baf0ff60086d9b61d89a4442bddae58e5eec42bdf4698186caa2bc1`）。必要時は本体・table・raw otadata/slotを再確認し、現在slotのApplicationだけへ原本を書込み、読戻し・非対象領域一致を確認する。FAT/NVS全体の復元で通常の設定更新や単調時刻予約を巻き戻さない。

今回の作業はAcceptanceと報告書のみで、Firmwareソース・startup・SDKは未変更。[機密値を除いた集計証跡](evidence/papermono-production-acceptance-20261026/summary.json)を併記する。報告書を既存feature branchへCommit/Pushし、Integration/mainへMergeしない。SketchとSDの修正は別Commit・別成果物で、改めてFAIL項目を確認する必要がある。
