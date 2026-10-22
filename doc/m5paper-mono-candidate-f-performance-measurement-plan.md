# PaperMono Candidate F — A/D/F描画速度の定量比較準備

2026-10-22 JST。対象コードはCandidate F実験Branchの`417a6cb382a3ebe3f0c16a2b25727b03267dc295`。**既存機能でソフトウェア時間の比較準備は可能。Firmware改修は不要。実測値は未取得であり、A/D/Fの速度差はまだ判定しない。**

本書はコマンド・計測定義・選別条件・ログ保存形式の調査結果である。試験操作、回数、測定順序、合否基準を確定した実機手順書ではない。それらは[初期光学評価](m5paper-mono-candidate-f-optical-evaluation-20261022.md)と本書を共有後、ユーザーとChatGPTが決定する。今回USB/COM、Flash、実機測定、Hostテスト、Buildは行っていない。

## 1. 結論と比較対象

既存の32件Traceと区間統計により、次を分けて比較できる。

1. `0x20`送信直前からBUSY待機終了までの**ソフトウェア計測時間**。
2. RAM転送、LUT送信、BUSY待機、post-sync、Baseline復元を含むDriver記録区間。
3. 更新開始間隔、BUSY完了から次の開始までの空き時間、派生計算による完了間隔。
4. WriterがCHARイベントを受け付けてからDriver処理終了までの近似遅延。

BUSYピンの厳密なエッジ時間、光学的な画素応答、物理キーダウンからの全遅延は測れない。4も集約されたFrameの入力範囲を表すもので、全キーそれぞれの遅延ではない。

A/D/Fは同一10F時間軸。D/Fは同じ経路でBaseline LUTを復元するため速度は近いと予想されるが未測定。Aは復元を行わないためDriver総時間に差が出る可能性がある。共通10Fから実測msや順位を導かない。根拠：[LUT](../src/drivers/epd_ssd1677_reference.inc#L10)、[送信・BUSY](../src/drivers/epd_ssd1677_reference.inc#L317)、[復元](../src/drivers/epd_ssd1677_reference.inc#L365)。

## 2. 既存コマンド一覧

以下はソース上の構文確認であり、今回実機で実行したコマンドではない。番号表は操作順序ではない。

| コマンド | 内容・注意 |
|---|---|
| `lut 1` | A選択。通常App/SketchともA |
| `lut 2` | D選択。SketchはA例外。WriterのD比較に使用可能 |
| `lut 3` | SketchもDの選択。今回のWriter比較で必須ではない |
| `lut 4` | E1選択。今回のA/D/F比較対象外 |
| `lut 5` | F選択。通常App/SketchともF |
| `lut` / `lut status` | 選択状態、累積カウンタ、Queue等の詳細。単独では区間平均を出さない |
| `ram status` | `rows-dual`または`full`の設定確認 |
| `ram rows` | 既存rows-dual転送を選択。LUTは変更しない |
| `display mode display0` | 現在の`refresh=...`と設定可能値を表示。tile・SPI・timing等の全現在値を列挙する機能ではない |
| `display async display0` | busy/pending/suspended/error、累積submitted/replaced/cancelled/rejected/started/completed/physical、maintenance、generation等 |
| `display async display0 wait` | 既存の完了待ち（最大70秒）後に上記を表示。光学完了や将来の全submitの完了を保証しない |
| `display test display0 fastest-lut trace` | 最新32件のWL記録を読出し。下記の4行を`seq`で対応付ける |
| `display test display0 fastest-lut trial-start` | 区間集計を新規開始。開始中ならINVALID_STATE |
| `display test display0 fastest-lut trial-status` | 区間集計を表示。開始中は継続し、停止後は凍結結果を表示 |
| `display test display0 fastest-lut trial-stop` | 区間集計停止。未開始/停止済みならINVALID_STATE |
| `display test display0 fastest-lut baseline` / `disable` | 両方ともAを選択。表示停止・Driver停止・無描画化ではない |
| `display test display0 fastest-lut candidate_d` / `candidate_f` / `status` | 番号選択・状態取得の長形式 |

長形式の`fastest-lut`を`writer-lut`に置き換えた互換aliasも有効。**`trial-start`等は単独のShellコマンドではなく、上記の長形式が必要。`lut trial-start`も無効。** 選択はRAM内だけで、再起動後の既定はD（Sketch A）、RAMはrows-dual。今回startup/NVSを変更しない。

設定構文として既存の`display mode display0 refresh=fastest`、`fastest-path=tile`、`spi-mhz=20`、`dirty-window=rows-dual`、`timing=off` / `timing=on`もある。1コマンド1設定。ここでは設定変更やcleanupを実行しない。特にモードsetterはExecutor policyも更新するので、測定区間内で操作しない。

根拠：[Shell長形式・Trace](../src/shell/solar_os_shell_writer_lut.inc#L42)、[短形式とRAM](../src/shell/solar_os_shell_writer_lut.inc#L136)、[alias受付](../src/shell/solar_os_shell_commands.c#L657)、[mode/async](../src/shell/solar_os_shell_commands.c#L729)、[mode値・setter](../src/drivers/epd_ssd1677.c#L1179)。

## 3. Traceと区間集計の違い

Traceは各Frameの`WL`、`WL time`、`WL panel`、`WL diag`を出力する。`seq`はDriver記録番号、`gen`はAsync snapshot世代。1入力=1世代=1物理更新とは限らない。sourceはwriter/sketch/otherで、ShellとLauncherはotherに含まれる。

区間統計はframes、実際の画像`0x20`数physical、A/D/E1/F/OTP別、source別、noop、error、ram_bytes、RAM path件数、periodic_resync、timing_missingを持つ。平均・最小・最大・合計・件数を持つ時間はBUSY、開始間隔、BUSY完了→次開始だけ。**SPI、LUT、復元、Driver総時間、入力遅延の区間平均・percentileは出ないためTraceが必要**。表示平均は整数`sum/count`で、samples=0のavg=0は「未測定」でありゼロ時間の実績ではない。

`trial-start`は区間統計だけを初期化する。Trace ring、累積カウンタ、LUT選択、RAM/Shadow、温間状態、32回周期のRAM再同期カウント、光学状態は初期化しない。区間内に他App/OTP/noopが含まれれば統計にも混ざる。`physical`は画像activation数で、電源制御だけの`0x20`は別の`power0x20`。

Trace ringは最新32 Frameで上書きされ、no-op等も枠を使う。区間統計は32件を超えて継続できても、消えたFrameの個別SPI等は復元できない。区間first_seq/last_seqとTraceの対応、mode別・source別件数、activation_seqの連続性を確認する。記録が欠けたら「全件」として平均を出さず、得られた部分集合と欠落を示す。

根拠：[記録構造と32件上限](../src/drivers/epd_ssd1677_writer_d.h#L6)、[ringへの保存](../src/drivers/epd_ssd1677_writer_d.inc#L85)、[区間集計](../src/drivers/epd_ssd1677_trial_stats.inc#L18)、[開始/停止](../src/drivers/epd_ssd1677_trial_stats.inc#L109)、[統計表示](../src/shell/solar_os_shell_writer_lut.inc#L9)。

## 4. 各時間の正確な定義

時刻は同じ起動内の`esp_timer_get_time()`、単位µs。再起動をまたいで差を取らない。以下の派生値は時刻が有効で前後関係が正常な場合のみ計算する。

| 項目 | 開始点 → 終了点／計算 | 測定範囲・限界 |
|---|---|---|
| `busy_us` | `activate`（`0x20`送信直前）→`ready`（BUSY待機ループ終了） | コマンド送信、最低1 RTOS tickのguard、GPIO polling・スケジューリングを含む。BUSYピンの純粋なHIGH幅や光学応答時間ではない |
| Driver記録時間 | `WL time execute`のend−start | `present_snapshot`内の記録初期化→refresh後の`writer_d_finish`内timestamp。差分検出、RAM、LUT、電圧、BUSY、post-sync、復元、Shadow処理等を含む。Executor callback全体やstatus完了処理の末端までは含まない |
| `REF total_us`（任意ログ） | `reference_refresh`入口→Shadow処理後の内部end | Driver記録とは境界が違う。REFログ出力自体はこの値に含まれないが、外側のDriver記録endには含まれる |
| `ram_bytes` | 成功したRAM payloadの総和 | pre-writeとpost-syncを含む。RAM addressコマンド、LUT、電圧、その他SPI command/dataは除く |
| `spi_us` | RAM各band/tileの変換終了→polling transmit復帰後timestamp、の合計 | RAM payload転送と小さなtransaction準備・呼出し費用。変換自体、RAM window/address設定、LUT/電圧/復元のSPIは含まない。全SPI時間と呼ばない |
| `ram`時刻のend−start | 最初のRAM write開始→最後のpost-sync write終了 | 間にLUT設定・BUSY待機が入るため、**RAM転送時間として使わない** |
| `lut_us` | 選択LUTの`cmd_data(0x32,...,105)`直前→復帰直後 | command+105B送信とソフトウェア費用。後続の0x03/04/2Cは除く |
| `restore_us` | post-sync後のAの`cmd_data(0x32,...,105)`直前→復帰直後 | D/Fでのみ実行。Aの0は「処理なし」であって復元測定の欠損ではない。追加activationなし |
| `start_gap_us` | 現在activate−前回activate | 更新開始→開始。前回処理・待機・人の入力間隔を含むのでDriver速度そのものではない |
| `ready_gap_us` | 現在activate−前回ready | **前回BUSY完了→次の更新開始**。ready→readyの完了間隔ではない。post-sync/復元やQueue待機も入り得る |
| BUSY完了間隔（派生） | 現在ready−前回ready | 隣接する有効な単一activation同士のみ。同じpairなら`ready_gap_us + 現在busy_us`。光学完了間隔ではない |
| Driver完了間隔（派生） | 現在execute_end−前回execute_end | 復元・post-syncを含む処理終了同士。BUSY完了間隔とは別 |
| Writer render時間（派生） | rendered−render_start | Writerでclear/header/layout等の描画を始める直前→submit直前。submit snapshot copyや物理更新は除く |
| QueueからDriver開始（派生） | execute_start−queued_us | 最新snapshotのQueue timestamp→Driver開始。timestampはbuffer copyより前なのでcopy・dispatch費用を含み、純粋なQueue待ちだけではない |
| 最初のQueueからDriver開始（派生） | execute_start−queued_first_us | Writer間のpending置換時は古いQueue時刻を継承。集約前の待ちも含む |
| 入力→BUSY完了（派生） | ready−input_first_us / input_last_us | Writer CHAR受付から当該FrameのBUSY待機終了。後続のpost-sync/復元は含まない |
| 入力→Driver完了（派生） | execute_end−input_first_us / input_last_us | Writer CHAR受付から当該FrameのDriver処理末端。物理キーダウン・BLE到着・Input Service内待機や光学応答は測定範囲外 |

BUSY境界：[reference.inc L329](../src/drivers/epd_ssd1677_reference.inc#L329)。Driver開始：[epd_ssd1677.c L1048](../src/drivers/epd_ssd1677.c#L1048)、終了とRAM集計：[writer_d.inc L62](../src/drivers/epd_ssd1677_writer_d.inc#L62)。RAM timing：[reference.inc L133](../src/drivers/epd_ssd1677_reference.inc#L133)、[L165](../src/drivers/epd_ssd1677_reference.inc#L165)。LUT/復元/REF：[L317](../src/drivers/epd_ssd1677_reference.inc#L317)、[L365](../src/drivers/epd_ssd1677_reference.inc#L365)、[L392](../src/drivers/epd_ssd1677_reference.inc#L392)。間隔：[trial_stats.inc L43](../src/drivers/epd_ssd1677_trial_stats.inc#L43)。Writer受付：[writer.c L1657](../src/apps/solar_os_writer.c#L1657)、render：[L1435](../src/apps/solar_os_writer.c#L1435)、submit：[L1479](../src/apps/solar_os_writer.c#L1479)。

入力timestampはCHARイベントだけの記録で、すべてのキー・ショートカットに必ず付くわけではない。`writer_handle_char`前の受付時刻なので、必ず文書が変更されたことを保証もしない。source=otherや入力なし再描画の0を入力時刻として使わない。

## 5. Asyncと計測への影響

既存Asyncは最新pending snapshotを置換する2 buffer方式。Writer同士の置換では最初/最後の入力時刻、inputs、coalescedと古いqueued_firstを継承するが、render時刻は最新snapshotのもの。したがって集約Frameを単独キーの遅延として扱わず、`input`/`merged`とともに入力範囲の最初・最後に対する遅延を報告する。世代番号はsubmit時に付与される。

`display async`のcompletedは成功callback数でno-opも含む。physicalは別。Driver WLの`physical=1`とAsyncのcompleted/physicalを混同しない。エラー時は既存fault契約に従う。Queue counterは累積で、区間統計は開始時との差を取る。queue_valid=false、queue_wrapped=true、overflow=trueの区間はそのまま正常比較に使わない。

LUT選択・Trace・trial制御はREAD barrierを使用し、現在のBUSY・post-sync・復元まで待つがpendingを破棄しない。**trial-stopはpendingを排出する操作ではない**。pending_end=trueなら停止後のFrameが区間外で実行され得る。開始/停止境界のbusy/pending、generation、seqの対応が必要。停止済み区間のsummaryは凍結されるが、Trace ringはその後も進む。

Traceの最初のstart_gap/ready_gapは区間前の更新を参照し得る。一方、区間統計はtrial-startで区間専用の前回時刻を消すため最初の区間外pairを集計しない。Host側の派生間隔も区間内で隣接する採用Frameだけから算出し、途中に別App/OTP/欠落があればそこでpairを切る。

根拠：[snapshotと集約](../src/services/solar_os_display_async.c#L175)、[worker](../src/services/solar_os_display_async.c#L49)、[barrier](../src/services/solar_os_display_async.c#L278)、[Driver接続](../src/services/solar_os_ssd1677.c#L72)、[READ制御](../src/services/solar_os_ssd1677.c#L136)。

## 6. 公平な比較の要件とサンプル選別

同一Firmware・Board・SPI実効20MHz・fastest・tile・Async・rows-dualを維持し、LUTだけをA/D/Fで変える。Writer文書内容、フォント/zoom/scroll/cursor状態、変更操作・変更領域、初期画像/Shadow、光学履歴、OTP baselineの有無、温度・電源・CPU条件も記録して揃える。初期化方法や測定順は未決定。

SPI設定値と実効値を分ける。`spi_device_get_actual_freq`の既存ログ`SPI requested_khz=... actual_khz=...`、または同条件の`REF phases ... spi_khz=...`を保存する。`display mode display0`だけで20MHz・tileを実機確認した扱いにはしない。[実効SPI取得](../src/drivers/epd_ssd1677.c#L95)

**warm rows比較に採用する基本条件：**

- `err=0`、`timing_valid=1`、画像`0x20=1`、source=writer、実際のmodeが目的のA/D/Fと一致。
- 実RAM path=`rows`、同じram_window・差分region・`ram_bytes`。設定がrowsでもcold/未同期では実際にはfullになるため、設定値だけで選別しない。
- D/Fはrestored=1。Aはrestored=0かつrestore_us=0が通常であり、全候補にrestored=1を要求しない。
- noop、OTP、cold/未同期、periodic_resync、別App、エラー、境界のpendingや欠落を通常warm平均へ混ぜない。これらは削除せず除外理由付きで別記する。
- CHAR入力遅延比較では入力時刻が有効、inputs/mergedとrender対応が解釈可能。単独Frameと集約Frameは別群。文字入力の速さが異なる集約群を波形だけの差としない。
- 間隔比較では区間内の物理activationが隣接し、時刻/activation_seqが連続すること。人の入力間隔が支配する低頻度入力を最大描画速度の比較に使わない。

RAM payloadのコード上の目安は、warm rowsで100B/行×3回、warm fullで144,000B、cold/未同期で192,000B。前者の3回は新画像pre-write＋両面post-syncで、3回の物理更新ではない。実記録を優先し、**転送量が違うサンプルをそのまま波形速度比較に使わない**。同じバイト数だけでも内容・位置まで同じとは保証されない。

32回後の周期的full-RAM再同期は既存挙動で、通常rowsとは転送量が異なる。Shellコマンドの入力・応答描画、LUT切替前後、App切替もカウントを進め得る。Traceにcoldの直接fieldはない。coldそのものの確認が必要なら既存REFのcoldログを補助にするか、full/未同期群を一括して別扱いとする。既存REFを有効にするかはログ負荷との兼ね合いで後続評価時に決める。

`timing=on`はREFログを増やしDriver総時間・次Frame開始を変え得る。通常のWL timingはtiming=offでも取れるため、低干渉の比較は同一のtiming/perf/log設定を前提にする。本体Shellの文字表示も実更新を発生させるので、将来の収集では既存USB Shellを利用できるが、**USB経由でもREAD barrierの干渉は残る**。測定途中の連続status/trace取得が無影響とは扱わない。

fastestのExecutor policyはframe_ms/idle/budget=0で、typing/Hybridとは異なる。設定setterはpolicyを更新するため区間外で扱う。[再同期とRAM窓](../src/drivers/epd_ssd1677_reference.inc#L218)、[サービスpolicy](../src/services/solar_os_ssd1677.c#L143)。

## 7. 推奨ログ保存形式

実データ取得後に、原本のUSB/bootログを無加工のUTF-8 textとして保管し、解析表を別ファイルにする。今回の作業では架空の測定行や値を作らない。

**manifest（JSONまたはMarkdown）：** date/timezone、run_id/boot_id、Firmware SHA/Commit、Board/IDF/flags、refresh/path/RAM、SPI requested/actual、LUT選択と実使用mode、timing/perf/log設定、App/文書条件、入力方法、初期RAM/OTP/光学状態、温度（不明なら不明）、区間seq、開始/停止時Queue、原本ファイルとhash、除外理由。人やカメラの外部時刻はESPの起動内µsと別列で保持する。

**Frame表（CSV、1 record/行）：**

```text
run_id,boot_id,seq,gen,source,mode,physical,err,restored,inputs,merged,region_x,region_y,region_w,region_h,ram_path,ram_x,ram_y,ram_w,ram_h,ram_bytes,periodic_resync,timing_valid,activation_first_seq,activation_last_seq,input_first_us,input_last_us,render_start_us,rendered_us,queued_first_us,queued_us,execute_start_us,execute_end_us,ram_start_us,ram_end_us,activate_us,ready_us,spi_us,lut_us,restore_us,busy_us,start_gap_us,ready_gap_us,include_group,exclude_reason
```

WLの4行を同じseqで結合する。`execute`等の時刻pairの順はShell出力に従う。原本の0/−1はそのまま残し、派生計算では欠損・対象外に読み替える。負値や時刻逆転を0で埋めず、除外理由を記録する。boot_id違い・seq重複は勝手に連結しない。

**派生表：** driver_us、render_us、latest_queue_to_driver_us、first_queue_to_driver_us、first/last_input_to_ready_us、first/last_input_to_driver_end_us、ready_to_ready_us、driver_end_to_end_us。各列の計算式は§4。必要な時刻が無い場合は空欄（NA）にし、推測補完しない。

**集計表：** modeと同条件群ごとの採用/除外件数、physical/noop/queue/periodic内訳、各時間の件数・平均・最小・最大。必要なら中央値/percentileは完全なFrame表から計算し、少数例やTrace欠落時はその制約を添える。区間統計とTraceの件数を突き合わせ、両者が一致しないまま全件統計とはしない。画像`0x20`数、submit数、キー入力数を別列にする。

実ログも最終測定条件もまだ無いため、今はHost集計scriptを追加しない。原本の形式と必要な集計が確定した後に必要なら実装できる。無条件の自動実機操作・反復は導入しない。

## 8. 追加計測の要否・未確定事項

| 目的 | 既存機能での対応 | 追加が必要なら（今回は実装しない） |
|---|---|---|
| A/D/Fのwarm Driver/BUSY proxy/RAM SPI/LUT/復元比較 | 条件を選別したWL Traceで可能 | 不要 |
| 更新開始・BUSY完了・Driver完了の各間隔 | Traceの隣接timestampから可能。ready_gapの誤読に注意 | 不要 |
| Writer CHAR受付→Driver完了 | 入力metadataのあるFrameで可能。集約は範囲として報告 | 不要 |
| Queue差分・集約/no-op・更新数の整合 | trial summary＋Async status＋Traceで可能 | 不要 |
| pure BUSY pin edge幅、物理SPI波形 | 現状のsoftware timerでは保証不可 | GPIO edge capture/外部logic analyzerなど。Driver/計測への影響評価が必要 |
| 物理キー→可視画素変化、黒濃度 | 既存WLだけでは不可 | 入力源timestamp対応、同期した映像/光学計測等 |
| 32件超の全FrameのSPI等を欠落なく収集 | 区間summaryだけでは不足 | 収集単位を制限するか、将来のbuffer/記録拡張。容量・同期・ログ負荷の回帰リスクあり |
| 完全な同画像・差分領域の証明 | WLのregion/bytesは補助のみ | 必要なら外部の既知文書/画像記録、将来のsnapshot hash等。今回は追加しない |

`SOLAR_OS_SKETCH_LATENCY_TRIAL`の追加traceは既存Fビルドで0のため、利用可能な計測として前提にしない。既存perfのWORKER_START/END等は有効化・ログレベル依存で、常時取得済みの値ではない。[optional trace/perf](../src/services/solar_os_display_async.c#L97)

ソース・既存Hostケースも参照したが今回再実行していない。関連ケースは[区間集計・時刻・overflow](../tests/host/sketch_d_diagnostics_cases.inc#L32)、[F実装の既存検証報告](m5paper-mono-candidate-f-1f-implementation.md)。文書だけの変更なのでHostテストとFirmware Buildは不要と判断した。

## 9. 準備状況と引継ぎ

**ソフトウェア時間比較の準備は完了。実機測定・速度判定は未実施。** 新診断コマンド、計測Firmware、LUT、RAM/Shadow、BUSY、pacing、既定LUTは変更していない。既存のF Firmwareを使う比較が可能であり、この文書のための再Flashは不要。

次にユーザーとChatGPTが決める事項は、比較する操作、回数・順序、共通初期化/温間基準、Queueと32件ringの境界、log設定、測定・光学の判定基準。主な残課題は人の入力間隔・Writer集約・cold/full/周期同期の混入・ログ負荷の統制である。

作業Branch：`feature/papermono-candidate-f-validation`。基点：`417a6cb382a3ebe3f0c16a2b25727b03267dc295`。Worktree：`E:\ESP32\M5PapserMono-SolarOS\.research\candidate-f-validation\worktree`。本書はP0記録とは別の文書Commitとし、originへPushする。Integration/mainや元の実験Branchにmergeしない。
