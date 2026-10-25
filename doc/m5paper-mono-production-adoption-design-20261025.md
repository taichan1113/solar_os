> 2026-10-25の設計履歴。今回の実装・仕様更新は[2026-10-26報告](m5paper-mono-production-integration-20261026.md)と[採用台帳](m5paper-mono-production-adoption-ledger.md)を参照。元Worktreeの原本は変更していない。

# M5Paper MONO 本採用追加仕様 — 調査・影響分析・実装設計

2026-10-25 JST。[本採用台帳](m5paper-mono-production-adoption-ledger.md)の追加要件を具体化する。以下のAPI名・設定名に「案」とあるものは未実装。今回の変更は本書と台帳のみで、実装・Merge・Build・Flash・USB/COMアクセスは行わない。

## 1. 調査範囲と採用の意味

| 参照 | 状態 |
|---|---|
| 現在のWorktree | `E:\ESP32\M5PapserMono-SolarOS`、`integration/papermono-dirty-sketch`、`aa08bbfdfa65c72ada0a0bc7737d36b47a521186` |
| 最新の既存Fコード | `.research/candidate-f-validation/worktree`、`5d32ac1810dcf8cc0b9434a95c6c4b1c7fe72c56`。実装は`70e1c0ffeaf4d4e9e616d47a28ee3e5aaf0386cc` |
| Remote | originの該当Branch HEADを読取り照合。fetch・checkout・merge・pushなし |
| 既存ユーザー作業 | rootの`.vscode/`と未追跡の調査文書2件を保持。39 WorktreeのHEAD/status/追跡差分を記録 |
| 前回台帳 | 専用台帳を発見できず、今回要件を補完台帳として新規記録。Platformの過去試験報告は別資料として保持 |

Fの初期定性評価は良好で、本依頼により**全アプリ共通の既定として採用する方針**へ更新された。一方、長期電気的安全性・寿命や今回追加仕様の実機確認を完了した意味ではない。現行IntegrationにはFがまだなく、Fコードを通常版へ取り込む作業も今回はしない。

### 表示既定化の最小案

- PaperMonoのBoard設定/SSD1677初期化時にfastest、tile、SPI20、rows-dual、F選択を揃える。共通Driverを使う他Boardの既定値は変えない。現在のDriverはrefresh=AUTO、tileはbuffer確保成功時、SPI20/rows-dualはBuild条件付きであり、全てが無条件の通常版既定ではない。
- Fは[既存配列・選択](https://github.com/taichan1113/solar_os/blob/70e1c0ffeaf4d4e9e616d47a28ee3e5aaf0386cc/src/drivers/epd_ssd1677_reference.inc#L60)を再利用。`writer_d_select`のD既定・Sketch例外をFのBoard既定に合わせる。AppからLUTを切り替えて既定化しない。
- 初回・Reset/Wake・失敗時のOTP/Shadow基準再構築、cold full-RAM準備、warm両面同期、BUSY待機、Baseline LUT復元は維持。**「F既定」はOTP full/cleanupまでFで置換する指定ではない。** 正常なcustom fastestでFを使用する。
- Fの更新後にA配列をレジスタへ復元しても、ソフトウェア上のF選択は維持する。次回もF。Driver再作成後の既定もFとする。
- 全アプリにSketchも含める。過去のD版でSketchをAにした経緯を、Fへの恒久例外にしない。Fで黒線に問題があれば診断結果として報告し、復旧用A選択は維持する。

根拠：F側`epd_ssd1677.c:894–973`、`epd_ssd1677_writer_d.inc:30–57,120–157`、`epd_ssd1677_reference.inc:202–412`。rootの同名ソースとは版が違うため、この段落の参照は上記F Commitで固定する。

## 2. startup既定設定化とNVS

### 現在の実行順序・保存設定

PaperMonoは起動時に画面Shellを開始し、そのstartupを実行してからLauncherへ切り替える。画面ShellにはLauncherのdisplay targetを供給する役目もあるため、startup簡素化を理由にShell Session自体を削らない。[main.c:2014](../src/main.c#L2014)

startupは起動内で1回。実行元はNVSの`shell/startup_src`、未保存ならAUTO。AUTOは「Board SD capabilityあり、かつSD mounted」ならSDの`.shell/startup`、それ以外はFlashを選ぶ。`SOLAR_OS_SHELL_STARTUP_DEFAULT`をFlashへ変えるだけでは、保存済みAUTO/SDを上書きできない。[shell.c:263](../src/apps/solar_os_shell.c#L263)、[startup policy](../src/apps/solar_os_shell_startup_policy.c#L43)、[実行:10198](../src/apps/solar_os_shell.c#L10198)

既存のローカル保存物として、`.research/hardware/storage/.shell/startup`にはUSB Shell作成行、2026-10-15の`.research/hybrid-refresh/hardware-20261015/startup.before-hybrid.txt`にはUSB Shell作成とfastest選択、`startup.hybrid.txt`にはHybrid選択等がある。**過去のコピーであり、現在の本体startupではない。** 前回のFlash監査だけから現在の各行を断定しない。

| 設定 | 現状 | 移す先・NVSとの関係 |
|---|---|---|
| F | 明示`lut 5`。RAM選択状態 | PaperMono Driver既定。調査したLUT選択にNVS読書きなし |
| fastest | Driver初期AUTO、startupで選択する運用 | PaperMono Board/Driver既定。初期OTP準備は保持。調査したcontroller setterにNVS保存なし |
| tile | band buffer確保後に有効 | Board既定を明示、確保失敗を診断。暗黙に同じ性能だとしない |
| SPI20 | SPI20 Trial flag条件 | 通常Board既定に移す。実効周波数ログは残す |
| rows-dual/Async | 現行Board/Buildに既存経路 | 通常Board構成で有効化し既存同期・Queueを再利用 |
| USB Shell | 画面ありではstartupから`session create shell cdc0 --term dumb --size 80x25`等 | CDC登録後に既存port Shell APIで1回作成。同じportが既に使用中なら重複作成しない |
| Wi-Fi ON | boot-policyの未保存値はtrue。ただしBootはinitのみ | Board起動ポリシーで既存startを呼ぶ。NVS `wifi/enabled=0`は明示OFFとして保持し表示する |
| NTP | 明示API/job | 自動開始を追加しない。startupの該当行は調査後に除去案。RTC復元と手動同期は保持 |
| startup source | NVS値>AUTO既定 | PaperMono追加SDをstartupの自動実行元にしないBoard方針が必要。保存済み値と実効元を明示する |

Display側にあるNVSは色・輝度等の設定で、調査したDriverにはLUT/refresh/RAM転送方式を保存するNVS経路はない。従って主な上書き要因はstartupとruntimeコマンド。NVSの表示orientation/font/色等は別途保持し、EPD既定化に便乗して消さない。[display.c:223](../src/services/solar_os_display.c#L223)

### Wi-Fi ONは「初期化許可」と「通信開始」を分ける

`wifi_load_boot_policy`は未保存ONだが、保存値を優先する。`solar_os_boot_services_init`は`solar_os_wifi_init()`を呼び、同initは最後にOFF stateで戻る。ON/startと保存先への接続は別APIで、`wifi on`のShell handlerが既存のstart/connectを組み合わせている。[wifi.c:149](../src/services/solar_os_wifi.c#L149)、[boot_services.c:130](../src/services/solar_os_boot_services.c#L130)、[wifi.c:1511](../src/services/solar_os_wifi.c#L1511)、[shell_network.c:485](../src/shell/solar_os_shell_network.c#L485)

最小案はPaperMonoのみ、サービス準備後にboot-policyがONならstartし、保存済みSTA情報があれば既存接続を要求する。未設定・AP不在でもBootを待ち続けない。過去に明示保存したOFFは尊重し、ON既定と矛盾する現状として移行一覧に載せる。常にONへ強制書換えする仕様とはしない。AP/NAT/Repeaterを新規に自動有効化しない。

Wi-FiのコードからNTP自動起動は見つからず、Board job自動開始の実体も現状no-op。`ntp-sync`はjob startが必要。将来Wi-FiをONにしてもIP取得イベントからNTPを開始しない。RTCからの時刻復元は継続。TLS証明書検証に使う時刻が不正な場合はManual更新で理由を出し、勝手なNTP起動やTLS検証無効化で解決しない。[board_boot.c:3](../src/services/solar_os_board_boot.c#L3)、[ntp job:208](../src/jobs/solar_os_ntp_sync_job.c#L208)、[time.c:488](../src/services/solar_os_time.c#L488)

### USB Shell自動起動の最小案

既存`solar_os_port_shell_start_with_options`を呼び、`run_startup=false`にする。画面Shellのstartup実行を二重化しない。USB接続中だけの一時処理ではなく、既存CDC port/workerの接続再開を使う。既存port占有チェックを再利用し、必要ならport layer内部に同一portの起動済み判定を小さく追加する。新しいShellタスク・USBスタックは作らない。[port_shell.h:21](../src/services/solar_os_port_shell.h#L21)

### startupの削除案（実ファイル未変更）

| 行の候補 | 既定化後の扱い | 機能・Shell自体 |
|---|---|---|
| `lut 5`、`... refresh=fastest`、`... fastest-path=tile`、`... spi-mhz=20`、`... dirty-window=rows-dual` | 同じ既定が先に適用されることを確認して重複行を削除候補 | 選択/診断APIの削除とは別 |
| `session create shell cdc0 ...` | 自動起動の存在確認後に重複行だけ削除候補 | USB Shell、手動Session操作は維持 |
| `wifi on`、保存先への接続行 | Board ON/connectの動作確認後に重複行を整理。資格情報はログや台帳へコピーしない | wifi on/off/enable/disable/connectを維持 |
| `job start ntp-sync ...`、起動時の明示NTP同期 | 本方針では自動実行停止候補 | 手動NTP・RTC・Clockは維持 |
| `refresh=hybrid/typing`、hybridパラメータ、`lut 2/3/4`、dose/hold、旧dirty-window試験値、timing=on、試験自動反復 | 採用既定を上書きするため実際に存在する行を特定して削除案 | 実験Buildの研究機能は保存。通常版公開コマンドの整理は下記 |
| BLE設定、キーボードprofile、font/zoom、ユーザースクリプト、未知の行 | 依存性と意図を個別確認し保持 | 一括削除しない |

移行時は読取り→原文バックアップとhash→行ごとの差分案→承認された変更だけ→読戻しと再起動確認とする。今回は読取り可能な過去コピーの確認までで、本体ファイルを書き換えない。startupを全て無効化する強引な方法ではNTP停止や設定移行を代用しない。

通常版から外す候補はD/E1/dose/holdの実験用選択、`writer-lut`旧alias、強制画像反復など。最初は研究Buildに隔離する案とし、古い実験LUTや研究Branchを消す意味ではない。Aによる復旧、F再選択、OTP cleanup、Trace/status/Async、明示launcher、手動NTP・Wi-Fi・disk・Helpは残す。Shell実装・補完・help/manual・Hostケースは、将来コマンドを変更する際に一緒に更新する。

## 3. Shell Escの局所変更

対象は`solar_os_shell.c:10152–10160`の「PaperMonoタッチGrid＋Launcherあり＋入力空＋画面terminalあり」でREPLACE launchする分岐のみ。これを削除する案。続く`input_len>0`のhistory解除・入力行クリアは残す。[該当分岐](../src/apps/solar_os_shell.c#L10151)

結果：空行Escはその場、入力中Escは取消、物理B長押しも同じEscとしてその仕様に従う。USB Shellはterminal条件の特殊分岐をもともと通らず、通常入力処理も変更しない。明示`launcher`は維持。他AppのEsc/CHILD_RETURN/終了処理、KEY2の長押し判定には触れない。ShellをLauncherから外した後も、CLIやSession切替で起動可能にする。

## 4. PaperMono Launcher：2行×3列

ユーザー確認済み配置は次のとおり。本文の「2列×3行」は採用しない。

| | 列1 | 列2 | 列3 |
|---|---|---|---|
| 行1 | Manual：`help` | Reader：`files --reader` | Wi-Fi：`wifi` |
| 行2 | Keyboard：`typemouse` | Sketch：`sketch` | Clock：`clock` |

PaperMonoは`SOLAR_OS_BOARD_LAUNCHER_TOUCH_GRID`の分岐で配列を作り、通常JSONを読み込まない。配置の変更はそのnames/commands/icons配列に限定する。グリッド計算・タッチrelease時の起動・lease/handoff・Sleepヘッダーを再利用。他BoardのJSON既定やユーザー設定ファイルを変更しない。[launcher.c:481](../src/apps/solar_os_launcher.c#L481)、[タッチ/Sleep:781](../src/apps/solar_os_launcher.c#L781)

Manualは新アプリを作らずApp Registryの`help`、Readerは既存の選択画面を残す。Files/ShellはLauncherの枠から外すだけ。Keyboardアイコンも維持。Wi-Fi画面は現状CHAR操作中心なので、「Launcherからタップで開く」と「Wi-Fi画面内部を全面タッチ操作できる」を同一視しない。後者を本依頼へ自動追加しない。[help登録:236](../src/apps/solar_os_app_registry.c#L236)、[Wi-Fi event:1019](../src/shell/solar_os_shell_wifi_tui.c#L1019)

Wi-FiはApp Registry直起動ではなくShell handlerから既存TUIを要求し、Launcherがそのrequested_appをCHILD_RETURNへ変換できる。一方、保存済みWi-Fi boot OFFではhandlerがTUIを要求せず戻るため、単なる配列追加だけだとLauncherが終了してShellに戻る可能性がある。Launcher専用経路から既存`solar_os_shell_launch_wifi_tui_ex(..., CHILD_RETURN)`を使い、OFF時も状態説明と戻る操作を可能にする最小案とする。OFFを勝手にONへ書き換えない。TUI内で許可されない操作の表示も確認対象。[launcher.c:615](../src/apps/solar_os_launcher.c#L615)、[OFFのguard](../src/shell/solar_os_shell_network.c#L524)、[既存TUI API](../src/shell/solar_os_shell_wifi_tui.c#L1216)

## 5. Manual一覧のタッチ設計

### 既存実装を使える部分

`solar_os_docs_app.c`はTUI Appで、cursor/top/collapsed_sectionsをSession stateに持つ。セクション/ページの可視index計算、開閉、キーボード移動が既存。ページは`reader man:<id>`をCHILD_RETURNで起動し、resumeではstate初期化せず再描画するので、同じ一覧へ戻ると選択・スクロール・開閉状態を保持する設計が既にある。start時のmemsetをresumeで再実行しない。[state:27](../src/apps/solar_os_docs_app.c#L27)、[描画:258](../src/apps/solar_os_docs_app.c#L258)、[子起動:334](../src/apps/solar_os_docs_app.c#L334)、[resume:452](../src/apps/solar_os_docs_app.c#L452)

現状のeventはCHAR以外を無視し、lease/suspendのtouch処理がない。Pointer対応とlifecycleだけを追加する。[event:468](../src/apps/solar_os_docs_app.c#L468)

### 最小の追加

- 同じApp stateに既存`solar_os_app_touch_t`とgesture開始行/開始top/最大移動量/dirtyフラグを持つ。既存RESUMABLE flagを保持してPOINTER_EVENTSを宣言し、Launcherのtouch handoff対象として扱う。別タスク、独立Touch Driver、新Frame経路は不要。
- local displayとterminalがある場合だけleaseをacquire。USB/remoteのTUIではacquireしない。Pointer座標は既存Session回転済み座標を使い、`solar_os_terminal_hit_cell`で表示セルへ変換する。固定のfont幅や800px換算を重複実装しない。[hit-cell:213](../src/services/solar_os_terminal.c#L213)
- 一覧はtitleがrow 0、項目がrow 1から。footer/余白は除外し、`top + (row−1)`を既存`docs_node_at`へ渡す。表示された可視木とhit判定のSingle Sourceを共有する。
- タップはreleaseで確定。未選択ページは選択だけ、同じ選択ページの再タップで既存open処理を呼ぶ案。セクション行は選択＋展開/折畳み。連続タップを次のReaderの入力へ持ち越さない。
- 上下スワイプは行高基準でtopをclampし、指に合わせて一覧を移動。小さい揺れと意図的swipeを分け、scrollとして確定したgestureをtap/openに変換しない。閾値は既存adapterの12px/既存Appの48pxを初期根拠とし、画面・行高で確認する。スクロール後に`docs_app_ensure_visible`が古いcursorへtopを戻さないよう、cursorを表示範囲へ合わせる。
- MOVEごとに無条件EPD更新せず、可視行/選択が変わった場合に既存event/TICKから集約描画する。releaseで最終位置を反映する。Manualのswipeのために新しい更新タスクを作らない。
- suspendでgestureを取消し実際のApp遷移/Sleepならleaseを返す。resumeでcanceling判定→必要ならlease再取得→再描画。stop/state_release_cleanupでも返却。CANCELによるbalanced suspend/resumeでpower cycleや余分な描画をしない既存adapterを再利用。[app_touch.h](../src/apps/solar_os_app_touch.h#L12)、[Reader lifecycle](../src/apps/solar_os_reader.c#L1449)
- 既存Session lifetime/epoch/全離指gateを保持。TUIでもdisplay_targetにbindされる現在のroutingを使う。Readerへ開いた指のreleaseや復帰前の接触を一覧のtapとして採用しない。[Session adapter:50](../src/services/solar_os_sessions_touch_adapter.inc#L50)
- キーボードのUp/Down/PgUp/PgDn/Home/End/Left/Right/Enter/Space/q/Escは既存処理をそのまま共有する。

SD抜去/Manual更新で一覧構造が変わる場合は、cursorの数値だけを盲目的に保持せずtopic IDを可能なら維持し、見つからなければ有効範囲へclampする。これは通常のReader子復帰で一覧stateを消す要求ではない。

## 6. microSD：既存SDMMCへの最小Board対応

### ピンと電源

| 信号 | 接続 | SolarOSでの表現案 |
|---|---|---|
| CLK/CMD/DAT0 | GPIO13/12/11 | native SDMMC pin |
| DAT1/2/3 | GPIO10/9/8 | 4bit時に使用。1bit時は−1 |
| TF_EN | M5IOE1 PYG14 | **`ioe0:13`**、push-pull、HIGH=ON |
| TF_DET | M5IOE1 PYG1 | **`ioe0:0`**、INPUT、LOW=挿入 |

M5IOE1の表記PYGは1始まり、既存providerのlineは0始まり。GPIO14はEPD MOSIなので、SD電源のPYG14をGPIO14として設定してはいけない。[既存provider:40](../src/services/solar_os_m5papermono_power.c#L40)、[write時line+1表示:83](../src/services/solar_os_m5papermono_power.c#L83)

[M5Stack公式PinMap](https://docs.m5stack.com/en/core/PaperMono#microSD)で6信号、TF_EN、TF_DETを確認した。TF_DETの外部pull-upはSD電源domainで、挿入時LOW。[公式電源構成](https://docs.m5stack.com/en/arduino/papermono/m5pm1_m5ioe1)もmicroSDをIOE配下とする。

公式UserDemoのローカル参照は`c1099107271d31a0678d661a896e2b04dbb331ea`、[hal_tf_card.cpp:14–40](https://github.com/m5stack/M5PaperMono-UserDemo/blob/c1099107271d31a0678d661a896e2b04dbb331ea/main/hal/hal_tf_card.cpp#L14)にIOE入力pull-up、[L138](https://github.com/m5stack/M5PaperMono-UserDemo/blob/c1099107271d31a0678d661a896e2b04dbb331ea/main/hal/hal_tf_card.cpp#L138)に電源ON後300ms・4bit・自動format=false、[L204](https://github.com/m5stack/M5PaperMono-UserDemo/blob/c1099107271d31a0678d661a896e2b04dbb331ea/main/hal/hal_tf_card.cpp#L204)にLOW挿入を確認。SolarOSでのOFF中検出の成立を保証する実機結果ではない。

### 現状の不足と追加先

| 項目 | コードから確定したこと | 最小案 |
|---|---|---|
| Board登録 | 現ManifestにSD/storage capability/deviceがなく、GPIO8～13はSD予約 | 既存`storage_sdmmc.cmake`/expansion_sdmmc、Board capability、pin定義を追加。IOE power providerの後にSDを準備 |
| 電源binding | `solar_os_sdmmc`のpowerはnative GPIO整数のみ。Board storage powerもgpio_set_level | SDMMCのpower bindingだけ既存GPIO_LINEを受け付けるよう拡張。native互換を残し`solar_os_gpio_line_*`へ接続 |
| 検出binding | detect roleはない。slot gpio_cdへIOE値を直接渡せない | GPIO_LINEのdetect＋active levelを追加。IOEでreadし、native SDMMCのcard-detect IRQと混同しない |
| 1bit/4bit | d1～d3全無しなら1bit、3本全て有れば4bit。S3 GPIO matrix対応条件あり | 初期Board案は公式同様4bit。切り分け用1bit構成を残す。両モードの成立・周波数は実機未確認 |
| 未挿入 | 現在はcard initを試し失敗を返す。検出に基づく早期判定なし | detect errorとカード無しを区別。無しならHost初期化を省略しoptional storageとしてBoot続行。I2C失敗を「無し」と偽らない |
| 電源時系列 | native powerではOFF80ms→ON120ms。IOE経路は未接続 | 同じprepare/power責務に接続し、ON安定待ちをBoard値で管理。公式300msは初期根拠。Board layerとDriverが二重にpower cycleしない |
| mount/unmount | 既存volume/FATFS mount、host deinit、rescanがある | そのまま再利用。再mount前のdetectとerror診断を足す。自動format・データ移動を呼ばない |
| 再挿入 | 検出workerや自動再mountを持たない | まず既存disk status/rescan/mount/unmountで完結。自動通知が必要なら既存main loopの低頻度pollへ小さく接続。`expansion_poll`は現在retry用で汎用SD監視が既にあるとはしない |
| 使用中抜去 | 現Driverに媒体所有者の一律I/O排他・抜去保証はない | 明示unmountを基本とし、使用中は拒否/利用者を閉じる。抜去エラー時はI/Oを失敗扱い、Manualはfallback。hot-unplugで無破損とは保証しない |
| Sleep/Wake | SDMMC driverにset_suspended hookが無い | 既存Expansion suspendへ最小hook。初版はI/Oを収束させ電源/Mountを維持する案。起床時detect/statusを再確認。無条件power OFF→ONはしない |

根拠：[Manifest](../boards/manifests/m5paper_mono.toml#L12)、[SDMMC bindings](../src/services/solar_os_sdmmc_driver.c#L3)、[attach](../src/services/solar_os_sdmmc.c#L68)、[slot幅](../src/drivers/sd_card.c#L276)、[configure](../src/drivers/sd_card.c#L366)、[native power](../src/drivers/sd_card.c#L410)、[init](../src/drivers/sd_card.c#L873)、[unmount](../src/drivers/sd_card.c#L1134)、[suspend framework](../src/services/solar_os_expansion.c#L1641)。

Sleep中にSDを省電力OFFする拡張は、open fileのclose/flush→unmount→host deinit→power OFF、Wake後power/再mountの順序とアプリ通知を整えてからにする。Light sleepでhost/peripheral状態が有効かは実機確認が必要。Deep sleep/電源断はcold initで扱う。共有IOEレジスタは既存providerのmutexとmasked writeを使い、EPD・Touch・LED・charger muxの状態を壊さない。SD電源保持による消費電流は未測定。

### GPIO・Busの競合

GPIO8～13は現ManifestでSD予約。EPD SPIはGPIO14～18、Touch/RTC/IOEはI2C47/48、KEYは2/3、USBは19/20で直接重複しない。4bitのDAT3=GPIO8とIOEのline8等は別namespace。SDMMC Hostを使いEPDのSPI busを再初期化しない。Manifest/resource claimに6ピンとIOE line0/13を登録し、拡張デバイスからの二重取得を防ぐ。将来のLoRa/NFCや手動GPIO設定との競合も同じresource管理で診断する。

### 最重要：SD搭載扱いと保存先を分離

現`storage_flash_default_mount_point()`は`SOLAR_OS_BOARD_HAS_SD`なら`/flash`、無ければ`/`。`solar_os_storage_mount_point()`はSD mountedならSDを優先し、相対パスの既定にも影響する。このままSDを有効化すると、同じFlashデータでもユーザーからパスが変わり、Writer/Sketch/履歴/設定/Launcher JSON等の読書き先も変わり得る。[storage.c:80](../src/services/solar_os_storage.c#L80)、[初期化:120](../src/services/solar_os_storage.c#L120)、[優先順位:254](../src/services/solar_os_storage.c#L254)、[Flash定数](../src/drivers/flash_storage.h#L10)

**推奨案：PaperMono限定でFlash root=`/`と既定保存先Flashを維持し、SDを`/sdcard`へ追加。SD capabilityは正しく有効にするが、primary storageとは分ける。** 最小のBoard policy（名称案：primary-flash/keep-flash-root）を上記2箇所へ適用する。カードの有無や再挿入で既定保存先を切り替えない。RAMFS rootがある構成の`storage_default_base_path`も確認対象。既存ファイル移動、format、partition変更をしない。

startupもPaperMonoではFlashを既定実効元とする。NVSがAUTO/SDを保持する場合を表示し、汎用AUTOのSD優先規則をそのまま適用しない。既存の明示SD起動設定があれば互換影響として移行時に通知する。ユーザーのSD startupを勝手に削除せず、通常Bootでカードから新たなスクリプトを実行しない。これらの保存先保護を先に実装・Host確認してからSDを有効化する。

## 7. Help/ManualをSDから利用する設計

### 再利用できる機能

`help`は一覧、`man`はtopic/search、`help status`はFirmware/Manual版・revision等、`help update`は署名付き完全版取得。外部ManualはSDの`.solar/docs`を使用し、内蔵側はsetup/recoveryとdownload案内が中心。SDなしでも全チュートリアル本文が入っているわけではない。[manual-system](manual-system.md)、[docs root:230](../src/services/solar_os_docs.c#L230)、[manual fallback:299](../src/services/solar_os_manual.c#L299)

現在の検証はcatalogのECDSA P-256/SHA-256署名、archiveのsize/hash、抽出後各ページのsize/hash。更新成功後にactive pointerを変更し、既存revisionは閲覧者のため保持する。Bootはcatalogを再検証し、ページ閲覧時にも署名・ページhashを確認する。更新中断で不完全なrevisionを採用しない既存処理を再利用する。[署名:450](../src/services/solar_os_docs.c#L450)、[Boot:1078](../src/services/solar_os_docs.c#L1078)、[ページ:1163](../src/services/solar_os_docs.c#L1163)、[更新:1298](../src/services/solar_os_docs.c#L1298)

更新は現在のFirmware versionと一致するcatalogを要求する。一方、保持済み旧版は読める場合があり、一覧headerとhelp statusでoutdatedを表示する。[版検証:519](../src/services/solar_os_docs.c#L519)、[一覧header:230](../src/apps/solar_os_docs_app.c#L230)。Wi-Fi OFFでも外部ページはSDから読める。help updateは明示操作で、ネット接続時の自動downloadは今回追加しない。

### SD抜差しと一覧の状態

Boot時のdocs_initだけでは、起動後に挿したカードのindexが自動で有効になる仕組みは揃っていない。SD mount/unmount成功後に、既存Docsの再評価/内蔵fallbackへの切替を直列化して呼ぶ小さな接続が必要。現在のcached indexを「カードなしでも完全版利用可能」と表示し続けない。更新workerと競合しないよう、update中のunmount拒否・操作完了後の再評価を整理する。Readerが既にメモリに読んだ本文と、次のページのSD I/Oは区別する。

Manual stateは同じtopic IDを優先して保持するが、媒体変更・catalog変更でtopicが消えたらcursor/topをclamp。偽署名・ページ破損では内蔵Helpへ戻し理由を表示。カードや署名が無いから検証を迂回しない。

### 独自Firmware用の配布

配布元は現在OTA URLと連動し、`/latest`なら実行中の`SOLAR_OS_VERSION`へ置換、`/doc/catalog.json`等を取得する。署名公開鍵も`SOLAR_OS_OTA_PUBLIC_KEY_PEM`共用。NVS保存済みOTA URLがあれば、BuildのURL既定を変えただけでは切り替わらない。[URL生成:1047](../src/services/solar_os_docs.c#L1047)、[署名:459](../src/services/solar_os_docs.c#L459)、[OTA NVS:370](../src/services/solar_os_ota.c#L370)

独自機能を説明する完全版は、forkの`doc/manual/*.md`から既存`generate_manual.py`でcatalog/ZIPを作り、対応する信頼鍵でcatalogの正確なbytesに署名してHTTPSで配布する必要がある。上流の秘密鍵を持つ前提にはしない。公開鍵だけでは署名できない。現在上流と同じ4.16.1等を名乗るだけでは、正しく署名された上流Manualと独自版の違いを自動判別できない。

最小推奨は独自版を識別できるFirmware versionと独自配布元を用意し、OTA/Manual両方の信頼先を一貫して管理すること。OTAの配布運用を変えたくない場合は、DocsだけのURL/公開鍵のBoard/Build override（未実装案）を設け、未指定時は従来OTA値を使う。**Manualのためだけに保存済みOTA URLを無断で変更しない。署名検証を無効化しない。** schema拡張の大規模設計は今は不要だが、独自機能の版表示をhelp statusとManualへ含める。

## 8. Sketch二方式切替とTypeMouseの保護

現在の局所描画版は、論理Canvas上の全線分を保持し、paint regionだけXBM変換・画面bufferへ描き、既存Asyncへsnapshot submitする。初回/UI/modal/相対入力等は全体描画へfallbackする。局所描画自体が過去の高速化成果なので、既存「遅い旧版」と新候補を名前だけで別方式と決めない。[sketch.c:791](../src/apps/solar_os_sketch.c#L791)、[render:850](../src/apps/solar_os_sketch.c#L850)

設計案は`sketch-render status|local|candidate`という**未実装**のShell selector。既存`sketch`起動引数と混同せず、App/Sketch設定層が保持する。共通DriverのLUT/RAM selectorとは独立させ、両方ともF/fastest/tile/SPI20/rows-dual/Async、同じ入力/Canvas/保存処理を使う。

切替時は現submitの世代を確認し、保持Canvasを消さずに未反映領域と最終離指を反映、方式の描画cacheだけ無効化して必要な再描画を1つ予約する。比較中に自動clear/OTP/反復はしない。方式の変更を波形変更やRAM full/rowsの変更で代用しない。既定は比較完了までlocal、selectorは当面RAM状態としNVS/startup依存を増やさない案。

高速化候補がどのCommit/変更群を指すか、今回のローカル資料だけでは「現行局所描画とは異なる完成済み候補」を確定できない。候補の同定・差分確認が実装前の未解決項目。新アルゴリズムをこの依頼で発明・実装しない。切替機構を設計しただけで比較・採用完了とはしない。

TypeMouseは現在のFrame consumer、Tap/Hold固定・キー反転・JIS/BLE Keyboard/Mouse・モード切替・終了時解放を維持する。F既定化とManualタッチのためにTouch DriverやSessionの別案を取り込まない。現行TypeMouseがすでに`TOUCH_FRAME`を使うことと、保留指定の「新Touch Frame経路」は区別する。[現行F側App](https://github.com/taichan1113/solar_os/blob/70e1c0ffeaf4d4e9e616d47a28ee3e5aaf0386cc/src/apps/solar_os_type_mouse.c#L433)

## 9. 統合順序・将来の検証

台帳§4の順序で小さく統合する。EPD既定化、Boot/USB/Wi-Fi、startup整理、Shell/Launcher、Manual Pointer、Flash保存先policy、SD基盤、SD lifecycle/Manual配布、Sketch切替を別変更として検証できる状態にする。全機能を一括変更してから実機原因調査する計画にはしない。

| 段階 | 将来のHost/Buildで確認すること | 人手実機確認 |
|---|---|---|
| EPD既定 | 全sourceがF、初回OTP→warm、復元後もF、Reset/Wake/失敗、A復旧、既存RAM/Shadowテスト | 再起動・各Appの黒文字/Sketch黒線・残像・反転 |
| Boot/startup | USB重複なし・startup1回、NVS OFF/未設定、NTP job未開始、既存startup行の移行差分 | USB抜差し、無線OFF保存/再起動、接続先なし起動、RTC |
| Shell/Launcher | 空行Esc無遷移、入力取消、USB不変、専用配列順・他Board JSON無変更 | キーボードなし起動、B長押し、明示launcher、Sleep/Wake |
| Manual Touch | セルhit/スクロール/折畳み/末尾、CANCEL、Session復帰、lease失敗とcleanup、キーボード操作 | swipe/tap/再tap、Readerから位置保持、最初のタッチの持越しなし |
| SD保存先保護 | SD有無でFlash root/既定保存先/startup不変、NVS AUTO/SD、既存pathとRAMFS | 既存文書/PNG/履歴/startupの同じパスでの読戻し |
| SD実装 | 1/4bit pin、IOE masked write、未挿入/error、mount/unmount/再挿入、format未呼出し、使用中I/O、Sleep hook | カード無しcold boot、カード種類、再mount、Light sleep/Wake、他peripheral回帰 |
| Manual配布 | 署名/版/hash/ZIP失敗・中断・抜去、内蔵fallback、topic ID維持、URL/鍵独立性 | help update→Wi-Fi OFF閲覧、未挿入/版違い警告 |
| Sketch比較 | 両方式の同じCanvas、座標/末端/Queue/保存、切替時no stale frame | 同条件直線/ジグザグ/曲線、追従性、保存再読込、終了再開 |

**今回は上記テストを実行していない。** Firmwareソース無変更なのでHostテスト・Buildも行わない。元Firmware・NVS・FAT・bond・startupの保持は「アクセスしない」ことで維持し、新しい実機保護領域照合を実施済みとは報告しない。変更後の文書リンク・diffと既存Worktree状態のみを確認する。

## 10. 実装前に残る確認事項

1. 前回の正式採用台帳の所在と、今回補完台帳とのID対応。
2. 現在本体のstartup/NVSと実効設定。特にWi-Fi OFF、startup source、OTA URL、表示の実験上書き行。
3. Sketch高速化候補のCommit/方式。局所描画版との実際の差分と比較条件。
4. SDのIOE検出電源条件、1/4bit、SDMMC/IOE保持とLight sleep、使用中unmountの所有者管理。
5. 独自Manualのversion・URL・信頼鍵と配布担当。既存OTA運用を変えるか、Docs overrideを追加するか。

いずれも未確認をPASSとせず、実装の依存順を明示した。追加の新Touch経路、独自BLE/Storage stack、新しい波形、資料調査だけのためのFlashを導入しない。
