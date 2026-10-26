# M5Paper MONO FAIL修正版 Application-only Flash・再Acceptance（2026-10-26）

**結果：FIX-01 PASS、FIX-02 FAIL、FIX-03 PASS。本採用Acceptance全体は未完了。** 指定された修正版をApplication-onlyで投入し、最後も同じFirmwareを残した。SD保存FAIL後の追加書込み試験・場当たり的な修正・再Flash・format・Mergeは行っていない。

## 対象と投入結果

| 項目 | 結果 |
|---|---|
| Branch／実装Commit | `fix/papermono-production-acceptance-failures`／`587680765b851dde2db35b0adc40958e092f6e4f` |
| 開始HEAD | `2a50d25926d7a5a3c4629ce71629bfe5d12175eb` |
| Worktree | `E:\ESP32\M5PapserMono-SolarOS\.research\production-failures-fix\worktree` |
| 指定Binary | `E:\ESP32\M5PapserMono-SolarOS\.research\production-failures-fix\artifacts\production-fixes-20261026\firmware.bin` |
| Size／SHA-256 | 3,518,880 bytes／`c0aa65057f9d07cd6f024e4792cf7aa2f17538f98155091b3d2e3f9118f9bed3` |
| 実機 | COM39、既知のUSB個体・ESP32-S3・16MiBを照合。M5Paper MONOのIOE／SSD1677／FT6336構成を確認 |
| OTA | 実行中Running／Bootとtable、CRC付きraw otadataが一致：`ota_0`、offset `0x10000`、容量`0x700000` |
| raw otadata | sequence=1、state=valid、CRC正常。他のrecordは未使用。前回slotを無条件に仮定していない |
| Application-only | **PASS**。対象Application内の消去・書込みのみ。Bootloader・table・otadata・NVS・PHY・FATへの書込み指定なし |
| 書込み直後の読戻し | **PASS**。投入長SHA一致、16MiB全域を読戻し、Application以外の全バイト一致 |
| 保護領域 | Bootloader／partition table／NVS／otadata／PHY／非対象OTA slot／内蔵FATの7領域一致 |
| Build | 今回は再Buildせず、指定された既存BinaryをSHA・形式・checksum・内蔵digest・容量で検証して使用 |

Host・Buildは前回の[修正報告](m5paper-mono-production-failures-fix-20261026.md)の事前検証結果であり、今回再実行したものではない。[機密値を除いた集計証跡](evidence/papermono-production-fixes-reacceptance-20261026/summary.json)を併記する。

## 新規バックアップと保護

今回のローカル専用保存先：

```text
E:\ESP32\M5PapserMono-SolarOS\.research\production-fixes-reacceptance-20261026\run-20261026T221522\
```

Flash前の16MiB全域、Application partition全体、実際のApplication、Bootloader、table、raw otadata、NVS、PHY、非対象OTA slot、FATを新規保存・照合した。既存のバックアップを上書きしていない。生Binary、抽出文書、資格情報、ファイル一覧はCommit/Push対象外。

| ファイル | Size／SHA-256 |
|---|---|
| `full-flash-before.bin` | 16,777,216 bytes／`e82925c35366b2344944f8fb569b24f18f9a59d407fc5dca4eba90dfdd2326af` |
| 今回の`application-rollback.bin` | 3,516,816 bytes／`86b735ef56e2a5a6f4400bb5d02985ee5c127b10f85eb165e3d1337d1051973b` |

Rollback第一候補は**今回取得した直前Application**。以前の`e11da1b8...`の原本は保持しているが、今回の第一候補と混同しない。Rollbackは実施していない。SD保存FAILだけを理由に自動Rollbackしない指示に従った。

## FIX-01：SketchのSave後タッチ — PASS

SD未挿入でユーザーが以下を確認した。

1. LauncherからSketchを開き、既存の専用試験PNG `/Sketches/sketch003.png` をOpen。
2. 短線・曲線→Save→通知のOKをタップ→追描きが正常。
3. 再Save→Escで通知を閉じる→再描画が正常。
4. B長押しで終了→再入→同じPNGの再読込→描画が正常。
5. LauncherのSleep→Sleep中タッチ無反応→KEY1 Wake→Sketch再入・同じPNGを開いて描画が正常。
6. 終了後、Manual・TypeMouseなど他Appのタッチも正常。

USB側では、操作中／Save・再入後ともSketch active、`touch0 ready=yes`、`requested enabled=yes`。Sketch表示時のenable leases=2、Launcherへ戻った後=1で、取得失敗による0への消失や、再入による累積増加は観測していない。`manual enabled=no`はlease管理下の正常状態であり、無効化されたことを意味しない。`pointer enable failed`は採取ログにない。Async busy/pending/error=0、ユーザーも線の末端・操作・表示を正常確認。

最終FAT読取りでは専用PNGの変更を確認し、72,617 bytes、全4 PNG chunkのCRC正常。ユーザーが再読込と追描きを確認した。専用PNG以外の既存画像はhash一致。

CANCEL各瞬間を専用counterで数えたり、物理電源レールを計測したりしたものではない。今回の具体的なSave/OK/Esc/再入/Sleep操作に対するPASSで、長時間耐久試験ではない。

## FIX-02：SD保存 — FAIL、追加書込み停止

### 条件・最初の試験

同じ既存SDHCカードを使用。20MHz・4bit、容量59,200MiB。`sd0p1`の256MiB FAT32だけを`/sdcard`へmountした。別57.6GiBの未対応partitionはmount・書込みしない。既存のpartition列挙に伴うmetadata検出以外に、その領域を調査していない。

Flash `/`、SD `/sdcard`、startup effective=flashを確認。未使用を確認した短名 `/sdcard/RF262215.TXT` へ、Flash上の専用文書 `/acceptance-20261026.md`（13 bytes）を**1回だけ**`cp`した。

sourceの事前SHA-256は`5b4ef8aa9113085f75c67dd9de98feeee1b8b4b07fcab1d6e14bda0df47536df`で、前後のFlashバックアップと一致する。

### Unmount／Rescan前の診断

| 観測 | 結果 |
|---|---|
| cpの表示 | エラー文字列なし、無出力でpromptへ戻る |
| Shell `status` | Last exit=0。ただし後述の理由でcp成功の証拠にしない |
| 目的ファイル | `ls`でENOENT。本体Storage APIの`exists()`もFalse |
| copy phase／errno | 失敗ログが出ておらず、**今回のcopy実行のphase・errnoは未取得**。errno=0と推定しない |
| SDMMC／DiskIO | 記録された直近I/Oエラーなし：operation=none、error=0、DRESULT=0 |
| LBA／count | 0／0。ただしエラー未記録を示す初期値で、実際にLBA 0へアクセスしたことを示さない |
| FatFS | mount-FRESULT=0。個別file write/sync/closeのFRESULTは未取得 |
| mapping | physical drive 1、logical volume 1、partition 1、start LBA 8192、524,288 sectors |
| パス解決 | 本体APIでもsource=`/acceptance-20261026.md`、dest=`/sdcard/RF262215.TXT` |
| source metadata | regular file、13 bytes。最終Flash hashも不変 |
| Flash側への誤保存確認 | `/RF262215.TXT`なし。最終FATの全ファイル一覧にも`RF262215`を含むファイルはない |

`disk status`のinit/mount=0x105は、成功後にクリアしない診断の保持値である。実際にはcard-readyログとmount成功があり、この値を今回のcp失敗原因として扱わない。DRESULT=0も「記録されたI/Oエラーなし」であって、file write成功・永続化成功の保証ではない。成功I/Oの回数を数える診断は今回Binaryにない。

`src/shell/solar_os_shell_fs.c:913`のcp handlerはvoidで、Storageからの結果をShellのLast exitへ設定しない。したがってLast exit=0とcopyの実際の戻り値を同一視できない。Storageの実戻り値は今回のログでは確定できない。`src/services/solar_os_storage.c:1317`以降のphaseログは失敗時のみで、start/completion・書込bytesのログはない。

採取したprivate証跡は`sd-first-copy.json`、`sd-failure-readonly-paths.json`、`sd-failure-readonly-resolve.json`など。直後に`disk status`・ring logを取得し、その後もパス・存在・元ファイルを読むだけとした。途中の読取り診断1件にホスト側の引用符の誤りがあり、SyntaxError/unknown commandを記録したが、修正した読取りクエリは成功した。このエラーはcp試験後の別操作であり、SD保存FAILの証拠と混同しない。

### 判断と次の作業

保存が成立しなかったため、**長名コピー・WriterのSD保存・その再読込はBLOCKED**。追加のSD書込み試験は0回。format・既存ファイル上書き・Firmware変更・再Flashによる試行は行っていない。

少なくとも高位のパス解決、指定partitionのmount、既存ファイルのreadは成立している。Flashへの単純な誤保存も今回の範囲では見つからない。一方、copy実行経路の到達・実戻り値、VFS/stdioの各段階、個別FatFS FRESULT、成功扱いのSDMMC writeとその永続化は区別できていない。**根本原因は未確定**で、カード故障・format不足・partition mapping不良・物理write正常のいずれも断定しない。

別作業で、専用試験パスに限定したcopy start/completion・実際のsource/dest・byte数・fstat/close/sync結果、VFS/FatFSの生結果、DiskIO write回数とreadbackの対応を最小限記録する案を推奨する。今回はそのコード追加や追加SD書込みを実施していない。

## FIX-03：Unmount→lsblk→物理挿抜 — PASS

FIX-02の診断保存後、追加書込みなしで次を実施した。

1. mount中の一覧と、既存ファイルの事前hashを取得。
2. `disk umount sd0p1`→`disk lsblk`。SDを再初期化した状態で未mountを確認。
3. ユーザーが同じSDを抜去し、数秒後に再挿入。
4. `disk lsblk`でcard-readyログ、`sd0p1`のFAT32再認識を確認。
5. `disk mount sd0p1 /sdcard`が成功。
6. 既存ファイル`issue.txt`を読み、145 bytes・SHA-256 `98340e20fac5920dd523f8e5a566cbe6f59a06603ab905fcd8114d4c038d6b99`が挿抜前後で一致。
7. Flash `/`とstartup effective=flashが維持され、最後は安全にUnmountしてカードを取り外した。

前回のtimeout 0x107とpartition消失は再発していない。試験対象は既存の読取り可能なファイルで、コピーFAIL後に新規SD文書を作っていない。mount中の強制抜去はしていない。今回の特定手順を1回確認した結果で、任意の挿抜・すべてのカードへの保証ではない。

## 最小回帰・最終データ照合

| 項目 | 結果 |
|---|---|
| Launcher | 3列×2行、文字・白背景・アイコン・タッチ正常をユーザー確認 |
| Manual | スワイプ、ページ表示、Readerから一覧への復帰が正常 |
| TypeMouse | A入力、Shift固定→近接Z→解除、Exit正常 |
| Writer / Flash | 未使用の`/RA262215.md`へ`FIX OK`を保存・再読込。最終FATでも6 bytes・内容一致 |
| Candidate F | mode=F、scope=all-apps。最終読取り前はphysical=110／success=110／error=0 |
| Refresh／転送 | fastest、SPI実効20MHzを確認。tile/rows-dualは指定固定BinaryとF正常動作の条件を維持。個々の値の独立status表示までは再取得していない |
| Display Async | enabled=1、最終読取り前completed=61、physical=60、busy/pending/error=0 |
| Wi-Fi／BLE | 既存接続先へ再接続。保存設定・bond値は後述の照合でも一致 |
| Sleep/Wake | SD未挿入でSleep中タッチ無反応、KEY1 Wake後Sketch・他Appタッチ正常。SD mount中Sleepの再試験は今回していない |
| USB Shell | CDC 1 Session。USB抜き差し後も応答、Launcher・タッチ正常 |

最後にFlashを読み取りだけで照合し、通常再起動を1回行った。Application SHA・partition table・otadata・PHYが一致。startupは153 bytes、SHA-256 `2ee2925a9776fd3cb0996ea9ad413bbb8856c38fb02c89047bd07f0b27ee28bd`で不変。startupの行整理や既定値への移行を実施していない。

既存63ファイルに削除なし、60件はhash一致。変更3件はユーザーが編集・保存した専用`sketch003.png`とShell履歴、Reader閲覧位置。追加2件は専用Flash文書とその正常な保存済みcheckpointで、既存Writer文書・他のPNGなどに差分なし。SDのコピー先がFlash内の別パスに生成された形跡もない。専用PNGの全chunk CRCと新規checkpointのchecksumも検証した。

最終読取り時のNVS物理領域には通常Boot後の差分があるが、57論理値を比較した差分は単調時刻予約とSDK PHY校正データのみ。BLE bond 19値・記憶済みpeer・Wi-Fiを含む他の値は一致。**これらは最後の通常再起動前に取得したsnapshotの照合**で、再起動後の通常更新を巻き戻していない。最後の再起動後にもLauncher・USB・F既定・Async error=0・Wi-Fi/BLE復旧を確認した。BLEは一時「記憶済みキーボード待機」で、ユーザーが既存PaperMono profileのShiftを1回押して起こすと再接続した。再ペアリング・設定変更はない。ユーザーの最後の表示確認は「はい、正常です」。

## 最終状態と次の判断

- 実機：今回の指定修正版、`ota_0`。SDは取り外し済み。Launcher表示正常。
- Rollback：未実施。今回取得した直前Applicationは検証済みで保存している。
- 全体判定：**未完了（FIX-02 FAIL継続）**。3件すべてPASSとは報告しない。
- 次の作業：SD保存の観測不足を補う最小診断を別依頼で設計・実施する。長名保存とWriter SD保存は、その最初のコピーが成立した後に再開する。
- 今回のGit変更は本書と集計証跡のみ。同じ修正Branchへ通常Commit/Pushし、Integration/mainにはMergeしない。元Worktreeと既存未Commit文書、旧Backup・成果物を保持した。

必要時は本体・slot・tableを再確認し、今回の`application-rollback.bin`をApplicationだけへ戻す。読戻しと非対象領域の一致を確認し、NVS/FAT/otadataを復元して通常更新を巻き戻さない。
