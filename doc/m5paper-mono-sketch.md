# M5Paper MONO Sketch統合

## A. 実装結果

- 作業ブランチ：`feature/papermono-sketch`。
- ベースcommit：`aa78871b`（Platform Services最終受け入れをコミット）。
- Sketch実装commit：`2b702918fde53fcae283c5a09604bd867070eb78`。
  本報告のcommitはこの実装commitに続く同ブランチのHEAD。
  `git log -1 --oneline -- doc/m5paper-mono-sketch.md`で識別できる。
- 採用baseline artifact：`papermono-platform-safe-telemetry/firmware.bin`、
  SHA256 `3bfc6e112e80445ef34d1958217049a222e8d387180d4c3e43f0db0ea8ac33db`。

既存SketchのCanvas、Pen/Line/Rectangle/Ellipse/Fill/Eraser/Clear、
PNG保存/Open/Import、storage browser、registry、cold-state lifecycleを再利用する。
Canvasは既に4色のindexed PNGを保存しており、色保持のためのencoder変更は不要。
ヘッダーの古いgrayscaleコメントと実装を区別した。

`flavors/writerdeck.toml`の`sketch = true`で既存`groups.sketch/app_sketch`を選択する。
依存は既存の`service_image/service_storage_browser`を維持し、他flavorの選択を変更しない。

### Touch lease

Expansionに最小の所有者別APIを追加する。

```c
solar_os_expansion_enable_acquire(name, owner);
solar_os_expansion_enable_release(name, owner);
```

`owner`は呼び出し元の生存期間に対応する非NULLの識別子で、dereferenceしない。
Sketchではcold stateのアドレスを使い、必ずその解放前にleaseを返す。
同一ownerの取得・解放は冪等で、別ownerのleaseを消さない。
各デバイスに最大8 ownerを固定長で保持し、lease操作自体の動的確保はない。

`manual_enabled OR lease_count > 0`をdriverのrequested stateへ渡す。
物理readyはdriverの`is_ready`から別途取得する。FT6336の既存`requested` getterだけを
追加し、電源/reset/worker/I2C処理は再利用する。getterのない従来driverはleaseにopt-in
せず、従来のmanual enable処理を維持する。

| Manual | lease | 論理要求 |
|---|---|---|
| OFF | なし | OFF |
| OFF | あり | ON |
| ON | なし | ON |
| ON | あり | ON |

取得失敗はownerを登録せず、以前の要求へrollbackし、失敗を返す。
lease保持中のdetachを拒否する。registryの既存reservationでattach、detach、manual
操作、suspendとの競合を保護する。解放は停止失敗時もownerを消して失敗を報告する。
進行中のenable/suspend callbackと重なる解放はownerをその場で消し、callback終了時に
最新の要求を反映する。ownerを解放済みstateへのポインターとして残さない。
この反映が失敗しても自動でhardware retry/resetせず、一度エラーを記録する。
CPU panic/完全電源断時のアプリcleanupを保証する設計ではない。

`expansion devices`にmanual enabled、enable leases、requested enabledを追加し、
ready（物理状態）との違いをUSB Shellから確認できる。

### Sketch lifecycle

- start：Canvas/bitmap/previewの確保後にlease取得、ready確認後にpointer案内を判定。
- enable失敗：別のready pointerがあれば継続し、なければ既存案内を表示。
- stop/`state_release_cleanup`：lease解放後に既存資源を解放。重複cleanupも安全。
- session suspend：進行中のgestureを終了しlease解放、Canvasとdirty状態は保持。
- session resume：再取得してから再描画。既存Canvas/未保存データを維持。
- device suspend：既存Expansion/FT6336がphysical OFFとrequested復元を担当。
  session切替のないdevice suspendではleaseは保持される。

PaperMono manifestの`SOLAR_OS_BOARD_SKETCH_POINTER_DEVICE="touch0"`で対象を選択する。
Sketchにboard名・ピン・IOE/PM1操作・二重座標変換を追加しない。
他ボードではこのdefineがなければ自動取得しない。

### 表示

既存Sketchの`solar_os_gfx_present`は同期送信のため、PaperMonoのみmanifestで
`SOLAR_OS_BOARD_SKETCH_ASYNC_PRESENT=1`を設定し既存`solar_os_gfx_submit`を使う。
immutable snapshot、latest-frame queue、completed shadowは変更しない。
MOVEは毎回Canvasへ反映し、既存`render_pending`と40ms tickで送信をまとめる。
RELEASEも最終状態をpendingにする。送信失敗をログへ記録し、無条件retryしない。
他ボードは従来の同期presentを維持する。
タイトル文字列の切り詰め幅を明示してhostのformat-truncation警告を解消した。

800×480ではCanvasは736×392。2bpp Canvas 72,128 bytes、preview 72,128 bytes、
mono bitmap 36,064 bytes、合計180,320 bytes＋cold state/browser等。
PNG decoderやFill workspaceは別の一時確保。hostで繰返しcleanup後の確保残数0を確認。
実機は6回の起動/終了で、起動中Internal 73.8KiB/PSRAM 7.0MiB、
終了後Internal 77.2KiB/PSRAM 7.3MiBの空き容量へ毎回戻った。
継続的な増加を認めないが、CLIの丸め精度と6回の範囲に限る。

保護対象のIP2315無アクセス、I2C排他/sleep pin/RX completion対策、Battery/RTC/LED、
BLE/NimBLE、Writer Altガード、SSD1677 LUT/SPI/tile/typing/cleanupは変更しない。
`.vscode`と先行Acceptanceコードを保持した。

変更ファイル：

- `boards/manifests/m5paper_mono.toml`、`flavors/writerdeck.toml`
- `src/apps/solar_os_sketch.c`
- `src/services/solar_os_expansion.c`、`solar_os_expansion.h`
- `src/services/solar_os_ft6336.c`、`solar_os_ft6336.h`、`solar_os_ft6336_driver.c`
- `src/shell/solar_os_shell_expansion.c`
- `tests/host/Makefile`、`expansion_registry_test.c`、`sketch_lifecycle_test.c`、
  `sketch_legacy_lifecycle_test.c`
- `tests/test_flavor_packages.py`
- 本報告 `doc/m5paper-mono-sketch.md`

## B. 自動テスト

- Host：既存129＋Sketch lifecycle/legacyの2 executable、合計131。
- Python：566 tests、4 skips、failure/errorなし。
- Package：writerdeckへのSketch/依存選択、他flavorへの非干渉。
- Expansion：manual/複数owner/冪等/上限/enable失敗rollback/解放時の停止失敗、
  suspend中取得/解放、callback競合中の解放、leased detach拒否。
- Sketch：3段階の確保失敗、不正引数、PNG open失敗、pointer unavailableと代替pointer、
  start/stop/重複cleanup、session state保持、MOVE統合/RELEASE最終送信、
  実PNG保存・STB再読込による色保持・Import、GIF Import、別ボードの従来動作。
- 既存FT6336/I2C/Battery/RX8130CE/BLE/Writer/display回帰を広域suiteに含む。

証跡：`.research/sketch-integration/host-final.log`、`python.log`。
Pythonログのunpatched NimBLE negative controlで表示される子processのassert失敗は
意図した対照試験で、suite本体は`OK (skipped=4)`。

## C. Firmware

PlatformIO env `esp32_s3_devkitc1_n16r8`、board `m5paper_mono`、flavor `writerdeck`、
ota layout、BLE security trace OFFでビルド成功（197.43秒）。
ビルド時の実装は未コミットだったが、その後production sourceは変更せず、
検証した同じ内容を上記実装commitへ保存した。
`build/papermono-sketch/firmware.bin`は3,346,736 bytesで、baselineから12,336 bytes増加。
SHA256：`599d3f5023200869c4aa8dd9768b63977b248ac1ae3456d262a85d26302d6471`。
`esptool write-flash 0x10000`でアプリだけ書込み、hash照合成功（17.8秒）。
範囲は`[0x10000, 0x341130)`、既存app partition 0x700000 bytes内。
NVS/FAT/partition tableは書き込んでいない。
COM39のVID/PID 303A:1001とMAC 28:84:85:44:79:68を再確認した。
書込み前NVS/FATを`sketch-before`としてread-only backupし、実機はShell/touch OFF。
診断instrumentationを追加していない。

検証後のread-only backup `sketch-after`を、書込み前の論理内容と比較した。
実機はテスト文書保存済み・Sketch/Writer/editor終了・Shellのみの状態で取得し、
esptool read-flashの再起動をI2C障害からの救済として使っていない。

- FAT：既存40ファイルのうち、実行コマンドのShell履歴だけ更新。それ以外はbyte一致。
- 新規3ファイル：`/Sketches/sketch001.png`、`/sketch-regression-20261010.md`、
  その `/.writer/7795dcb8.recovery`。既存文書・startup等を上書きしていない。
- PNG：72,617 bytes、736×392、2-bit indexed、4色palette、全chunk CRCとIDAT展開を検証。
- 新Writer本文：9 bytesの`SKETCH OK`。saved checkpointのpath/cursor/zoom/checksumも検証。
- NVS：38 logical keys、変更はWireGuardのmonotonic boot reservation 2回分だけ。
  5 bond-store keysとremembered peer、全ユーザー設定は一致。予約値を巻き戻していない。
- FAT raw SHA256：`44b73b8a6612cb292aee29686f15b96bcc945ba57f5ddec440e2744ab45fb7db`。
- NVS raw SHA256：`9778aa3802f6e82210010c532fa8f0367e05741511b656812d37ddc90b06c387`。

証跡：`.research/sketch-integration/data-audit.log`。

## D. 実機Acceptance

起動後USBから`apps`登録とShell/touch OFFを確認。
`session send 0 sketch`で既存Shell経由の起動が成功し、touchは
manual no / leases 1 / requested yes / ready yesとなった。
ログに既存PYG13→reset LOW/HIGH→FT6336 readyが記録され、実SPI 40MHzも確認。
Sketch最初の通常tick観測は平均13us/最大332us（描画前の短い観測なので
描画・表示完了の遅延を表す値ではない）。
起動時Battery 3.846V /60% estimated /external /Charging unknown、RTC flags
21 00 10、PM1 ID 50、IOE ID 5cを確認。
先行Acceptanceの人手確認を今回の実績として再計上しない。

人手でPenの横線/ジグザグ、Clear、Line、Rect、Oval、Fill、Erase、Canvas四隅の
小さい印、Saveの基本動作を確認。指と線の位置は良好で、Sidebar/ボタンは画面内。
新規 `/Sketches/sketch001.png` 72,617 bytesをUSBで確認し、既存文書へ保存していない。
`session close 1`後にmanual OFF / leases 0 / requested OFF / ready noとなり、
FT6336ログにもworker stopped / power OFFを確認。PNG引数で再起動後はlease 1/ready yes。
USB Shellから直接の`session fg`は既存仕様により拒否されるため、成功に数えない。
既存`input emit ALT+TAB`による切替と人手のAlt+Tabを併用。
Sketch→既存edit→Shellではlease 0/ready no/worker停止、Sketch復帰ではlease 1/ready yes。
人手で切替前の図形/線の保持と復帰後の指描画を確認。
USBの`session send`はsuspended/nonempty promptを拒否する既存仕様があり、
拒否ログを成功扱いにせず、実際のsession list/lease/powerログで遷移を照合した。

手動ON＋Sketch lease 1で既存`input emit q`により通常終了。
終了後manual ON/lease 0/requested ON/ready yesを確認し、手動OFFでready noへ戻した。
その後6回Sketch起動/`session close`を反復し、起動lease 1、終了lease 0、
worker停止/power OFF、終了後のRTC flags/PM1 ID/IOE ID/Batteryすべて正常。
実機にretry/resetによる救済は行っていない。
証跡：`lifecycle-probe.log`、`lifecycle-probe-summary.json`。

Sketch foreground、実タッチ入力・100Hz worker動作中の共有バス試験は
180.889秒/60ラウンドPASS。RTC BCDとUTC比較、STOP/VLF、PM1/IOE ID、Batteryを検査。
reset/retryでの救済なし。証跡は`sketch-touch-soak-summary.json`および各ラウンドログ。
これは今回のSketch固有試験であり、先行600秒試験を再計上していない。

| 項目 | 結果 |
|---|---|
| Sketch起動 | PASS：USB起動＋人手で画面確認、pointer案内なし |
| Touch自動ON | PASS：manual OFF・lease 1・ready、指で実描画 |
| Touch自動OFF | PASS：終了後lease 0・ready no・worker停止・power OFF |
| Manual ON保持 | PASS：通常終了後manual ON/lease 0/ready yes、最後にmanual OFFへ復元 |
| Session切替 | PASS：切替時の自動解放/復帰時再取得＋人手でCanvas保持/指描画 |
| Device suspend/resume | PASS：KEY1停止・復帰、Canvas/追加線保持、復帰後の指描画を人手確認 |
| Pen/Shape描画 | PASS：Pen/Clear/Line/Rect/Oval/Fill/Erase、基本動作を人手確認 |
| PNG保存・再読込 | PASS：新規PNG保存・Sketch終了/再起動後に内容一致・指描画を人手確認 |
| 800×480レイアウト | 位置/四隅/画面内配置PASS。ただし文字が小さく操作しづらいとの指摘 |
| E-paper表示 | 機能は表示、品質FAIL：描くほど薄まる・ラグ・小さい文字でUXが悪いとの人手評価 |
| 共有I2C安定性 | PASS：描画中180.889秒/60ラウンド＋6起動終了反復＋KEY1復帰後 |
| Battery / RTC / LED | PASS：Sketch中Battery/date/time/LED offとRTC/PM1/IOE応答 |
| Writer / BLE回帰 | PASS：新文書へZMK入力/保存/終了、ZMK OFF→20秒→ON後version実行を人手確認、保存9 bytes/再接続ログも確認 |
| ユーザーデータ保持 | PASS：既存40ファイルは履歴以外一致、NVS38 keysのユーザー設定/5 bonds/peer一致 |

機能統合の主要AcceptanceはPASS。E-paper品質/操作感は上記FAILとして残し、
「快適なSketchとして全項目受け入れ完了」とは判定しない。
PNG/GIF Import、複数consumer競合、低メモリ/起動失敗は自動テストで確認し、
実機へ故意のI2C障害やメモリ不足を起こす試験は行っていない。

最終起動はShell、manual touch OFF/lease 0/ready no、`refresh=fastest`。
`apps`のSketch登録、BLE接続、保存された2テストファイル、正常なRTC/PM1/IOE、
Battery 3.862V/62% estimated/external/Charging unknown、LED offを再確認した。
既存`/.shell/startup`を変更せず、診断用timingもOFFのまま。

## E. 次タスクへの引継ぎ

人手の初回Pen確認：線は表示され、指と線の位置は良好、pointer案内なし。
「ややラグあり」との感想を記録。追加の人手確認では「描くにつれて薄くなる」、
「画面が小さく文字が見えにくい」「ユーザー体験は悪い」と報告された。
統合の機能確認と使用感の受け入れを区別し、表示品質をPASSにしない。
Sketchの既存小さいUIと、E-paperの累積更新品質を次タスクの改善候補とする。

実機session統計は描画/図形/Fill/保存等を含む期間に最大405,061us、deadline超過189回。
これは純粋なPen描画時間や入力から光学的表示完了までの遅延ではなく、原因は未分離。
一時的に既存`timing=on`で採ったdriverログではOTP fast更新約0.916〜1.128秒、
BUSY約0.555〜0.576秒の例を観測。`timing=off`へ復元済み。
通常Writerで得たfastestの過去測定値をSketchの実績として流用しない。
非同期submitはsnapshotコピーまででBUSY完了を待たない既存実装だが、
Sketch全画面のCanvas変換/描画、Fill、PNG I/O等のCPU/IO時間の分離計測は未実施。
描画検証時のruntime queryは`refresh=partial`。保存された `/.shell/startup` は
`display mode display0 refresh=fastest`を保持しており、最終起動ではfastestへ復帰した。
表示遅延/品質の観測時と最終状態のmodeを区別する。
KEY1試験後はmanual OFF/lease 1/ready yes、追加pointerのRELEASE、RTC flags
21 00 10/PM1 ID50/IOE ID5c、Battery 3.856V/61%、LED offを再確認。
USB接続下の機能的な停止/復帰確認であり、45秒の実light sleepを実証したとは記載しない。
ログで測れるCPU処理時間/driver更新時間と、指から光学的表示までの感触を混同しない。
I2C failureが出たら最初の失敗を保存し、reboot/retryで成功扱いにしない。
IP2315充電telemetryは復活させない。表示品質改善は今回実装せず、
本報告をChatGPTへ戻してreviewした後の別タスクとする。

今後の改善候補は、既存UIの文字とタッチ対象の読みやすさ、描画中の濃さ保持、
Canvas変換・全画面描画・Fill/Saveの時間分離計測。SSD1677 LUT/波形/typing pacingは
今回変更しておらず、次のfastest表示品質改善タスクには着手していない。
Touchのboot defaultはOFFで、foreground consumer/manual要求のORに従う。
物理sleep中はready OFFになり得るため、leaseと電源状態を同一視しない。
DMMによる電流/消費電力の比較は今回NOT MEASURED。
