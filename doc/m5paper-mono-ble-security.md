# PaperMono BLE security：stable Writer比較

## Task validity review（実装前）

参照はユーザー指定の`E:/ESP32/M5Paper_Writerdeck`、HEAD
`86877a881a89b4867c561899f42bdfecd973af1b`。`m5stack-papermono` envの
`main_papermono.cpp → WriterDeckPaperApp.cpp → WriterDeckBleKeyboardHost.cpp / WriterDeckBleUtils.cpp`
を追跡した。既定envは別機種だがPaperMonoのsrc filterを明示的に選んで比較する。
working treeにはWriterDeckBleUtils.h等の既存変更があるため、HEADだけでなく実ファイルを対象とする。
PaperMono libraryは実際に解決済みのNimBLE-Arduino 1.4.3。

同じZMKで安定するというユーザー観察は比較価値があるが、参照の全経路が
「bondを絶対に消さない」わけではない。boot eraseは無効で通常はstored bondを使う一方、
security failureではstale bondを削除するfallbackもある。
これをSolarOSへ移植せず、正常bond再利用のsecurity順序を比較する。
参照firmwareを現在のSolarOS実機へそのまま書くとpartition/bond namespaceが異なり、
同一bond条件の比較を壊すため、まずソースとSolarOS限定traceを使う。

### 実装前に確認した重要差分

- 参照も`startSecurity → ble_gap_security_initiate`を明示的に呼ぶ。peer Security Request待ちだけの実装ではない。
- PaperMonoはpre-security HID discoveryを無効化。接続→security→HIDという因果順序はSolarOSと同じ。
- NimBLEClientのCONNECT callbackはMTU exchangeを開始し、呼出側はconnect完了後にsecurityを開始する。SolarOSはCONNECT callback内でsecurityを開始し、MTU/GATTは暗号化完了後。
- 参照のreadyは`conn_info.isEncrypted()`または認証callback成功をpollingする。SolarOSはENC_CHANGEに依存する。
- 参照はNoInputOutput / bonding on / MITM off / SC on / ENC-only key distribution。SolarOSはDisplayOnly / bonding on / MITM on / SC on / ENC+ID。既存bondが成立している今回の問題を、設定差だけで説明したことにはしない。
- ESP-IDF 5.5.5の`ble_sm_enc_initiate`は既存procを見つけるとEALREADYを設定し、`ble_sm_process_result`へ渡す。同関数は同じconnのprocを再検索し、非zero app_statusでremove/freeする。旧参照NimBLEにも同形の経路があるため、単純なSDK世代差と断定しない。

### 初期仮説（修正前、traceで検証予定）

| 順位 | 仮説 | 支持する証拠 | 反証・不足 |
|---|---|---|---|
| H1 | peerが開始したSMとCONNECT内のlocal initiateが競合し、EALREADY後処理が既存procを消す | 現SDKのsource経路、失敗時EALREADY・ENCなし・SM timeoutも見えない | proc生成元/state、実機remove原因、HCIイベント有無が未取得 |
| H2 | SDK/接続callback順序の差で暗号化イベントがCONNECTより先に進む | SolarOS成功時のearly ENC、参照のCONNECT→MTU→呼出側security | early ENC保持修正は既存で維持済み。H1との因果を要確認 |
| H3 | bond lookup / identity / metadataの不一致 | security開始時のLTK lookupはまだtrace不足 | 保存BDA/type一致、同じbondで後続成功、NVS保持確認済み |
| H4 | 旧SM procedureがdisconnectを越えて残る | EALREADYという結果は既存procを示す | SDKはdisconnect時にSM cleanupする。世代と生成元のtraceが必要 |
| H5 | IO/MITM/key distribution差 | 参照との実設定差 | 既存bond再利用の失敗をそれだけで説明できない。設定を推測で変更しない |

前フェーズのworker退役競合修正、task creation reservation、stale handle通知防止、
REPEAT_PAIRING時のbond保持、NVS erase禁止は維持する。
display、Writer、font、startup、その他周辺機能は対象外。

## 調査・実装結果

### Root causeの実機証拠

`.research/security-trace-before-device.log`のattempt 1（時刻はtrace内部のµs）：

| t（bootから秒） | event | 意味 |
|---:|---|---|
| 7.021006 | SMP RX opcode 11 | ZMK→central Security Request |
| 7.021029 | peer_sec_request authreq=9 | bonding + SC、MITMなし |
| 7.021098 | peer_lookup rc=0 metadata=135169 | LTK present、key size16、authenticated=0、SC=1 |
| 7.021150 | sm_insert gen=1 state=6 flags=1 | peer要求でENC_RESTORE procedure生成、central initiator |
| 7.021178–7.021407 | hci_encrypt_tx/status rc=0 | stored LTKによるHCI LE Start Encryption受理 |
| 7.121140 | local_sec_request | SolarOSのCONNECT callbackがsecurityを明示開始 |
| 7.121206 | local_lookup rc=0 metadata=135169 | 同じidentityで有効LTKを検索できている |
| 7.121222 | enc_initiate state=6 | 進行中の同じprocを発見 |
| 7.121236 | sm_result app_status=2 enc_cb=0 | EALREADYを既存procの処理結果へ渡す |
| 7.121243 | sm_remove gen=1 state=6 | 完了前のprocを削除 |
| 7.320568 | hci_encrypt_rx status=0 encrypted=1 proc=0 | controllerでは暗号化成功。しかしSMの所有者がない |
| 7.320599 | sm_result enc_cb=1 proc=0 | process_resultがproc=NULLでbreakし、GAP ENC_CHANGEを発行しない |
| 67.121 | HID security timeout | SolarOSは欠落したcallbackを60秒待つ |

attempt 2でも同じ削除→HCI成功→GAP通知なし→timeoutを再現。
attempt 3でも同じ削除とHCI成功を観測（capture終了時点ではdeadline前）。
trace lost/foreign producerは記録されていない。
ログentryの表示時刻11.141s等はmain taskがtraceをdrainした時刻であり、因果順序には`t=`を使う。

原因は、peer Security Requestとlocal encryption initiateの重なりに対して、
NimBLEの`ble_sm_enc_initiate`がEALREADYを`ble_sm_process_result`へ渡し、
同じconnの既存SM procedureを削除してしまうこと。
後続HCI成功は受信・link状態更新されるが、`process_result`のproc=NULL早期breakで
ENC_CHANGE callbackとbond restored metadataが失われる。
従って「HCI暗号化が進まない」「LTKがない」という前フェーズの未確定候補は、今回の失敗では否定された。

### 最小修正と直接回帰test

`scripts/patch_nimble_security.py`はESP-IDF 5.5.5のsource SHA256を検証し、
build-localの`ble_sm.c`へ既存proc発見時のunlock＋EALREADY returnを生成する。
installed SDK、設定、key/bond、scan/backoff/deadlineは変更しない。
EALREADYを成功に置換せず、既存procedureの本来のHCI完了とENC_CHANGEを待つ。
`patches/nimble/overlay.cmake`が当該projectのbt targetの1ソースだけを置換する。

`scripts/test_nimble_security.py`は実SDKの`ble_sm_enc_initiate`、`ble_sm_process_result`、
`ble_sm_enc_event_rx`を抜き出してnative compilerで実行し、controller I/O・pool等をshimする。
修正前：`HCI encrypted=1 bonded=0 ENC_CHANGE callbacks=0`でassert FAIL。
修正後：`HCI encrypted=1 bonded=1 ENC_CHANGE callbacks=1`、既存PAIR/KEY_EXCH procの保持、
新規encryption開始、allocation failure後の再試行もPASS。
証拠は`.research/security-before/sdk-failure-sequence.txt`、
`.research/security-fixed-sdk-failure-sequence.txt`、`.research/security-fixed-sdk-regression.txt`。
このtestはSDK source自身を実行するが、実radioの反復acceptanceは別途必要。

### 参照との比較上の注意

参照旧NimBLEにも同型のEALREADY削除経路がある。
NimBLEClient CONNECT callbackはMTU exchangeを始め、connect呼出はMTU event通知を待って戻る。
Writerのsecurity開始はその後で、暗号化状態もpollingする。
SolarOSはCONNECT callback内で即時開始し、ENC_CHANGEで次段階へ進む。
この順序差はraceへの遭遇と欠落callbackの影響を説明するが、参照実機のSM/HCI traceはまだ取得していない。
「参照で毎回raceが回避されている」はsourceとユーザーの安定観察からの推論であり、実測event系列とは区別する。
参照のMTU待ちや500ms settleをコピーする方式はtiming依存で、generic BLE clientで再発し得る。
SM所有権を正す最小修正を採用し、arbitrary delay、blind retry、bond erase、SDK upgradeは採用しない。

### 実装と設定の比較

参照はArduino 2.0.17 / ESP-IDF 4.4.7 / NimBLE-Arduino 1.4.3、SolarOSはESP-IDF
5.5.5付属NimBLE host。後者に独立したNimBLE-Arduinoのversion番号を付けない。
比較したSDKソースはhashを固定し、upgrade/downgradeしていない。
両hostともLegacy pairingとSecure Connectionsをcompile時に有効化している。
SolarOSはSC debug keys無効、NVS persist有効で、この設定も変更していない。

| 項目 | Reference Writer（PaperMono env） | SolarOS |
|---|---|---|
| BLE stack | Arduino上のNimBLE-Arduino 1.4.3 | ESP-IDF 5.5.5付属NimBLE |
| host / controller | NimBLE host / ESP32-S3 controller、Bluedroidではない | 同じ構成、SDK世代は異なる |
| central init | `keyboardHost.begin`→NimBLEDevice init、PUBLIC own address | NVS/peer復元→nimble_port_init→host task/sync、PUBLIC own address |
| scan | active、interval100ms/window80ms、boot async5秒、bond repair rescan8秒 | active、interval80/window48（0.625ms単位、50/30ms）、最大8秒、一致時早期停止 |
| bond store | NimBLE store/NVS、通常boot消去off。ただしstale repair分岐には削除あり | NimBLE store/NVS、REPEAT_PAIRINGはIGNORE、NVS erase fallback禁止 |
| remembered peer | Preferences `ble_addr`文字列と`ble_name`、scan結果のaddressへ照合 | `blekbd/peers`のBDA/type/name、単一remembered keyboard |
| reconnect開始条件 | boot scan完了後saved address選択。切断callback後main loopが再scan/connect | boot/sync・disconnect/resumeがbackground workerを予約、Shellとは独立 |
| GAP connect API | `NimBLEClient::connect(device, true)`、3秒 | `ble_gap_connect(PUBLIC, address, 3000, callback, epoch)` |
| CONNECT callback timing | callbackがMTU exchange開始、呼出側はMTU通知を待ってconnectから復帰 | CONNECT callback内でsecurity開始。SDKのremote-feature待ち中にearly ENCも起こり得る |
| encryption initiation | connect復帰後`ensureSecureBond`→`startAndWaitForBleSecurity` | early ENC未受信ならCONNECT内で`ble_gap_security_initiate` |
| security開始主体 | central明示要求。peer requestもNimBLEが処理する | central明示要求＋ZMK peer request。本失敗ではpeerが先に開始した |
| initiate相当API | NimBLEDevice::startSecurity→ble_gap_security_initiate | ble_gap_security_initiate直接 |
| peer initiated security | hostがSecurity Requestを受信しstored LTKでENC_RESTORE | 同じ。修正前traceでpeer request→proc生成→HCI開始を確認 |
| already encrypting | rc0/EALREADYなら実encrypted状態/auth callbackを6秒まで待つ。旧hostにもproc削除bugあり | EALREADYでも60秒deadline内に実ENC_CHANGEを待つ。今回hostのproc保持を修正 |
| encrypted接続検出 | connInfo.isEncryptedまたはsecurity/auth callbackを100ms周期で確認 | ENC_CHANGE成功＋conn descriptor encrypted/bonded、early ENCはCONNECTまで保持 |
| bond lookup | hostがpeer identityの`ble_store_read_peer_sec`、LTK有→ENC_RESTORE | 同じAPI/分岐。今回peer/local両lookup成功、同じmetadata |
| identity address | PUBLIC local、remoteはNimBLEAddress/device、store identityに解決 | PUBLIC local、saved remote BDA/typeとconn descriptor identity、random peer type=1 |
| ENC_CHANGE | NimBLEClientがsecurity callbackへ変換 | HID GAP callback→encrypted_link_ready→MTU/discovery |
| AUTH_COMPLETE等 | onAuthenticationCompleteがsecurityComplete/encryptedを更新 | 独立したArduino callbackはなくGAP ENC_CHANGEを使う |
| GATT開始条件 | PaperMonoはpre-security discovery=false、security ready後500ms settle | 実ENC成功、CONNECT確定後にMTU→service/characteristic/descriptor/report map |
| disconnect cleanup | client切断・main側cleanup、押下解除、cache/client再作成 | epochでstale callback拒否、GATT/cancel完了後slot release、押下解除、worker再予約 |
| reconnect retry | connect最大3回/700ms、security wait6秒、subscription最大8回/250ms。PaperMono独自settle設定あり | scan/open loop、initial250ms、backoff1→最大5秒。今回いずれも変更なし |
| reconnect後subscription | service/report再取得、CCCD write without response、ready確認後入力 | 毎接続report再取得・CCCD登録、全完了でinput source ready |
| security config | NoInputOutput / bonding on / MITM off / SC on、PaperMono ENC-only init/resp | DisplayOnly / bonding on / MITM on / SC on、ENC+ID init/resp。変更なし |

参照の`WriterDeckPaperApp.cpp`がHostのdefault timingsを上書きする点にも注意した。
PaperMonoではclientDeleteDelay=1500ms、postSubscribeVerify=1000ms、postSecuritySettle=500ms、
postBondAdvertiseSettle=3000ms。これらは参照側のcache/CCCD/bond修復順序に付随する待ちであり、
今回証明したSM所有権の問題を解決するものとしてSolarOSへコピーしていない。

参照ファイルのSHA256（dirty treeの実ファイル）:

| ファイル | SHA256 |
|---|---|
| WriterDeckBleKeyboardHost.cpp | `3f550226b58ab0ffaa49aedc52c7e5191fb787ed378965cba754d0f33cd7bd55` |
| WriterDeckBleUtils.cpp | `d545bcfae13aa1e0deb2b96b36b7698da6084662d69ca81f142b4275c57776a5` |
| WriterDeckPaperApp.cpp | `40525be3fea718fedc6446ec8b10ecfc84e2d3864efb8de93c346eb0cd98c94d` |

### 3-way event sequenceと修正後実測

| 系列 | causal sequence | 証拠の範囲 |
|---|---|---|
| Reference success | saved address scan→connect→CONNECT/MTU→connect復帰→startSecurity→encrypted/auth callbackをpoll→settle→GATT/CCCD→HID ready | sourceとユーザーの安定動作報告。SMP/HCI実機時刻は未取得。peer requestとの厳密な相対順序は断定しない |
| SolarOS以前のsuccess | peer側security→early ENC_CHANGE成功→CONNECT→early state reconcile→GATT→HID ready | 前フェーズで採取済み。CONNECTが先の場合も実ENC成功を待つ設計 |
| SolarOS修正前failure | peer Security Request→ENC_RESTORE生成/HCI開始→CONNECT→local initiate/EALREADY→同proc削除→HCI成功/procなし→ENC_CHANGEなし→60秒timeout→cleanup/retry | 本フェーズbefore traceで2回のtimeout、3回目も削除/HCI成功を確認 |
| SolarOS修正後success | peer Security Request→ENC_RESTORE/HCI開始→CONNECT→local initiate/EALREADY/proc保持→HCI成功→正常proc完了→ENC_CHANGE→encrypted/bonded→GATT→HID ready | 本フェーズafter traceと通常版ログで確認。bond操作なし |

修正後trace `.research/security-trace-fixed-device.log`、同じconn=1 / SM generation=1:

| bootから秒 | event |
|---:|---|
| 6.994018 / 6.994040 | SMP RX opcode11 / peer Security Request authreq9 |
| 6.994111 | peer LTK lookup成功、metadata135169（present/keysize16/SC、authenticated0） |
| 6.994160 | ENC_RESTORE procedure生成、initiator |
| 6.994189 / 6.994448 | HCI LE Start Encryption送信 / controller受理rc0 |
| 7.094208–7.094304 | CONNECT後local security、同LTK lookup→EALREADY、procは保持 |
| 7.293577 | HCI Encryption Change成功、encrypted1、同procが存在 |
| 7.293608 / 7.293616 | SM正常完了stateNONE→ここでproc削除 |
| 7.293654 | GAP ENC_CHANGE発行status0、restored1/bonded1 |
| 7.293 | HID descriptor encrypted1/bonded1 |
| 9.843–9.844 | reports ready→input source ready、attempt1成功 |

ユーザーがこの診断版からZMKの`version`実行に成功した。
通常版 `.research/security-normal-device.log` でもattempt1のCONNECT=7.093s、
EALREADY=7.093s（encrypted0/bonded0）→ENC_CHANGE=7.293s→encrypted1/bonded1→
HID ready=9.793sとなった。**EALREADYだけでreadyに進めてはいない。**

HCI成功時にSM procが失われていることと、欠落callbackの因果関係がroot cause。
「前attemptのstale proc」「key lookup失敗」「HCI未完了」ではない。
既存procを生成した主体は今attemptのZMK Security Request、暗号化をcontrollerへ要求する主体はcentral host。
この問題はNimBLE hostの重複initiate処理のbugで、SolarOSの開始タイミングがそれを露出させた。
参照の旧NimBLEにも同形の処理があるため、SDK version差だけを原因にはしない。

### traceの範囲・bond metadata

診断版のみ512 entryのPSRAM ringを使用する。host側hookはログmutexを取らず、
main pollが最大16 entryずつdrainする。単一producerを検証しoverflow/foreign producerを警告する。
eventにはµs timestamp / conn handle / SM generation / proc identity / state / resultを付け、
既存HID traceのepochとconn/timeで照合する。SMPはRX/TX方向とopcodeだけ、
LTKは存在/key size/authenticated/SCだけ記録し、LTK/IRK/EDIV/RAND等のbytesやpacket payloadは出力しない。
今回の再接続は既存LTKによる暗号化復帰で、Pairing Request交換を必要としなかった。
disconnect reason、deadline/slot release、worker retryは既存HID/keyboard traceで補完する。

SolarOS before/afterのlookupは双方LTK present1/key size16/authenticated0/SC1、peer identity type1で一致。
referenceの実bond metadataは別環境なので未取得。同じZMKでも両hostのLTK値が同じとは仮定しない。
referenceへfirmwareを書いて現在のnamespace/profile条件を壊す比較は実施していない。

通常版は`SOLAR_OS_BLE_SECURITY_TRACE=0`。collector source・SDK hooks・record symbolは含まれない。
診断する場合のみENVを1にしてCMakeを再configureする。ENV変更はcacheが残るため再configureが必要。
通常版のbt source置換はSMの1ファイル、既存dynamic-service overlayはそのまま。
installed `ble_sm.c` hashは `384e25aae3ea1f623aef7a6f10cefe5ed7bbeb972f2ee163a6c6ed3ba2e01a7e` のまま。
SDKCONFIGもbeforeとbyte一致（SHA256 `f79b32c8c8a228b8dbfc0069712120b5e9984684dbacfc255136eb80ecd5ca00`）。

### 仮説の最終評価

| 順位 | hypothesis | supports | contradicts / missing evidence | 結論 |
|---|---|---|---|---|
| 1 | H1 重複initiateでactive procを削除 | source、beforeの同gen削除、HCI後callback欠落、実SDKtestのFAIL、afterのproc保持/callback成功 | 再接続反復の最終acceptanceは別に必要 | この失敗のroot causeを確認 |
| 2 | H2 CONNECT/ENC順序がraceを露出 | peerが先にSM生成、localがHCI完了前に重複、以前のearly ENC成功 | 参照実機の厳密なSMP時刻は未取得 | triggerの説明。arbitrary delayでは修正しない |
| 3 | H5 security設定差 | IO/MITM/key distributionに実差分あり | 同LTK/HCI成功、設定変更なしでcallback復旧 | 今回の直接原因を支持しない |
| 4 | H4 前attemptのstale proc | EALREADY一般にはその可能性あり | traceで今attemptのpeer requestから新gen生成を確認 | 今回の失敗では否定 |
| 5 | H3 lookup/identity不一致 | 理論上bond restore失敗の候補 | peer/local lookup成功、HCI成功、metadata一致、同bondで修正後成功 | 今回の失敗では否定 |

### code / host regression

Production変更はbuild-local `ble_sm_enc_initiate`の既存proc分岐だけ。
`scripts/patch_nimble_security.py`とCMake overlayはSDK source hashを検証し、未知versionで黙って適用しない。
任意delay、deadline短縮、scan延長、bond削除、SDK交換は行っていない。
限定診断は`trace_nimble_security.py`と`solar_os_ble_security_trace.c/.h`、keyboard init/pollのhookのみ。
前フェーズのworker retirement race・creation reservation・stale handle通知防止・early ENC処理を保持した。

| 検証 | 結果 | evidence |
|---|---|---|
| 実SDK失敗系列（stock） | EXPECTED FAIL: encrypted1、bonded0、ENC callback0 | `.research/security-before/sdk-failure-sequence.txt` |
| 同系列（fixed） | PASS: encrypted1、bonded1、callback1 | `.research/security-fixed-sdk-failure-sequence.txt` |
| 実SDK関数回帰 | PASS: ENC_RESTORE/PAIR/KEY_EXCH保持、新規開始、OOM後回復 | `.research/security-fixed-sdk-regression.txt` |
| overlay Python tests | 5件PASS（WSL/gcc、native test含む） | `.research/security-overlay-tests.log` |
| SDK config guard | 6件PASS | `.research/security-sdkconfig-tests.log` |
| BLE host suite | worker/HID/adapter/scan policy/service/multi-peer/server/power、retirement race PASS | `.research/security-host-regression.log` |
| BLE Lua binding / HID report | PASS | `.research/security-host-extra-results.log` |
| BLE Shell TUI | 10件PASS | `.research/security-ble-tui-tests.log` |
| 診断版 / 通常版build | PASS | `.research/security-trace-fixed-build.log` / `.research/security-normal-final-build.log` |
| 通常版app-only flash | 0x10000、書込hash verified | `.research/security-normal-upload.log` |

実SDK3関数をそのまま抽出してnative fixtureへ入れるため、EALREADY成功とENCを手動で連結するだけのtestではない。
stockでは実HCI handlerまで実行してもcallbackが出ず、同じ操作をfixedで実行するとcallbackとbond復帰が成立する。

再現手順（WSL/Linuxでrepo rootから、ESP-IDF 5.5.5とgccが必要）:

```sh
python3 -B -m unittest discover -s tests -p 'test_nimble_security_overlay.py'
python3 scripts/test_nimble_security.py --source .pio-core/packages/framework-espidf/components/bt/host/nimble/nimble/nimble/host/src/ble_sm.c --failure-sequence
python3 scripts/test_nimble_security.py --source .pio/build/esp32_s3_devkitc1_n16r8/solar_os_nimble_security/ble_sm.c --failure-sequence
```

2行目はstockに対するEXPECTED FAIL、3行目はfixedに対するPASS。Windows nativeではgcc/Unix process制御がないため
native caseはskipし、WSLで実行した。SDKがない環境のskipを実SDK PASSとして数えない。

## 実機acceptance（通常版・USBなし・同profile/bond）

| ケース | 必要試行 | 現在の結果 |
|---|---:|---|
| A ZMK ONでPaperMono complete OFF→ON | 5 | **5/5成功**、ユーザーが各回version実行を確認 |
| B ZMK OFFで起動、Shell後10–30秒待ってZMK ON | 5 | **5/5成功**、Shell後15秒待機、version実行確認 |
| C 接続中ZMK OFF→10–30秒→ON | 5 | **5/5成功**、各回20秒不在、version実行確認 |
| D ZMK 3–5分不在→ON | 1 | **成功**、3分不在後に自動復帰、version実行、約4秒 |

各回ZMKから`version`を実行して入力を確認する。USB reset/suspendをcomplete power cycleの代用にしない。
bond/profile消去、再pairing、ZMK firmware変更は実施していない。
latencyはA=PaperMono電源ON→入力可能、B/C/D=ZMK ON→入力可能を分けて集計する。
ユーザー観察は概算秒として扱い、USB traceのadvertisement→HID readyと混同しない。
通常版のUSB log実測1回はadvertisement6.797s→HID ready9.793s=2.996s。
ユーザー観察BのZMK ON→入力可能は `[4, 3, 5, 4, 4]` 秒、min3 / median4 / max5秒。
Aは5回全成功の報告のみで個別時刻は未取得。BのlatencyをAへ転用しない。
ユーザー観察Cは `[3, 2, 2, 3, 3]` 秒、min2 / median3 / max3秒。
A/B/Cの計15回に失敗・追加電源操作・bond操作はなかった。
ユーザー観察Dは約4秒。B/Cの10試行を合わせるとmin2 / median3.5 / max5秒。
観察Dも含む11回ではmin2 / median4 / max5秒。すべて概算で、画面で入力可能と判断した時刻を含む。
電源ON時刻の記録がないAについてmin/median/maxの数値は作らない。
前フェーズのsuccessful attemptは `.research/reconnect-diag-security-ring.log` の
attempt3、ENC_CHANGE136.631s→CONNECT136.731s→encrypted1/bonded1→ready139.131s。
`.research/reconnect-security-sdk.log` にも同じearly ENC順序のattempt1成功がある。

## データ保持・サイズ・artifact

app以外をflashしていない。startup fastest・JP・元のINFO/CDC sink offは実機反復後のUSB queryで確認した
（`.research/security-final-settings.log`）。FAT全ファイル/全NVS論理値の最終比較も完了した。
bootごとのWireGuard TAI reservationとShell historyは正常に進むのでraw NVS/FAT全体のbyte一致を要求しない。
bond/remembered peer、文書/checkpoint、zoom/font/power設定には変更を許容しない。

| 保持検証 | 結果 | evidence |
|---|---|---|
| FAT全38ファイル | file set一致、変更は`/.shell/HISTORY`だけ。他37ファイルはbyte一致 | `.research/security-fat-preservation-result.log` |
| 全NVS論理値 | 変更は`wireguard/tai_ceiling`だけ、14回分の正常なboot reservation進行 | `.research/security-nvs-preservation-result.log` |
| remembered peer / NimBLE bond | `blekbd/peers`と`nimble_bond`の5キーすべてbyte一致 | 同NVS比較。鍵bytesは出力していない |
| JP / UI font / power等保存設定 | `input`・`writerdeck`・`papertype`・`M5GFX`含むNVS値一致 | 同NVS比較とUSB query |
| 元文書 / recovery | `/transport-test.md`173Bと未保存recovery200Bをreadback、一致、zoom4保持 | `.research/security-preservation.log` |
| startup | 指定2行byte一致、起動fastest保持 | FAT比較と`.research/security-final-settings.log` |
| snapshot取得後の通常起動 | Shell自動起動、既存peerに自動接続、10.143sでopen成功 | `.research/security-final-boot.log` |

元文書SHA256は `a82889b15c8d7078570a9de577afc29f9a927742c8bdb6c46b1621073e78eb69`。
FAT rawはbefore `1a043d9af851d70443c4c5ca02c966eb9853b3ab5109e52d7d7271750cd0a703`、
after `33a06d0814d3ae9641243bdbf4de0a72f71fe949a439fee5c95eec551d71f186`。
wear leveling/historyのためraw hashは異なるが、全ファイルをSDK FAT parserで抽出して比較した。
旧NVS snapshotを書き戻して単調時刻予約を戻す操作は行っていない。

| 指標 | このフェーズ直前artifact | 通常版 | delta |
|---|---:|---:|---:|
| static RAM | 51,884 B | 52,480 B | +596 B |
| flash使用量 | 3,319,815 B | 3,322,931 B | +3,116 B |
| firmware.bin | 3,325,600 B | 3,328,704 B | +3,104 B |

上記は各artifactのbuild report差で、SDK関数の数行だけに全deltaを帰属させない。
通常版にはdiagnostic collectorのring/hookが存在しないことをcompile sourcesとELF symbolsで確認した。

通常版: `build/papermono-ble-security/firmware.bin`（ELFも同directoryに保存）

SHA256: `c9e94e8a0caabd5a9711034102965a27067cc99daf2d8c57fa686481fbf48733`

これは実機へ書いたbinと最終CMake依存修正後のbinで同一。
`trace-before.bin`、`trace-fixed.bin`、`trace-recorder-final.bin`は限定診断用で通常配布物ではない。

## 未取得事項・制限

USBなしA/B/C各5/5、D自動復帰、実SDK/host回帰、通常版flash、FAT/NVS保持比較を完了した。
Reference実機のSMP/HCI traceとbond metadataは未取得で、sourceからの推論と実測を区別する。
参考実装の厳密なradio event時刻とAの個別latencyは未取得。B/C/Dのlatencyはユーザーの概算観察である。
今回修正が全BLE機器の任意のsecurity failureを解決するとは主張しない。
build-local patchはESP-IDF 5.5.5のsource hashに依存し、将来SDK更新時はreview/testが必要。

## 最終報告31項目の対応

| No. | 項目 | 結果・参照 |
|---:|---|---|
| 1 | Task validity | 実装前review、通常bond再利用経路を比較。参照に例外的bond削除fallbackがあることも確認 |
| 2 | Reference repository/revision | `E:/ESP32/M5Paper_Writerdeck`、HEADとdirty実ファイルhashを記録 |
| 3 | Reference stack/SDK | NimBLE-Arduino1.4.3、Arduino2.0.17/IDF4.4.7 |
| 4 | SolarOS stack/SDK | IDF5.5.5付属NimBLE、installed sourceとSDKCONFIG未変更 |
| 5 | Reference architecture | async scan→saved address→NimBLEClient connect/MTU→明示security/poll→CCCD |
| 6 | SolarOS architecture | background reconnect worker、単一remembered peer、async Shell/generic BLE/suspend統合を保持 |
| 7 | Reference success sequence | 上の3-way表。source-derived、runtime SMP/HCIは未取得 |
| 8 | SolarOS success sequence | 以前のearly ENC系列と修正後EALREADY/proc保持系列を実ログで提示 |
| 9 | SolarOS failure sequence | peer request→重複initiate→proc削除→HCI成功→通知欠落→timeout |
| 10 | 最重要差分 | CONNECT内即時initiate vs connect/MTU復帰後initiate、callback依存 vs state polling |
| 11 | Security ownership | 今attemptのpeer requestがSMを生成、central hostがHCI暗号化開始。後続local initiateが重複 |
| 12 | EALREADY | 既存procありの返値。暗号化成功ではない |
| 13 | ENC_CHANGE欠落 | result processorがprocを再検索して削除し、後のprocNULL経路がcallback前にbreak |
| 14 | SMP trace | RX opcode11/authreq9、方向/opcodeのみ、鍵payloadなし |
| 15 | HCI trace | Start Encryption受理とEncryption Change成功をbefore/after双方で確認 |
| 16 | bond state比較 | SolarOS LTK metadata一致、NVS bond全5キー保持。reference metadataは未取得 |
| 17 | root cause | NimBLEの重複encryption initiateで既存SM所有権を破壊するbug |
| 18 | alternative hypotheses | 事前順位と最終支持/反証/不足を表に記録 |
| 19 | code change | hash-guarded build-local SM overlay、optional bounded trace。通常版は最小fixだけ |
| 20 | state machine before/after | 早期proc削除→通知欠落から、実HCI完了後proc終了→正規通知へ |
| 21 | host regression | BLE worker/HID/adapter/scan/service/multi-peer/server/Lua/report/power/TUI PASS |
| 22 | root-specific test | 実SDK3関数、stock FAIL / fixed PASS、HCI成功なのに通知がない状態を再現 |
| 23 | already ON | USBなしcomplete power cycle、5/5、各回version成功 |
| 24 | later ON | USBなしShell後15秒でkeyboard ON、5/5 |
| 25 | OFF→ON | USBなし20秒不在を5回、5/5 |
| 26 | long absence | USBなし3分、約4秒で復帰、version成功 |
| 27 | latency | B3/4/5秒、C2/3/3秒（min/median/max）。A個別時刻未取得 |
| 28 | data/bond preservation | FAT38ファイル、全NVS論理値比較、元文書/recovery readback、JP/z4等保持 |
| 29 | RAM/flash delta | 前artifact比+596B/+3116B、通常traceなし |
| 30 | firmware/SHA256 | 上記通常版bin/ELF、SHA256と実機flash同一性を記録 |
| 31 | unresolved | reference runtime trace/metadataとA個別latency未取得、SDK hash更新時review必要 |
