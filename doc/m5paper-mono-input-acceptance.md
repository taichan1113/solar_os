# PaperMono system KEY・基本入力 acceptance

対象: SolarOS `1ba98726e0c52f5147c8a565522a94d7b67b0291` + このport。
確認日: 2026-10-07。外部Quick Startではなく、このcheckoutの実装を正とする。
画面は横800×480、orientation=270、USB-Cが左側面上寄り。OTP全画面更新は約4.5秒、
Writerのカーソル点滅無効を維持。描画が落ち着いてから操作する。
文字入力後は複数回の全画面更新が続くことがあり、10秒で全操作が反映されるとは限らない。
ボタン試験は画面更新が止まってから行う。

## 入力経路と確定した仕様

KEY1は `key` capability + `SOLAR_OS_BOARD_PIN_KEY=GPIO_NUM_2` で登録する。
active level=0、pull-up=1、pull-down=0。
`src/main.c` の `key_button_init()` がGPIOと両エッジISRを設定し、ISR通知を受ける
`poll_key_button()` が共通runtimeで押下・解放・長押しを判定する。
GPIO2は `SOLAR_OS_BOARD_BUTTONS` から除去し、二重処理を防ぐ。
`SOLAR_OS_BOARD_KEY_SHORT_INPUT_KEY` は定義しない。

| 操作 | このrevisionの実際の仕様 | 実装 |
| --- | --- | --- |
| 短押し | 押下観測から30ms以上1200ms未満で解放。未設定NVSの既定はsuspend | `src/main.c`: `poll_key_button`, `handle_key_short_press`; `src/services/solar_os_power.c`: `solar_os_power_init` |
| power action | `power key off / sleep / suspend`。保存済みNVS値を優先。`off`は短押し操作無効であり電源断ではない。旧名`light`は`sleep`のalias | `solar_os_power.c`: `solar_os_power_parse_key_action` |
| suspend | 描画を止め、電源profileを一時lowpowerにする。runtime・radio・jobs・sessionは残る。もう一度KEY1短押しで復帰 | `main.c`: `enter_suspend`, `exit_suspend`; `solar_os_power_begin_suspend/end_suspend`; `solar_os_display_suspend_primary/resume_primary` |
| sleep | 既存の明示light sleep。radio停止とKEYのRTC EXT1 wakeを設定。GPIO2の押下で復帰し、radio/displayをresumeする。deep sleepとは別 | `main.c`: `enter_sleep`, `enter_light_sleep`, `key_prepare_rtc_wakeup`, `resume_display_after_sleep` |
| 長押し | 1200ms以上の押下をruntimeが観測すると1回発火。解放時に短押しを追加発火しない | `main.c`: `KEY_LONG_PRESS_MS`, `key_long_press_fired` |
| 長押しBLE動作 | 今回bootでBLEが有効なら、毎回`solar_os_ble_keyboard_forget()`、続けて`solar_os_ble_keyboard_start_pairing()`を呼ぶ。forgetが非同期ならpairingを待機 | `src/services/solar_os_ble_keyboard.c`: `forget_remembered_keyboard`, `complete_deferred_bond_forget`, `solar_os_ble_keyboard_start_pairing` |
| pairing中の再長押し | cancelトグルではない。同じforget/pairing要求を再実行する。スキャンtaskやpasskey処理中は`ESP_ERR_INVALID_STATE`となり得る。pendingなら待機、接続処理中なら停止・再要求の経路もある | 同上、`start_pairing_scan_now` |
| BLE無効時の長押し | BLE処理を行わず、短押しにも変換しない | `main.c`: `poll_key_button` |
| suspendからBLE復帰 | 接続・新規キー押下によるwake generation変更を検出しresumeする | `main.c`: `maybe_exit_suspend_for_ble_keyboard`; `solar_os_ble_keyboard.c`: `note_wake_activity` |
| KEY2 | GPIO3、active low、pull-up。25ms debounce後の解放でEnterを送る | `src/services/solar_os_buttons.c`; `solar_os_input_write_char`; `SOLAR_OS_KEY_ENTER='\n'` |

system KEYは今回generic input sourceを開かない。`input test buttons`のKEY1カウンターが
増えないのは正常である。KEY1は電源状態・BLE状態・ログでEnd-to-End確認する。
KEY2の`buttons` sourceとBLEキーボードの入力は既存input serviceからsessionのfocusへ届く。
長押し判定は割り込み内の独立タイマーではない。全画面更新中に短い押下を完結させず、
アイドル状態で短押しを約0.2〜0.5秒、長押しを約2秒行う。
Writerで短押しが観測されない場合は、描画停止を待って約0.8秒で再試験する
（1.2秒以上にしない）。KEY2は長押し機能がないため約2秒保持後の解放でも確認できる。
描画中に完結するボタン操作を必ず捕捉できる、という保証はこの共通poll経路にはない。

## 比較した既存board

すべて `boards/manifests/` の現行定義を確認。

| board | system KEY | 短押しの扱い |
| --- | --- | --- |
| `solar_term` | GPIO18、active low、pull-up | 共通power action |
| `esp32_s3_devkitc1_n16r8` | GPIO0、active low、pull-up | 共通power action |
| `freenove_esp32_s3_display_4_0` | GPIO0、active low、pull-up | 共通power action |
| `elecrow_crowpanel_esp32_s3_4_2_epaper` / `5_79_epaper` | GPIO2、active low、内蔵pullなし | 共通power action。別の汎用EXITボタンもある |
| `waveshare_esp32_s3_epaper_3_97` | GPIO0、active low、pull-up | `KEY_SHORT_INPUT_KEY=APP_EXIT`というboard例外。長押しは共通BLE処理 |
| PaperMono / bringup | GPIO2、active low、pull-up | 共通power action。両profileで同じ |

PaperMonoでは物理App Exitがなくなる。WriterはBLEキーボードのEscで終了要求する
(`src/apps/solar_os_writer.c`: `writer_handle_char`, `writer_request_exit`)。
未保存なら確認ダイアログが出る。`q`はWriterでは文字入力であり汎用終了キーではない。
KEY2長押し・double-click・comboは追加しない。USBの`session list`と`session close <id>`も
診断に使えるが、編集データがあるsessionを不用意に閉じない。

## USB準備

今回の実機はCOM39。再接続で番号が変わった場合はdevice listで確認する。
アップロードとmonitor、複数のmonitorを同時に開かない。

```powershell
$env:PLATFORMIO_CORE_DIR = "$PWD\.pio-core"
$env:UV_CACHE_DIR = "$PWD\.pio-core\.cache\uv"
$env:PYTHONIOENCODING = 'utf-8'
$env:SOLAR_OS_BOARD = 'm5paper_mono'
$env:SOLAR_OS_FLAVOR = 'writerdeck'
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
& $pio run -e esp32_s3_devkitc1_n16r8 -t upload --upload-port COM39
& $pio device monitor --port COM39 --baud 115200
```

この実機は`/.shell/startup`の`session create shell cdc0 --term dumb --size 80x25`で
USB shellを起動する。通常のファーム更新でこのファイルを作り直す必要はない。
新しい個体の初期設定は[port手順](m5paper-mono.md)参照。

起動完了後、USB shellで以下を実行する。`log level`と`log sink`は保存される設定なので、
検証前に`log status`で元の値を控え、終了後に必要に応じて戻す。

```text
log status
log level info
log sink cdc on
power status
power key
ble status
input buttons
input test buttons
input test touch0
session list
```

## A. KEY1短押し / suspendとresume

1. `power key`で現在値を確認。既定確認とは分けて、再現可能な試験では
   `power key suspend`を設定する。この実機の変更前保存値は既に`suspend`だった。
2. KEY1（GPIO2/Button A）を短く押して離す。赤い電源/RESETボタンとは別。
3. ログ`KEY short press: suspended; profile=lowpower restore=...`と
   `power status`の`Suspend: active`を確認。E-paperは電源を落としても前の絵が残るため、
   白画面になることを判定条件にしない。低電力profileの自動light sleepによりUSB通信が止まる場合がある。
4. KEY1をもう一度短押し。全画面更新を待ち、ログ
   `KEY short press: resumed; profile=...`、`Suspend: inactive`、画面の復帰を確認。
5. `session list`で同じWriter sessionが残ることを確認。`input test buttons`のカウントは
   このKEY1操作だけでは増えない。

USBが止まった場合: KEY1で画面復帰した後、monitorを閉じ、USBケーブルを抜き差しして
monitorを開き直す。リセットは不要。`power status`、`log show 60`、`input test buttons`で
保存された状態・履歴を回収する。今回のCOM39実機でもこの手順で復旧した。
ESP32-S3のUSB Serial/JTAGはlight sleep中に動作せず、復帰後もホストが再列挙しない場合がある
([Espressifの説明](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32s3/api-guides/usb-serial-jtag-console.html))。
ローカルESP-IDF 5.5.5の`esp_driver_usb_serial_jtag/Kconfig`も同じ制約を説明する。
標準の`CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION`はUSB接続中の自動sleepを抑止する設定だが、
この変更では電源policyを変更せず、再接続で診断する。USB transportの停止をsession消失と混同しない。

任意の別試験: `power key off`では`KEY short press: sleep disabled`となる。
`power key sleep`では明示light sleepとなり、USBが切断/再列挙する場合がある。
KEY1で起こし、`wake from light sleep: cause=... ext1=0x0000000000000004`と
`power status`のLight sleep countを確認する。wake操作の解放は短押しとして二重処理しない。
KEY2やタッチ、BLE入力による明示light sleepからのwakeは今回の指定経路ではない。
最後に`power key suspend`へ戻す。追加の低消費電力最適化は本タスクの対象外。

## B. KEY1長押し / ZMK pairing

1. 両方のsplit半体を起動し、普段の左右間接続を成立させる。
2. ZMKのcentral側で、キーマップの`&bt BT_SEL n`に割り当てた操作を行い、未使用profileを
   選ぶ（nは0始まり）。空profileでは接続先募集のadvertisingを行う。左右半体間のbondを
   消さない。全profileを消す`BT_CLR_ALL`やsettings resetは不要。
3. USB給電のZMKがUSB出力を優先している場合は、キーマップの`&out OUT_BLE`でBLE出力を選ぶ。
   物理キー組合せはユーザーのZMKキーマップ依存であり、ここでは決め打ちしない。
4. SolarOSで`ble status`を確認。`current enabled`が必要。無効なら`ble enable`で次回bootの
   有効化を設定し再起動する。表示更新が止まった状態でKEY1を約2秒押して離す。
5. `KEY long press: BLE keyboard forget and pairing requested`を確認。
   `pairing scan start`、`ble status`のpairing/connecting、接続成功後の
   `BLE: connected ... , remembered 1/1`を確認する。スキャン窓は8秒。
   ほかのペアリング待ちキーボードは候補になり得るので、このZMKだけを募集状態にする。
6. passkeyが要求された場合だけ、表示された6桁をZMKで入力してEnter。
   設定によってはpasskey入力なしで進むため、必須とは仮定しない。
7. `input keyboard`で実際のsource名を確認し、`input test <その名前>`を接続前後で比較する。
8. 接続・記憶成功を確認してから、再起動後の自動再接続と`remembered 1/1`も確認する。
   セッションや未保存文書を保存してから再起動する。

KEY1を再長押ししてもcancelにならない。成功後に長押しするとその記憶済みキーボードを
忘れるので、通常の再接続には使わない。shellの`ble pair`は同じ標準サービスへの診断経路だが、
これだけの成功を「物理KEY1の長押しが成功」とは数えない。

ZMKの一次資料:
[Bluetooth behavior](https://zmk.dev/docs/keymaps/behaviors/bluetooth)、
[split](https://zmk.dev/docs/features/split-keyboards)、
[output selection](https://zmk.dev/docs/keymaps/behaviors/outputs)。

### 旧NimBLE bondの88/80警告

ESP-IDF 5.5.5の`components/bt/host/nimble/nimble/nimble/host/store/config/src/ble_store_nvs.c`
の`get_nvs_db_value()`は、`nimble_bond`名前空間の保存blobと現在の構造体サイズが違うと
`ESP_ERR_NVS_INVALID_LENGTH`を返す。`ble_nvs_restore_sec_keys()`の復元が中断し、
`get_nvs_db_attribute()`経由の新しいbond保存にも影響し得る。無害とは判断しない。

標準`ble forget`は`solar_os_ble_keyboard.c`で記憶済み相手に対して`ble_gap_unpair()`し、
確認できた後にSolarOSの記憶情報を消す。記憶0台なら`no remembered keyboard`で戻り、
旧ファーム由来の孤立・不正形式blobを全探索して削除する処理ではない。
また、古いmanualの「GATT cache消去」に対し、この実装には永続汎用GATT cacheはない。

問題時は`ble status`、`log show 80`、接続/保存失敗を含むserial logを保存し、記憶済み相手が
ある場合は`ble forget`の成功後に未使用ZMK profileで再試験する。現在profileを再利用する場合に
限り、対象を確認してZMKの`BT_CLR`を使う。警告が残るときに`ble forget`を反復しない。
NVS全消去・Flash全消去・独自の自動bond削除はこの変更では実施しない。
「接続できた」と「再起動後にもbondが使える」は別々に判定する。

今回の実機では、長押しから`travelers-split`へ接続し`remembered 1/1`になったが、
`persisting our sec/peer sec`でサイズ不一致が発生した。続く標準`ble forget`でも
`Error while removing LTK`が出たため、標準forgetだけでは復旧しなかった。
旧形式の`our_sec_1/2`と`peer_sec_1/2`が80 bytesであることを`nvs list nimble_bond`で確認し、
既存shellの選択消去機能でNimBLE領域だけを修復した。ファームには自動削除処理を追加しない。

```text
nvs list nimble_bond
nvs backup /papermono-before-bond-repair.json
ble forget
# forget後も上記エラーが残る場合のみ。自動的に再起動する:
nvs erase nimble_bond
```

バックアップは本体FAT上に保存済み（16,384 bytes、CRC32 `744aa820`）。拡張子にかかわらず
既存NVS backup形式であり、認証情報を含むので内容を公開しない。この選択消去はNimBLEが
管理するbond・関連情報を消すが、Wi-Fiや電源設定、FATファイルは消さない。
ZMK側には失敗試行のbondが残り得るので、再試験は別の未使用profile、または今回使ったprofile
だけを`BT_CLR`した状態で行う。通常のpairingで毎回実行する手順ではない。

## C. KEY2 Enter

1. 描画停止後のdisplay shellで`input test buttons`をUSBから取得し、開始カウンターを控える。
2. KEY2（GPIO3/Button B）を1回押して離す。
3. 再び`input test buttons`。正常ならkeyイベントが2増え、最後は
   `action=release physical=0 usage=0 key=10 modifiers=0x00`となる。
   これは物理解放時に生成するEnterのpress/release一組である。
4. 画面shellに空行/次のpromptが出ること、またはWriterで改行されることを確認。
   source登録だけでなくcounterとUI結果を揃えて合格とする。

## D. touch0

1. `input test touch0`を取得。タップ後pointerカウント増加を確認する。
2. 指を置いたままUSBで再取得し`action=press`または微動による`move`とbuttons=0x01を確認。
   離して再取得し`release`とbuttons=0x00を確認。
3. 端から少し内側の左上→右上→左下→右下を数秒間隔でタップし、各回の出力を保存する。

| 横向きUIの位置 | 診断で見えるportrait座標の目安 | dispatch後の論理座標 |
| --- | --- | --- |
| 左上 | (0,799)付近 | (0,0)付近 |
| 右上 | (0,0)付近 | (799,0)付近 |
| 左下 | (479,799)付近 | (0,479)付近 |
| 右下 | (479,0)付近 | (799,479)付近 |

有効範囲はX5〜475/Y5〜795で、角そのものの0/799が出るとは限らない。
`input test`のraw/valueはinput service内の校正前/後であり、画面方向変換の後ではない。
後段の`src/main.c:dispatch_input_pointer()`が既存
`solar_os_input_pointer_apply_orientation()`で`(799-y,x)`へ回す。
`session create launcher display0`で表示アイコンをタップして、その場所のアプリが選ばれることを
確認するとGUIまでのEnd-to-End試験になる。Writerはタップを文字入力として扱わない。

## E. Writer / BLE typing / resume

1. USBで`session list`を取得。既存Writerがなければ`session create writer display0`。
   `writer`だけをUSBに入力するとUSB terminalで起動するので区別する。
2. `session focus display0`、`session list`でWriterがactiveであることを確認する。
3. ZMKで`SolarOS123`と入力しEnter。左右半体から少なくとも1文字ずつ入力し、約4.5秒の
   更新を待って文字と改行を確認。必要に応じてUSBでキーボードsourceのcounterを比較する。
4. KEY1短押し→suspend→KEY1短押し。`session list`の同じID、編集文字とカーソル位置の保持、
   続けて英数字を入力できることを確認。長押しはpairingのやり直しになるので使わない。
5. Escを押し、未保存なら標準確認ダイアログを確認。必要ならCtrl+Sでファイル名を指定して保存。
   `q`は終了キーとして使わない。

## 変更範囲・検証記録

今回の変更は `boards/manifests/m5paper_mono.toml`、継承を説明する
`m5paper_mono_bringup.toml`、両profileでKEY1を二重登録しない回帰テスト
`tests/test_board_manifests.py`、この手順書と`doc/m5paper-mono.md`。
共通system KEY、power、touch、Writerの実装は今回変更していない。
実機で判明した再接続不具合に対して`src/services/solar_os_ble_hid.c`と
`tests/host/ble_hid_test.c`も変更した。独自pairing処理ではなく、共通HIDの通知順序の修正である。
以前のport差分は[portレポート](m5paper-mono.md)に別記している。

- metadata全board検証、両manifest検証: 成功。
- Python: 538件、534成功・4 skip。system KEYの既存例外、BLE再pair、BLE wake/suspend、
  deep sleep境界を含む`test_runtime_boundaries.py`と全boardテストを含む。
- C host: input、keys、HID report、BLE scan policy、HID client、NimBLE adapter、
  BLE server、BLE service、BLE multi-peerの9実行ファイルが成功。
  電源関連ではBLE sleep lifecycleを実行。KEYの実時間判定とESP32の実sleepそのものは
  これらのホストテストで保証しない。
- 実機変更前: COM39接続、`power key suspend`、BLE current enabled、remembered 0/1。
- `m5paper_mono_bringup` / writerdeck: 初回123.62秒、HID修正後119.33秒で成功。
- `m5paper_mono` / writerdeck: 初回127.86秒、HID修正後33.68秒で成功。
  最終app 3,300,923 bytes、static RAM 52,264 bytes。
- 通常版をCOM39へ書き込み、Flash hash検証とboot成功。KEY1定義はGPIO2、generic buttonsは
  起動ログで1個、電源既定suspend、BLE有効を確認。これは登録・設定の確認段階。
- 最終firmware.bin SHA256: `e5268baf097fd948235e2e090a73c79ba7a7e217740ab3e97212654756fe71ad`。
  成果物: `build/papermono-writerdeck/` と `build/papermono-bringup/`。
- ログ: `.research/system-key-python-tests.log`、`system-key-bringup-build.log`、
  `system-key-writerdeck-build.log`、`.research/hardware/system-key-boot.log`。
- HID修正後もPython 538件（534成功・4 skip）とC host 9実行ファイルを再検証して成功。
  最終ログ: `.research/system-key-python-tests-final.log`、`system-key-host-tests-final.log`、
  `system-key-ble-fix-build.log`、`system-key-ble-fix-bringup-build.log`。
- 物理操作とEnd-to-Endの結果は下記に追記する。ホストテストの成功から推測しない。

### 2026-10-07 実機入力結果

| 対象 | 登録 | 物理イベント・状態 | End-to-End |
| --- | --- | --- | --- |
| KEY1短押し | GPIO2 system KEY | 初回boot後200.609秒にsuspended、216.147秒にresumed。最終Writer試験も455.698秒にsuspended、578.861秒にKEY resumed | ユーザーが画面復帰を確認。初回のUSBログ回収にはケーブル再接続が必要だった |
| KEY2 | buttons source 1個 | 初回試験とWriter上の再試験でそれぞれkey counter 0→2、release / key=10 / modifiers=0 | input layerへの到達と、ユーザーによるWriter改行の確認が一致 |
| KEY1長押し・ZMK | BLE host ready、ble-keyboard登録済み | 物理長押しのforget/pairingログ、travelers-split接続、remembered 1/1 | 旧bond修復後は88-byte保存に成功。共通HID修正後は再起動・自動再接続・Writer入力も成功 |
| touch0 | 登録済み | 四隅のpress/release各1組、pointer counter 0→8、押下buttons=1/解放=0 | 実測座標と270°変換後の四隅が整合。GUIアイコンのhit-testは未実施 |
| Writer | display0のsession 1で起動 | ble-keyboard counter 0→30→38→46を観測。suspend/resume後もsession ID=1 | ユーザーが文字表示・改行・文章保持・追加入力を確認。KEYによる復帰とBLE activityによる復帰をそれぞれログで確認 |

短押しとKEY2の証跡: `.research/hardware/system-key-physical.log`、
`.research/hardware/system-key-replug.log`。後者の冒頭には切断中の古い出力が残るため、
再接続後の明示`power status`とruntimeログの時刻で判定する。

BLE証跡: `.research/hardware/system-key-pairing.log`（修復前の保存エラー）、
`system-key-standard-forget.log`、`system-key-bond-inspect.log`、`system-key-bond-repair.log`、
`system-key-pairing-repaired.log`（再pairing成功）、`system-key-bond-sizes.log`
（our_sec_1 / peer_sec_1 = 88 bytes）、`system-key-reconnect-boot.log`。
初回再起動では`remembered keyboard`の復元後に接続失敗した。
一時診断ログ`system-key-ble-diag-retry.log`で、ESP-IDFがCONNECTより先にENC_CHANGEを
通知し、既存HIDが初期値conn=-1を参照して`BLE_HS_ENOTCONN`になることを確認した。
共通HIDで早い暗号化結果と実接続番号を保持し、CONNECT後にGATT discoveryを開始するよう修正。
成功・暗号化失敗・CONNECT待ち中のcancelをホスト回帰テストに追加した。
一時診断ログ出力は最終コードから除去済み。
修正版の起動後は`system-key-ble-fix-boot.log`で、長押しやbond削除なしの自動再接続、
`connected travelers-split / remembered 1/1`、`ble-keyboard ready=yes`を確認した。
最終Writer・ボタン試験は`system-key-writer-final.log`。初回Writer試験ではKEYによるsuspend後、
2回目の短押しによるresumeは記録されず、BLE入力で復帰していた。0.8秒短押しの再試験で
KEY resumeログ・Suspend inactive・同じWriter session・文章保持・追加入力が揃った。

タッチ証跡: `.research/hardware/system-key-touch-final.log`。指定順に各位置を4秒保持した結果:

| ユーザーが触れた位置 | 実測raw/value (x,y) | 既存270°変換 `(799-y,x)` の計算結果 |
| --- | --- | --- |
| 左上 | (53,735) | (64,53) |
| 右上 | (73,64) | (735,73) |
| 左下 | (404,715) | (84,404) |
| 右下 | (401,68) | (731,401) |

各位置でpressとreleaseを観測。表の変換後座標は実測rawからの計算であり、
GUIアプリのhit-test出力を計測したものではない。Writer上で試験したため文章を変更しない。
最終状態は`system-key-final-status.log`: BLE connected / remembered 1/1、power performance /
Suspend inactive、Writer session 1 active、buttons key=2、BLE key=46、touch pointer=8。
診断用`log sink cdc`をoffへ戻し、Writerの編集内容とBLE接続を残して終了した。

### 未確認・制約

- GUIアイコンのタッチ選択、Escでの未保存確認ダイアログは今回の実機試験では未実施。手順は上記。
- `power key sleep`による明示light sleep/wake、bringup profileの実機書き込みは未実施。
  bringupはbuildと同一KEY semanticsのmanifest回帰テストまで。
- pairing中の再長押しはコード確認まで。接続済みbondを失うため、最終状態では再試験しない。
- 長時間のBLE安定性と消費電力は未測定。部分更新などScope外の機能は追加していない。
- 全画面更新・Writer描画中の短いボタン操作の取りこぼしと、USBの自動sleep後再接続制約は残る。
