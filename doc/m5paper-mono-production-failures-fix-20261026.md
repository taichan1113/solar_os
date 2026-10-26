# M5Paper MONO 本採用Acceptance FAIL修正・再Acceptance準備（2026-10-26）

今回の範囲はソース修正・Host検証・本採用Firmware Build・修正BranchへのPush。**実機・COMポートへアクセスせず、Flash、startup/NVS/SD変更、format、Mergeは行っていない。** 実機Firmwareは今回変更せず、新成果物は未投入。前回のAcceptance対象Firmwareが最終確認状態であり、今回は本体状態を再照合していない。

## A. 原因と修正

| ID | 確認できた原因・限界 | 修正 | 判定 |
|---|---|---|---|
| FIX-01 Sketchタッチ停止 | SketchがTOUCH_CANCEL後のbalanced suspend/resumeを実際の休止・復帰として扱い、leaseを解放・再取得する。再取得がExpansion UPDATINGで拒否されると再取得機会がなくなる。実機ログの`ESP_ERR_INVALID_STATE`、lease=0と整合。Hostで修正前FAILを再現。ただし実機の全イベント順序を採取したわけではない | CANCELでは線の入力状態だけ破棄してleaseを維持。実休止・復帰は従来どおり解放・再取得。INVALID_STATEのみ既存TICKで最大2回追加取得、合計3試行で終了 | **Host修正PASS／実機待ち** |
| FIX-02 SD保存失敗 | 実SDK FatFS・DiskIOで256MiB FAT32／約59GiBカードの対応を再現して保存・読戻しに成功。したがってpartition mapping不整合は確認できない。一方、コピーの同期確認不足・先行errnoの上書き、Writer後処理でerrnoが失われる問題をHostで確認。これが前回のSD実機FAILの根本原因かは未確定 | コピーで同期・closeを検査し最初のエラーを保持。Writerにopen/write/flush/fsync/close/rename/verifyの失敗段階を記録。native SDMMCにも既存診断DiskIOを適用しSDMMCエラー、DRESULT、LBAを取得 | **エラー処理修正・診断準備PASS／保存FAILの原因判定は保留** |
| FIX-03 SD再挿入失敗 | Unmount→lsblkで未mountのcard_readyが残る。検出できなかった挿抜後に古いHost/Cardを再利用する。Hostのcard世代変更で修正前FAILを再現。またpartition先頭読取りエラーを成功・空一覧として返していた | 未mountで明示rescan/mountする際に古いHost/Card/DiskIOを破棄して再初期化。読取りエラーを呼出元へ返す。mount中は所有関係を維持してstatus確認のみ。Sleep中は初期化を拒否 | **Host修正PASS／実機待ち** |

### FIX-01の経路と範囲

- `src/services/solar_os_sessions_touch_adapter.inc:35`：CANCEL配送後、legacy Appにbalanced suspend/resumeを呼ぶ既存経路。変更なし。
- `src/apps/solar_os_sketch.c:144`：取得失敗の有限再試行。成功またはINVALID_STATE以外の失敗で予算を終了。
- `src/apps/solar_os_sketch.c:1520`：CANCELと実suspend/resumeを区別。CANCELでタッチ電源を往復させない。
- `src/apps/solar_os_sketch.c:1568`：CANCELで描画中のstrokeを終了。shapeプレビューは再描画で消し、離指として確定しない。
- `src/apps/solar_os_sketch.c:1590`：既存TICKを使用。Task・待機時間・無制限retryは追加しない。ExpansionのINVALID_STATEガードは変更しない。

新規テストは実Sketchコードを使用し、Save通知→CANCEL→OK／Esc→描画、PNG再読込、3回再入、Browser/child境界、Sleep境界、UPDATING相当の取得失敗と回復、取得・解放回数を検証する。Session adapter自体は既存の実adapterテストでも確認した。FT6336の実電源・実機スケジュールは未確認。

### FIX-02の追跡結果と診断の読み方

Shell `cp`→Storage copy→VFS stdio→FatFS→DiskIO→SDMMCを確認した。Writerはパス解決後に同じVFSへstaged保存・同期・rename・読戻し検証を行う。既存Flash `/`、SD `/sdcard`、startupの参照先は変更しない。

Hostイメージは121,241,600 sectors、FAT32 partitionはLBA 8,192から524,288 sectors。物理drive 0/1を別用途として使用し、SDは**physical drive 2／logical volume 1／partition 1**を強制した。実SDK `ff.c`・`ffunicode.c`・`diskio.c`・`diskio_sdmmc.c`でlong filenameの作成→write→sync→close→read一致→Unmount/Remount一致を確認。通常writeがpartition外へ出ないことも検査する。媒体は削除されるHostの疎な一時ファイルだけで、SDカードやデバイスへはアクセスしない。Host用mkfsはこの一時イメージ専用で、Firmwareのformat経路を実行していない。

**未変更の保存経路でもこのHost保存試験はPASSした。** VFS登録・SDMMC transportはHost代替で、ESP-IDF VFSの実fopenとカードの物理writeまで検証したものではない。SDK FatFSソースは実物だが、Host設定では再入ロック・DMA・割込みを再現しない。前回のカードが書込み可能か、書込み時のカード応答・電源・通信が正常かも未確認。カード不良やformat不足とも断定しない。

診断追加箇所：

- `src/services/solar_os_storage.c:1321`：copy失敗時に`phase`と`errno`。成功前に`solar_os_storage_sync_file()`を呼び、close/cleanupで先行エラーを上書きしない。
- `src/apps/solar_os_writer_files.c:13`：`save phase=... errno=... result=...`。データ本文・保存パスはログに出さない。保存・バックアップ方式、checkpoint仕様は維持。
- `src/drivers/sd_card.c:116`：native SDMMCとruntime SDSPIの診断。既存SDKと同じSDMMC read/write APIと引数・戻り値を使い、失敗時だけ記録する。Board固有SDSPIのSDK経路は維持。
- `src/shell/solar_os_shell_hardware.c:213`：既存`disk status`に診断を追加。コマンド自体は媒体I/Oを発行しない。pdrv、logical、partition、start LBA、直近I/Oエラー・DRESULT・LBA/countを確認できる。

`mount-FRESULT`は直近の**f_mountの結果だけ**で、個別のf_write/f_syncのFRESULTではない。VFS stdioで返るerrnoとSDMMCの実エラーを混同しない。DRESULT=-1はstatus用のDSTATUS経路でDRESULT非該当。直近I/Oエラーは成功I/Oでは消去せず保持するので、失敗操作の前後のログ・statusを比較する。リセット／設定変更で診断は初期化される。失敗したdriveが解放済みならpdrvは255（未割当）になり得る。ファイルごとの生FRESULTをVFSの外から取得する機構は今回追加していない。

### FIX-03の所有関係

`src/drivers/sd_card.c:769`のscanで先頭読取り失敗を返し、`:1054`の未使用時解放、`:1084`のmount前更新、`:1327`のrescanで古い状態を破棄する。mount中のCardを勝手にdeinit／再bindしない。抜去・交換後の古い所有者へアクセス成功を返さず、明示Unmountを要求する。自動監視Task、format、無制限再初期化は追加していない。

## B. Git

- 基点：`5e66a56e147d78b9409374cceffbeb6dd111d21e`（報告）。実装`ea7b10e49a0a191947b3f227e855e9335a7e0616`との間にFirmwareソース変更なし。
- Branch：`fix/papermono-production-acceptance-failures`
- Worktree：`E:\ESP32\M5PapserMono-SolarOS\.research\production-failures-fix\worktree`
- Push先：`origin`＝`https://github.com/taichan1113/solar_os.git`、同名Branchのみ。Force Push・Integration/mainへのMergeなし。

| Commit | 内容 | Commit時点の検証 |
|---|---|---|
| `454a30cd9fce875a121575b13094fd92134b10d5` | Sketch CANCEL/lease修正＋回帰 | 新規CANCEL、projection、adapter、scale140、Expansion PASS |
| `8c7004d49a139187347e7f3971626d1640f7653c` | SD未mountの明示rescanで再初期化＋実FatFS fixture | FAT32 write/remount・stale Card・不在・scan失敗・mounted保護、SDMMC expansion PASS |
| `587680765b851dde2db35b0adc40958e092f6e4f` | SD保存エラー保全・診断＋回帰 | 本採用Host 30本＋Async追加3ケース PASS。SD実機原因は未確定 |

元のroot Worktreeは`integration/papermono-dirty-sketch`／`aa08bbfdfa65c72ada0a0bc7737d36b47a521186`、元の本採用Worktreeは`feature/papermono-production-integration`／`5e66a56e...`を維持。rootの既存未追跡`.vscode/`と文書4件を触らず、本採用Worktreeはclean。修正Worktreeのビルド出力は専用`.pio`で衝突しない。

## C. テスト・Build

最終コード`58768076`で以下を確認した。

| 検証 | 結果 |
|---|---|
| `make -C tests/host test-production` | 30 binaries＋Async追加3ケース PASS |
| 新規`sketch_cancel_lifecycle_test` | CANCEL/Save/OK/Esc/再入/取得失敗と有限回復 PASS（修正前FAIL） |
| 新規`sd_card_fatfs_test` | 実SDK FatFSのmapping・write/close/read/remount・write失敗伝播・stale Card/不在/scan失敗・mount所有権 PASS（stale状態は修正前FAIL） |
| `storage_mounts_test`追加ケース | sync失敗、write失敗＋close失敗、close失敗のerrno・cleanup PASS（修正前FAIL） |
| `writer_files_test`追加ケース | sync失敗後のcleanupでerrnoを保持し元文書を維持 PASS（修正前FAIL） |
| Sketch関連追加6 binaries | lifecycle、baseline、legacy、touch projection、Expansion registry、FT6336 lifecycle PASS |
| Python | Board manifests 33、Manual生成13、Wi-Fi TUI 6、計52 PASS |
| ESP32-S3本採用Build | SUCCESS、236.58秒、ESP-IDF 5.5.5、xtensa 14.2.0+20260121 |

Hostは既存WSL GCC、FatFS/cJSONはインストール済みESP-IDF 5.5.5を参照。Firmwareは既存`scripts/build_papermono_production.ps1`を使用しSDK/PlatformIOを更新しない。

共通Storage/SD経路への変更がある。旧1bit native GPIO電源・PaperMono 4bit IOE電源のSDMMC設定、legacy Sketchの同期描画・pointer lease非使用、Flash保存先をHostで確認。PaperMono以外のFirmware全ビルドや各Boardの実機試験は行っていない。

Candidate F、fastest/tile/SPI20、rows-dual/32回同期、Display Async/Pacing OFF、Writer checkpoint、Launcher配置、Manual/TypeMouse/BLE、Shell Esc、Wi-Fi/NTP方針は変更していない。LUT・電圧・EPD更新シーケンスのdiffはない。

## D. Firmware成果物

| 項目 | 値 |
|---|---|
| 実装Commit | `587680765b851dde2db35b0adc40958e092f6e4f` |
| Board／Flavor／Layout | `m5paper_mono`／`writerdeck`／`ota`、SolarOS 4.16.1 |
| Binary | `E:\ESP32\M5PapserMono-SolarOS\.research\production-failures-fix\artifacts\production-fixes-20261026\firmware.bin` |
| Size | 3,518,880 bytes（旧本採用版より2,064 bytes増） |
| SHA-256 | `c0aa65057f9d07cd6f024e4792cf7aa2f17538f98155091b3d2e3f9118f9bed3` |
| Partition table SHA-256 | `fd976d7c1eaa8f9d0a01ac87a8d05fc1cb1e59d3c5970954b30ad0c3aa12a488`、旧本採用成果物と一致 |
| Build flags | WRITER_D=1、SPI20_TRIAL=1、DIRTY_HOLD_TRIAL=1、HYBRID_TRIAL=0、WAVEFORM_TRIAL=0、SKETCH_LATENCY_TRIAL=0 |

[集計・成果物manifest](m5paper-mono-production-failures-fix-artifact-20261026.json)も併記する。ELFとpartition binaryは同じローカル成果物フォルダに保存。生バックアップ・実機データ・BinaryはCommit/Push対象に含めない。

Application-only用。Partition CSVは無変更で、生成partition tableも旧成果物とSHA一致を確認済み。Bootloader・table・filesystem imageを実機へ書かない。今回のコードは実機未投入なので、Host/Build PASSを実機PASSとして扱わない。

esptool 5.4.0のオフライン`image-info`でもESP32-S3／16MB／SolarOS 4.16.1／IDF 5.5.5、image checksum・内蔵validation hashがvalidであることを確認。Application容量は`0x700000`のOTA slot内に収まる。ポートを指定・列挙せず、ファイルだけを検査した。

旧Rollback原本は以下を保持した。

```text
E:\ESP32\M5PapserMono-SolarOS\.research\production-acceptance-20261026\run-20261026T085105\application-rollback.bin
3,466,448 bytes
SHA-256 e11da1b81baf0ff60086d9b61d89a4442bddae58e5eec42bdf4698186caa2bc1
```

## E. 次回の実機再Acceptance

Flashは別依頼で実施する。その時点の未保存編集・競合・チップ・16MiB容量・起動slot/table/raw otadataを再確認し、**現行Applicationを新規バックアップ**してから今回BinaryのSHAと投入長を照合する。Applicationだけへ書込み、読戻しと非対象領域の一致を確認する。以前のslotをそのまま仮定しない。

1. **Sketch**：専用PNGを開く→Save→OKをタッチ→短線描画。再Save→Escで通知を閉じる→追描き。終了→再入→再読込を少数回、Browser復帰とSleep/KEY1 Wakeも確認。各境界の`touch0` ready/requested/lease数、Session、pointer enableエラーを記録する。線の欠け・最終線分・PNG内容も確認。他Appへ戻ってタッチが使え、余計な電源往復がないことを確認。
2. **SD保存**：既存256MiB FAT32 `sd0p1`だけを`/sdcard`へmount。Flash `/`とstartup effective=flashを確認。存在しない専用の短名・長名を選び、既存Flashの専用小テキストを`cp`→readback一致／checksum→Unmount/Remount後一致。Writerでも別の未使用名へ保存・再読込。各失敗の直後に`disk status`とUSBログのphase/errno/DRESULT/SDMMCコード/LBAを取得し、無出力だけで成功としない。失敗時は追加の書込みを止め、記録から次の修正を判断する。mount-FRESULTだけでfile write成功と判断しない。
3. **SD再挿入**：Unmount→lsblk→物理抜去→再挿入→lsblk→mount→専用ファイルreadbackを確認。lsblkを挟まない通常挿抜、カードなし起動、mount中Sleep/Wake後の読取りも確認。mount中の抜去を自動復旧・成功扱いしない。
4. **最低限の回帰**：F既定と更新error=0、Launcher、Manualタッチ/Reader復帰、TypeMouse起動/Exit、USB再接続、Flash保存・再読込、Wi-Fi/BLE再接続を確認。

SD自動format、既存ファイル上書き、未対応partitionの試験書込み、既存データ移動は禁止。失敗を繰り返して正常化しようとしない。今回のFIX-02を含め**実機3件のPASSを確認してから統合判断**する。

必要なRollbackは次回採取する直前Applicationを第一候補とし、旧原本を使う場合も正当性とslot/容量を再確認する。Applicationだけ復元し、読戻し・非対象領域不変を照合。FAT/NVS全体を巻き戻して通常の設定更新や単調時刻予約を壊さない。
