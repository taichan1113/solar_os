# PaperMono BLE keyboard reconnect reliability

次フェーズのsecurity root cause・SM/HCI実測・最小SDK overlay修正は
[m5paper-mono-ble-security.md](m5paper-mono-ble-security.md)を参照。
以下は前フェーズ時点の記録であり、原因未特定という記述はその時点の調査結果。

## Task validity review（実装前、2026-10-07）

対象は現行SolarOSの1台のremembered keyboard（travelers-split）の再接続経路。
ユーザー報告は、software reboot/cold power-on後に接続しない場合があり、
bond消去や再pairingなしのキーボードOFF/ONで復旧するというもの。
現行版の失敗時ログは不足しており、この症状のroot causeは未特定。
fastestとの因果関係を示す証拠はない。診断・host fault injectionを先行し、
症状の原因と独立したコード上の問題を区別して扱う。

### 現行architecture

- NVS blekbdのpeers（legacy peerも読込）から有効な1台を復元する。
  BLE bondはNimBLE storeの別データ。input initがJP等の共有layoutを復元する。
- boot policyがenabledならbackground peripheral initでNimBLEを起動する。
  host syncは最大5秒待つが、peer接続待ちはShell起動をblockしない。
- remembered peerがあればschedule_reconnect(0)がworkerを作る。
  workerはscan_taskやSCANNING/CONNECTING/PASSKEYとの競合を避ける。
- scanはactive、interval=80/window=48（50ms/30ms）、duration=8秒、
  duplicate filteringなし。見つからなければbackoffして再scanする。
- reconnect候補は保存BDAの全6 bytes一致、connectable legacy広告event 0/1。
  name fallbackはdiscovery表示には使うが、reconnect選択には使わない。
  reconnectはaddress typeを別途比較しない。候補をfreezeしてscan cancel後に接続する。
- SDKのble_gap_disc_desc.addrには広告reportのaddress/typeが渡る。
  controllerによるidentity解決やRPAの実際の影響は失敗時ログで確認が必要。
  HID encryption後にはpeer_id_addrを読み、identityのBDA/typeを保存する。
- HID層はsingle slotをepochで保護し、NimBLE host taskがGAP/GATTを所有する。
  connect requestは3秒、hostのconnect deadlineは4秒。
  CONNECT後はsecurityを開始し最大60秒待つ。GATT各段階は10秒、
  open呼出し全体は90秒の上限。OPEN/INPUT/CLOSEは別event workerがpolicyへ渡す。
- early ENC_CHANGEは実handleと結果を保持し、CONNECT後にGATTへ進む。
  既存修正とhostテストを維持する。GATTはMTU→services→characteristics→
  report map/reference→CCC subscription→HID ready。
- policyのOPEN successでconnectedとble-keyboard input source readyを立てる。
  CLOSEで押下状態とreadyを解放し、通常時は250ms後のreconnectを予約する。
- reconnectは最大5秒のbackoff。現行コードは初期値1秒を待つ前に倍化するため、
  実際の失敗後の待ちは2→4→5秒。まずこの実装値のまま評価する。
- stopはtaskを強制削除せず、notifyとscan/open cancellationで所有権の返却を待つ。
  sleep/pairing/forget/generic GATT clientの抑制flagがあり、resumeはhostを再初期化する。
  cold boot/software rebootは静的状態の再初期化、resumeは同じRAMの状態を引き継ぐ。

### 既存証拠と未特定事項

過去のsystem-key診断では、同じca:15:95:b4:20:02/randomを捕捉した後のsecurity待ち失敗と、
候補なしのscanが複数回継続した例がある。ただしearly ENC_CHANGE修正前の記録を
現在の症状の原因とみなさない。現行terminal版では自動接続成功例があり、
失敗をユーザーが報告した時にはUSB読取り開始までに既に復旧していた。
scan miss、connect失敗、security/HID失敗、worker stuck、ZMK advertising timingを
区別できる追加ログが必要。

レビューでは以下も見つかった。現時点では今回の実機失敗の原因と断定しない。

- worker終了直前のdisconnectと再予約、およびworker作成の競合をhostで評価する必要がある。
- HIDのREPEAT_PAIRING処理が自動でstored peer keysを削除する経路を持つ。
  通常reconnectでbondを自動消去しない今回の明示要件と整合させる必要がある。
- keyboard側init_nvsにNO_FREE_PAGES/NEW_VERSION_FOUND時の全NVS erase fallbackがある。
  今回のデータ保持要件のため、BLE側からの自動全消去は許容しない。

診断はdebug levelを使い、通常info使用時に広告ごとの大量ログを出さない。
scan/backoff/name matchingを推測で変更せず、bondを保持した状態で再現を試みる。
display/startup/fonts/Writer/touch/RTC/battery/LEDは変更しない。

## 実装・検証結果（最終報告 3–26）

**状態：失敗phaseの特定と独立したworker競合の修正まで。再接続の主症状は未解決で、最終acceptanceは未達。**
検出、GAP接続、security、GATT、入力readyを区別できるログを追加した。
実機の失敗は「対象を検出してGAP接続した後、暗号化完了を待って60秒timeout」に絞れた。
なぜNimBLEのsecurity procedureが完了しないかはまだ特定できていない。
以下のworker修正を、この実機症状のroot causeを解決したものとは扱わない。

### 再現・phase・根拠（3–6）

ログの時刻は各bootのSolarOSログ時刻。ZMK ONの時刻とは異なる。

| 試験 | 観察・証拠 |
|---|---|
| 変更前terminal firmware | `.research/reconnect-baseline-status.log`。同じ保存BDAを検出し、open開始12.991s→失敗74.160s、77.961s→失敗130.005s。3回目134.106s→OPEN136.670sで成功。原版でも失敗後のretryは継続する。 |
| ZMK OFFで約3分放置→ON | `.research/reconnect-diag-late-on-debug.log`。不在中14回のscan完了は候補0、attempt 15で192.010sに対象広告を検出。ユーザーはON後90秒待っても入力不可と報告。 |
| その失敗の内部状態 | BDA `ca:15:95:b4:20:02`、type=1、name=travelers-splitが保存値と一致。192.038sにopen、192.272sにGAP CONNECT成功。192.273sにsecurity request rc=2、encrypted=0、bonded=0。その後ENC_CHANGEがなく60秒timeout。GATT開始前に失敗する。 |
| diagnostic版の再起動後 | `.research/reconnect-diag-security-ring.log`。attempt 1/2はsecurity rc=2→60秒timeout→slot release→2/4秒backoff。attempt 3ではENC_CHANGEがCONNECTより先に成功し、139.131sにinput ready。bond消去・再pairingを行わず復旧した。 |
| SDKログ設定の比較 | `.research/reconnect-security-sdk.log`。1回目がENC_CHANGE 7.676s→CONNECT 7.776s→HID ready 10.126sで成功。ユーザーがZMKからversion実行を確認。ただしSDKのNimBLE DEBUG設定だけでは全体のINFOログ上限を越えられず、詳細なSMP/HCIログは取得できなかった。 |
| 通常SDK設定の最終版 | `.research/reconnect-final-device.log`。attempt 1はsecurity開始7.527s→timeout67.527s→slot release67.598s→2秒backoff。attempt 2も69.927s→129.927s→slot release130.012s→4秒backoff。attempt 3は134.327sに同じsecurity待ちへ入り、後続の設定復帰時ログでもretryの継続を確認。panicなしでcleanup・再試行するが、主症状は未解決。 |

`rc=2`はSDKの`BLE_HS_EALREADY`。SDKの`ble_gap_security_initiate` / SM開始処理では、
既存のsecurity procedureがある場合に返り得る。この値そのものは暗号化成功ではない。
`bonded=0`はその時点のconnection descriptorの状態であり、NVS内のbond消失を意味しない。
失敗時にもremembered 1/1を維持し、成功時は同じidentityのencrypted=1/bonded=1になる。
失敗したprocedureの生成元、SMP要求・応答の順序、双方のHCI結果が未取得なので、
SolarOS・ESP-IDF/NimBLE・ZMKのどこが主因かを断定しない。

最初の診断版には、追加したslot-releaseログをspinlock中に呼ぶ不具合があった。
`.research/reconnect-diag-late-on-debug.log`の253.284sでpanicし、backtraceは
`command → solar_os_log_write → log_unlock → xQueueGenericSend`だった。
mutexを使うログをcritical sectionの外へ移した。これは今回のinstrumentationが
導入した問題であり、変更前からのBLE症状の原因とはしない。

### 変更コード・state machine（7–8）

- `src/services/solar_os_ble_keyboard.c`：attempt番号、保存peer、bounded広告trace、scan完了、候補、open結果、input readyのdebugログ。task作成中をstop/sleep判定に含める。通知はhandle退役と同じlock内で行い、削除済みhandleへの通知を防ぐ。
- `src/services/solar_os_ble_keyboard_reconnect.inc`：本番workerを内部includeへ分離し、その同じコードをhost testで実行。task作成の予約をlock内で行い、同時scheduleによる二重生成を防ぐ。退役後に再接続の必要性を再確認する。
- `src/services/solar_os_ble_hid.c`：HID epoch/conn/stage、security/ENC、GATT、disconnect理由、slot releaseをdebugで追跡。REPEAT_PAIRINGでは既存のIGNORE方針へ委譲し、通常reconnect中にstored peer keysを自動削除しない。
- BLE側`init_nvs`：NVS初期化エラー時に全NVSをeraseするfallbackを除去し、エラーを返して保存データを保持する。
- `tests/host/ble_keyboard_reconnect_test.c`、`ble_hid_test.c`、`ble_test_support.c`、Makefile：worker fault/schedulingテストと、bond自動削除を行わないことの確認。

修正前：open成功→worker終了を決定→その間にCLOSE callback→既存workerへ通知→workerがhandleを消して終了。
この順序では通知が消え、次のworkerが予約されない。
修正後：handle退役後にもinitialized/connected/remembered/suppressedを再評価し、必要なら250ms後の再接続を予約する。
明示stopの場合は再予約しない。task作成中は予約flagで二重作成を抑える。
旧workerを同じhost fault injectionで実行すると終了直前disconnectテストが失敗し、修正後は通る。
**この競合はhostで確認済みだが、実機の60秒security待ちの説明にはならない。**

### scan・backoff・matching・identity（9–12）

| 項目 | Before → After |
|---|---|
| Active scan | interval 80 / window 48（50/30ms）、duration 8秒、duplicate filterなし → 同じ |
| scan終了 | 対象候補をfreezeしてcancel後open → 同じ |
| 失敗後backoff | 実際の待ち2→4→最大5秒 → 同じ。1秒の初期値をwait前に倍化する現行動作を維持 |
| 通常disconnect後 | 250msでschedule → 同じ。終了競合時も再予約可能にした |
| match | 保存BDA全6byte一致＋connectable legacy event 0/1 → 同じ。nameは診断・discovery用でreconnect fallbackなし |
| address type | 保存・open時に使用、scanで独立比較しない → 同じ |
| identity | ENC後に`peer_id_addr`を保存 → 同じ |

今回の失敗広告ではBDA/typeが保存値と一致し、scan missやRPA変化は観測していない。
controller identity resolutionと異なるRPAを使う将来のケースまで正しいと保証する試験ではない。
ZMK広告は少なくとも今回のON後scanと後続retryで捕捉できた。「ON直後しか広告しない」という根拠は得られていない。

### security・HID・cleanup・sleep（13–16）

CONNECT前のENC_CHANGEを実handleと対応させる既存修正は維持した。
connect request 3秒、host connect deadline 4秒、security 60秒、GATT段階10秒、open全体90秒の既存上限を変更していない。
MTU→services→characteristics→report map/reference→CCC→readyの順序も維持する。
timeout/cancelはhost側でterminate/slot releaseを完了してcaller所有権を返し、workerがretryする。
通常版のsecurity timeoutでもこのcleanupと次attemptを観測した。
長押しforget＋pairingの明示操作は維持し、検証中には実行していない。

sleep/client/pair/forgetのsuppressionをscheduleとworker loopで確認する。
stopは強制task deleteを行わず、scan/open cancelと退役を待つ。
作成予約中もsleep-readyとは判定しない。resumeの複数schedule要求は1workerへ集約する。
実機の今回版でのKEY1 suspend/resume、BLE wakeの回帰確認は未実施で、host結果とは区別する。

### alternative・trade-off

scan延長・retry追加・bond消去・毎回pairingでは、実測のsecurity待ちを解消した証拠にならず、
既存登録を失わせる可能性があるため採用しない。今回は確認できた所有権競合の修正とphase traceに限定した。
radio duty、scan間隔、security待ちのworst caseは従来と同じで、電池消費の改善を主張しない。
debug時にはログ量・ring上書き・timingへの影響があり、通常使用はinfo/CDC offへ戻す。
NVSエラー時は自動消去で起動継続する代わりにBLE初期化エラーを返すため、保存データを保持した上で別途修復が必要になる。
REPEAT_PAIRINGの自動key置換を抑えたため、本当にpeer側だけがbondを失ったケースでは既存の明示forget/pairを使う。

### Host tests（17）

`.research/reconnect-host-tests.log`に最終実行結果を記録。以下はすべてPASS。

- 本番worker：peerなし、20回不在scan→後から出現、scan/connect timeout、HID open failure、disconnect後retry、open中cancel、suppression、resumeの単一worker、作成の再入、終了直前disconnect。
- HID：CONNECT前ENC成功・失敗、実handleでのcancel、open cleanup、GATT deadline/stale callback、report bounds/subscription/input lifetime、REPEAT_PAIRINGでbond delete 0回。
- NimBLE adapter、keyboard scan policy、BLE service、multi-peer、server、PaperMono powerの既存関連テスト。

worker testはradio結果とschedulerを模擬し、同じ本番workerコードを実行する。
実NimBLE/GATTのcleanupは別のHID/adapterテストで確認する。host testを実機接続成功率の代用にはしない。

### 実機acceptance・latency（18–22）

| 要求された試験 | 今回の結果 |
|---|---|
| USBなしcold boot・ZMK後からON ×5 | 未実施。5/5とは報告しない |
| USBなしkeyboard already ON ×5 | 未実施。USB付きsoftware rebootの単発成功・失敗は別記録 |
| USBなしdisconnect→OFF/ON ×5 | 未実施 |
| 2〜5分不在→ON | USB付き診断で約3分不在のretry継続を確認。対象を検出したがON後90秒で入力不可。acceptanceは失敗 |
| 最終版suspend/resume | host PASS、今回版での実キー回帰は未実施 |

ZMK ONの正確な時刻をログと同期していないため、ON→HID readyのmin/median/maxは算出不能。
未成功試行を除いて平均化したり、advertisement→readyをON→readyと表示したりしない。
比較用の別指標として、SDK診断版の単発成功はopen開始7.302s→ready10.126sで2.824秒。
失敗試行ではGAP接続後securityだけで60秒消費する。最終acceptanceを満たすにはこの待ちの原因解明が必要。

### データ保持・サイズ・artifact（23–25）

app-only flashを0x10000へ実施し、esptoolの書込みhash検証が成功。
FAT/NVSの事前読取りを`.research/reconnect-before/fat.bin` / `nvs.bin`へ保存した。
bond削除、BT_CLR、全flash/NVS eraseは実施していない。
display/fonts/startup/Writer/touch/RTC/battery/LEDのコードはこのフェーズで変更していない。

最終readbackを`.research/reconnect-after-fat.bin` / `reconnect-after-nvs.bin`へ保存して比較した。
FAT全領域のraw bytesは同一ではない。ESP-IDF公式のWL除去・FAT展開で38ファイルを比較し、
差分は診断コマンドを記録した`/.shell/history`の本文・時刻・サイズだけだった。
全既存文書、全checkpoint、startup、その他37ファイルの本文はbyte一致。
結果は`.research/reconnect-fat-preservation-result.log`。
別途USB Shell読取りでも`/transport-test.md`の173 B、未保存recoveryの200 Bとz4、JP layoutを確認した。
文書SHA256は`a82889b15c8d7078570a9de577afc29f9a927742c8bdb6c46b1621073e78eb69`。

NVSはentry/pageの物理配置ではなく、有効な論理値を比較した。
remembered peer、NimBLE bond storeの5キー、入力/Writer/font/powerを含む全設定値は一致。
唯一の差分は既存WireGuardコードの起動時単調時刻予約`wireguard/tai_ceiling`で、
差分18,934,560,000秒は既存の予約単位3,155,760,000秒の6回分に一致した。
保存データ消失とは扱わず、この単調カウンターを古い値へ戻していない。
結果は`.research/reconnect-nvs-preservation-result.log`、更新元は`wireguard_time_reserve()`。
診断用log level/sinkも比較時点で原値に一致した。

`/.shell/startup`の2行を読み戻し、runtimeのrefresh=fastest、JP、info/CDC offを確認。
読取り後の再起動でもShell、remembered 1/1、通常ログ設定、USB versionを確認した
（`.research/reconnect-final-reboot-info.log`）。USB version成功をBLE入力成功に置き換えない。

| 指標 | terminal原版 | 最終通常版 | 差分 |
|---|---:|---:|---:|
| PIO静的RAM | 52,464 B | 51,884 B | −580 B |
| PIO app flash | 3,317,927 B | 3,319,815 B | +1,888 B |
| app BIN | 3,323,712 B | 3,325,600 B | +1,888 B |

RAM値は各成果物のELFに対応するビルド結果。`.dram0.data`は−588 B、`.dram0.bss`は+8 B。
名前付き静的シンボルの変更にはHID stage pointer +4 B、attempt/広告/match counter各+4 B、starting flag +1 Bがある。
総RAM減少をreconnect省メモリ化の効果とは扱わない。worker stackは従来の4096 B、既存ログringはPSRAM 51,200 B。

- 通常版：`build/papermono-ble-reconnect/firmware.bin` / `firmware.elf`。
- BIN SHA256：`e70a22859d2f0cb4b8d40d41a28c1962d3b66f7926d85d85c728c1243e7bd1b0`。
- ビルド：`.research/reconnect-final-build.log`、SDK NimBLE/全体logは通常INFO設定へ復帰。
- 書込み：`.research/reconnect-final-upload.log`。factory imageは書き込んでいない。
- SDK診断用成果物は`diagnostic-sdk.bin/.elf`として別名で保管。常用版ではない。
- `sdkconfig.esp32_s3_devkitc1_n16r8`は事前コピーとSHA256一致（`f79b32c8c8a228b8dbfc0069712120b5e9984684dbacfc255136eb80ecd5ca00`）。

### 未解決事項・次に必要な証拠（26）

1. security rc=EALREADY後にENC_CHANGEが来ない原因。失敗・成功の双方で、SM procedureの生成/終了、SMP packet順序、HCI encryption結果、可能ならZMK側同時ログが必要。SDK DEBUG flagだけでは不十分だったため、compile/runtime両方のログ上限または限定したSM traceが必要。
2. 現象の実機再現率：今回のlong-absence試行は1回失敗。USB付きbootの複数open失敗はあるが、所定条件を揃えた5回matrixではない。主症状を解決する前の最終成功率は未確定。
3. 上記原因を証拠で修正した後、USBなしcold boot/already ON/OFF-ONを各5回、long absence、KEY1/BLE wake回帰、ON→readyの時刻同期を行う。

添付の「原因を特定できなかった場合はfailure phase・instrumentation・再現率・次の証拠まで整理して終了」に従い、
推測によるsecurity timeout変更や再pairingで完了扱いにはしない。
