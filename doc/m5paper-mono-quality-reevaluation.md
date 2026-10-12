# PaperMono / SSD1677 表示品質の再評価（Task 1）

調査日: 2026-10-12。結論: **CONDITIONAL — 新規LUTを作らず、既存baseline/doseの短い同条件比較へ進む価値はある。doseの正式採用・安全性保証は現段階ではできない。**

既存のdoseはfastest級の速度と読みやすさを両立した有望な観察結果を持つ。一方、保持黒への直接の補強は行っておらず、なぜ固定文字が濃く保たれたかは未確定。次は初期濃度の違いと保持中の濃度変化を分離する。具体的なhold LUTは不採用とし、再試験しない。

この調査でfirmwareのビルド・flash・実機更新・波形選択・設定変更は行っていない。新しいLUTも追加していない。現在のdose選択を変更するUSB操作も行っていない。以下の実測値はすべて過去のログであり、今回の新規実機測定ではない。

## 1. 作業状態と対象バージョン

開始時worktreeは`E:\ESP32\M5PapserMono-SolarOS`、branch `fix/files-legend-wrap`、HEAD `6e1db442`。追跡ファイルの未コミット差分なし。未追跡のユーザー`.vscode/`は触れていない。現在のHEADから`research/papermono-display-quality-review`を作成し、過去の実験ブランチへcheckoutしていない。

UI140%、Files修正`a76fc022`、波形実験`95c56b8e6657e56461cd0d2a1453b28119f13c96`を含む現行コードを調べた。直前に書き込んだartifactは`build/papermono-files-legend-trial/firmware.bin`、source `a76fc022`、writerdeck / m5paper_mono、`SOLAR_OS_SSD1677_WAVEFORM_TRIAL=1`、3,429,696 bytes、SHA256 `c5fbfe959b322eb96d6bef4b705ccd31f5977ce7f7c3f0cd58c6421bb97be700`。今回、それを再ビルド・置換していない。

ドライバーのfastest設定は40MHz SPI、8×8 tile変換、32行DMA band、2 plane全面転送。非同期executorとsnapshot方式を維持。40MHzはSPIの転送クロックで、画素を駆動するwaveformのFRや「40Hz」とは別物。

| 参照コード | 今回確認した版 |
| --- | --- |
| M5GFX | `c5a3fefad0b38a52cc750e2c4761e339a69a7208`。取得時のmasterも同一。関連する新しいupstream変更はなかった |
| 参照Writerdeck | `E:\ESP32\M5Paper_Writerdeck` / `86877a881a89b4867c561899f42bdfecd973af1b`。インストール済み`Panel_SSD1677.cpp`のSHA256は`c87a4c6a95a24d50d4d2750fe99215c655d673b040337523e3ace13fba093f58`で過去の調査と一致 |
| 公式OTP Demo | `c7c02554f89fd06f80d988b805b2a59050c78a46` |
| FreeInk SDK | `7fcd3b34c108f28c086ade3b433924022dbc0998`。第三者の実装として扱う |

SolarOSはM5GFXを実行時ライブラリとして呼び出しているわけではない。移植したbaselineの110 bytesを、pinしたM5GFXの`lut_fastest_SSD1677`および参照Writerdeckの`lut_fastest`と比較し、**全bytes一致**を確認した。他のM5GFXモードや第三者LUTと同一だとは扱わない。

## 2. 過去の実験結果と今回の再集計

過去の[波形比較報告](m5paper-mono-fastest-quality.md)と`.research/fastest-quality/`の17ログを再解析した。診断ログは`QUALITY requested=`より前に限定し、診断終了後のShell再描画を除外すると、既存JSONの値とすべて一致。エラー・不完全REF・破損イベントは検出されなかった。手動10/20/30のログは累積時点であり、足して60試行とは数えない。

| 波形 | 光学的な観察（過去のユーザー報告） | 評価 |
| --- | --- | --- |
| baseline | 独立診断10回ではやや薄い印象だが目立つ変化なし。通常使用では更新につれて薄まることがあった | 更新履歴による問題は観察されているが、定量濃度・温度依存は未確定 |
| dose | 初回はやや濃い。消去残りはやや少ない印象。Writer30文字で濃さをかなり維持し、Shell30文字でもほぼ開始時と同じ濃さ、表示異常なし | 有望。ただしbaselineの手動30文字と同じ初期画像・履歴・照明で比較した試験ではない |
| hold | 独立診断10回で固定文字・線が薄まった。Shell10→20でさらに薄くなり、30で読みづらい。Writer10でも非常に読みづらい | この具体的なLUTはNO-GO。読みやすい濃さに安定する結果なし |

同じcycle pattern、dirty bounds `152,332,288,102`、40MHz/tileでの過去のwarm値:

| 指標 | baseline | dose |
| --- | ---: | ---: |
| warm試行数 | 9 | 9 |
| Driver p50 / p95 | 110.301 / 110.377ms | 110.290 / 110.482ms |
| BUSY観測 p50 | 51.723ms | 51.820ms |
| 2 plane変換 p50 | 34.279ms | 34.221ms |
| SPI呼出し時間合計 p50 | 19.795ms | 19.789ms |
| 別のWriter単発試験数 | 5 | 5 |
| Writer入力→完了 p50 / p95 | 163.475 / 171.482ms | 168.511 / 179.858ms |

Writer p50の差は+3.08%、p95は+4.88%。n<20のp95は探索値で、遅延保証ではない。Writerは文書名・初回dirty boundsが違うため、Driverのcycle比較とは分ける。BUSY観測は待機APIの経過時間であり、オシロスコープによる信号測定や光学的な濃度測定ではない。

## 3. 一次資料と波形データの照合

### 3.1 SSD1677と対象パネル

[SSD1677 datasheet Rev 1.0 / Nov 2018](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1267/SSD1677.pdf)を再取得し、過去と同じSHA256 `daf8b726f822f6907529386b8af6fbea6ec5e8ecc7b7af361340be911d632544`を確認した。印刷P15–17、P23–24、P27–28、P30の表を画像でも確認した。PDFの物理ページと印刷番号は1ページずれる。

- P15のRAM表はR RAMとB/W RAMの4組合せを説明する。`old/new`はホスト側の意味づけで、データシートがすべての用途でそう規定しているわけではない。SolarOS fastestではR RAM=`old`、B/W RAM=`new`として遷移クラスを構成する。
- P16–17: 10 groups×4 phases、TPはframe数、TP=0は位相省略、RP+1が繰返し回数。VSは00=VSS、01=VSH1、10=VSL、11=VSH2。LUT4はVCOM側の選択。0..104を0x32、105を0x03、106..108を0x04、109を0x2Cへ送る。112-byte配置の110/111はreservedで、SolarOSの110-byte表と矛盾しない。
- P23/P30: baselineの設定値`0x46/0xA8/0x32/0x30`は、公称VSH1=+16V、VSH2=+5V、VSL=-15V、DCVCOM=-1.2Vに対応する。**保持クラスVS=0でもVSSとVCOMの設定値は同じではない。** これだけで実画素の電界やDC積算量を算出できるわけではない。
- P27–28: 0x21はRAMのnormal/bypass/inverse、0x22は温度・LUTロード、Mode 1/2表示、電源段階等の更新シーケンス選択、0x20はactivation。fastestのcold `0xCC` / warm `0x0C`は、ホストLUTを毎回OTPで上書きする構成ではない。公式partialの`0xFF`は温度とMode 2 LUTをロードし、表示後にanalog/OSCを停止する。
- P24: 公開Rev 1.0のDeep Sleep表は`A[1:0]=11`とHWRESET復帰を記載する。公式Demoが使う`0x10=01`の「Mode 1・RAM保持」はDemo側の説明を根拠にしており、このdatasheetだけから保証したとは扱わない。
- §6.8/6.9: 温度別OTP設定がある。fastestは固定ホストLUTで、0x18=80のセンサー選択だけでは温度補償された波形にならない。FRの5 bytesは比較できるが、今回確認した公開資料から全codeの実frame時間を確定していない。10 framesを10ms等に換算しない。

[DKE Module Manual](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1267/EPD_Module_User_Manual.pdf)も再取得し、SHA256 `6b65f56d5d549a132e5c807eef329fb3e997feab04ac06e8a107f01591618130`が一致した。P1/P4/P6は`DEPG0397BBS770F3`、3.97 inch、480×800、SSD1677、OTP/MCU LUT対応を示す。一方、[公式OTP Demo README](https://github.com/m5stack/M5PaperMono-OTP-Demo/blob/c7c02554f89fd06f80d988b805b2a59050c78a46/README.md)は`DEPG0397BBS770F3HP-XM`を対象と記載する。系列・解像度・controllerは一致するが、末尾HP-XMまでの仕様一致、実機のラベル・ロット・OTP版は今回確認していない。系列資料を個体の高速駆動保証に代用しない。

[M5Stack PaperMono公式資料](https://docs.m5stack.com/en/core/PaperMono)は、約10回のpartial fast更新後のfull推奨、外部波形のDC balanceへの注意、連続partialへの注意を記載する。さらにSoftware欄ではM5GFX波形の不安定性を指摘し、メーカーOTP例を優先するよう案内している。これは候補doseの認証でも「10回まで安全」という保証でもない。今回この注意を理由に通常アプリへ自動pacingや頻繁なfullを追加していない。

### 3.2 baseline / dose / holdの静的比較

対象コードは`src/drivers/epd_ssd1677_reference.inc:10`と`epd_ssd1677_waveform_trial.inc`。コメントを除いた実配列をdecodeし、全110 bytes、TP/RP、VS、FR、電圧を比較した。

| 遷移クラス `(old,new)` | baselineのSource | doseのSource | holdのSource |
| --- | --- | --- | --- |
| 00 保持黒 / LUT0 | VSS 10 frames | 同じ | VSL 2→VSH1 2→VSS 6 |
| 01 黒→白 / LUT1 | VSH1 2→VSL 8 | VSH1 1→VSL 9 | baselineと同じ |
| 10 白→黒 / LUT2 | VSL 2→VSH1 8 | VSL 1→VSH1 9 | baselineと同じ |
| 11 保持白 / LUT3 | VSS 10 | 同じ | 同じ |

3案ともgroup0は8 frames、group1は2 frames、RPは0、FR bytesは全て8f、VCOMのVSは0、電圧bytesは`17,46,a8,32,30`。doseの差はbyte50の2→1とbyte51の2→3だけ。holdはbyte0の00→90だけ。

**doseとbaselineでは、保持黒・保持白・VCOMのSource/共通電極選択とgroupごとの総期間が一致する。** TPは共通の期間表だが、保持クラスの全位相が同じVSなので、doseは保持黒へ新しい補強パルスを加えていない。変化画素の逆極性期間を減らし、目的色期間を増やしているため、同じ総frame数でも電気的な作用は同じとはいえない。正負のframe数や対称な黒化・白化だけでは、各画素の更新履歴、VCOM offset、実電圧を含むDC balanceを証明できない。

[M5GFXのpinしたコード](https://github.com/m5stack/M5GFX/blob/c5a3fefad0b38a52cc750e2c4761e339a69a7208/src/lgfx/v1/panel/Panel_SSD1677.inl#L144)のfastest LUTと一致することは、移植の来歴の確認である。コメントのnear field-freeを、完全な無電界や寿命保証とは解釈しない。M5GFXは4階調の履歴・Mode 2 absolute fallbackも持ち、その全挙動をSolarOSのmono実装と同一視しない。

### 3.3 公式OTP DemoとSolarOS

[公式ドライバー](https://github.com/m5stack/M5PaperMono-OTP-Demo/blob/c7c02554f89fd06f80d988b805b2a59050c78a46/components/EDP_OTP_LUT_demo/src/EDP_OTP_LUT_demo.cpp#L181)と`src/drivers/epd_ssd1677.c:488`を比較した。

| 経路 | 公式Demo | 現行SolarOS |
| --- | --- | --- |
| Full初期化 | HWRESET、0x12、0x18=80、booster末尾80、0x21=00 | 同じ主要設定。SolarOSの向きに合わせRAM scanは11=01（Demoは03） |
| `refresh_mono_full()` | 0x22=F8、RAM1へ反転画像、0x20。次に14、RAM2/RAM1へ同一画像、0x20 | 同じ主要シーケンス。成功後にhost shadowを昇格 |
| `wake_for_partial_update()` | HWRESET、software resetなし、border=80 | 同じ。RAM保持が前提で、rail-off後には利用しない |
| `refresh_partial()` | RAM1へ全面48000 bytes、0x21=00、0x22=FF、0x20 | 同じ主要シーケンス。partial後にhost側から0x26を書き直さない |
| 更新後 | 0x10=01、100ms待機 | 同じ。controller_ready=falseと光学baselineのvalidを分ける |
| `load_white_baseline()` | 両RAMへ白を書き、activationせずbaseline_ready=true | その省略経路は採用せず、未知の光学状態はOTP fullで同期する |

白RAMをロードしてもパネル上の既存像は消えない。Demoの起動経路をそのままコピーすると、赤い電源OFF後も残る実機の像とhost oldが一致する保証はない。現行の初回fullを削る案は採用しない。

待機には差がある。DemoのBUSY待ちは開始時1msと1ms polling、SolarOSのOTP待ちは開始時100msと20ms polling。HWRESETや更新後sleep待機もある。ただし過去のSolarOS OTP partialはtotal 857.383ms、activation観測559.426ms、RAM転送65.351msだった（[過去のrefresh報告](m5paper-mono-refresh.md)、`.research/hardware/refresh-fast-first.log`）。559msには100ms開始待機が含まれるため純粋なBUSYパルス幅ではないが、待機APIを短縮するだけで52ms級になる根拠はない。SPI/tileの適用で転送を削っても残るwaveform時間を無視しない。

### 3.4 FreeInk SDKの参考範囲

[PaperMonoDriver.cpp](https://github.com/Free-Ink/freeink-sdk/blob/7fcd3b34c108f28c086ade3b433924022dbc0998/libs/display/FreeInkDisplay/src/driver/PaperMonoDriver.cpp#L25)はOTP初回に0xFC、warmに0x0Cを使い、analog/clockを維持する。新旧planeを再書き込みし、cold optical historyに追加passを設ける。これはwarm経路を検討する参考になるが、公式Demoの「各更新後sleep」とは異なる制御で、fastest級の速度を保証しない。

同じファイルは独自LUT、VSH1=0x41（SolarOSの0x46と違う）、保持領域へのtop-upやdark backgroundの旧画像合成も使う。コメント自身が意図的なDC不均衡を説明する箇所がある。lab-validatedという第三者の記述をメーカー検証に格上げせず、独自LUT・電圧・旧画像の強制反転を移植しない。

## 4. 原因候補の根拠の強さ

| 仮説 | 根拠 | まだ説明できないこと / 判定 |
| --- | --- | --- |
| A 新規黒の駆動不足 | doseは白→黒の目的色を8→9 frames、準備を2→1に変更。初回やや濃いとの観察 | 初回改善の可能性は高いが、固定文字の減衰率の改善とは別。光学濃度の定量比較なし |
| B 保持黒の濃度低下 | baselineの通常使用で薄まり、holdで固定画素が強く薄まった。RAM不変でも光学状態は測定していない | baseline/doseの保持クラス設定は同じ。周囲更新・共通電極・境界・粒子履歴の関与は推測で、資料や目視だけで因果を確定できない |
| C old/newや未完了shadowの誤管理 | 成功後のみshadow昇格、immutable snapshot、warm/cold、失敗時の無効化をコードとHostで確認 | 現在のソフト不具合は再現していない。実Writerの固定領域が各時点で本当に00 classだったかの現物traceと、光学状態の一致は別途必要 |
| 初期濃度と評価履歴の差 | doseの手動30文字試験とbaselineの独立10回試験は同一履歴でない | より濃い初期黒が同程度に減衰しただけでも、最終像は良く見える。これを保持能力改善と混同しない |
| 温度・画面内の変化面積 | 固定ホストLUTと温度別OTPの差は確認。fastestは毎回両RAMを全面転送 | 過去ログには制御された温度・照明・変更画素数の比較がない。dirty boundsは物理駆動領域でも変更画素数でもない |

現行のold/newは、`epd_ssd1677_reference.inc:139`で0x24へimmutable new、0x26へcompleted shadowを全面転送し、BUSY待機成功後の`:169`でshadowを昇格する。`epd_ssd1677.c:947`のsnapshotはpresent中のみ固定。`solar_os_display_async.c`はactive bufferをpending置換で上書きせず、成功したframeだけcompletedへ進める。mode切替・rail-off・resume・fault・cleanup時のbaseline管理も既存テストに含まれる。

XORで変更がなければ更新しない。cleanupは同一画像でもOTP fullを強制し、baselineを再構築する。RAM範囲レジスタやdirty boundsを狭めただけで「外側の黒へ電界が及ばなくなる」とは証明できない。今回、未確認の矩形物理駆動を改善策として約束しない。

## 5. 改善候補（最大3案）

優先順は、公式駆動の利用、状態管理、既存実験波形の限定評価。いずれも今回の実装・実機試験は行わない。

| 評価項目 | 1. OTPの待機・warm経路 | 2. 初期状態と遷移分類の照合 | 3. 既存doseの同条件比較 |
| --- | --- | --- | --- |
| 具体的内容 | まず現行partialの速度内訳を短く再確認。候補はOTPの0xFC初回→0x0C warm、両plane明示再投入。不要待機とpower lifecycleを分離 | 同じOTP full初期像から、固定領域のold/new bits・00 classと光学濃度を追跡。不一致が見つかった場合だけ状態遷移を最小修正 | 新規LUTなし。既存baseline/doseを同じ画像・温度・回数・撮影条件で比較し、初回黒化と保持を分離 |
| レイヤー | Driver / Service（別タスク） | まずHost / 診断。必要ならDriver / Service | 既存Driver選択＋診断のみ |
| 改善対象 | 濃度・残像、OTP待機時間 | 初期像・旧画像不一致による濃度／残像 | 初回濃度、保持時の読みやすさ、消去残り |
| 速度影響 | 転送・待機削減余地はあるが、過去のactivation約559msからfastest級は期待しにくい | 正常経路なら速度を変える理由なし。毎回fullは不可 | 過去のcycleは約110msでほぼ同じ。Writer p50は+3.08%、新規同条件測定は必要 |
| DC balance | OTP内部内容は不明。warmで連続使用した適合性は未確認 | LUTも遷移への意図的な補強も変更しない | 目的色期間増加による作用変更。未確認でリスクあり |
| 安全性の根拠 | datasheetの更新命令、公式OTP経路。warmの品質・寿命はDemoでは保証されない | completed snapshotと正しいbaselineの既存設計・Host試験。光学履歴は要測定 | 短い目視試験のみ。公的な安全性保証はない |
| コスト | 中 | 小（切り分け）。不一致原因によって修正費用は変わる | 小（既存選択利用） |
| 検証 | 初回1＋warm最大2の時間測定を入口とし、速度不達なら終了。実装前に状態テストを追加 | 同一初期黒の下で、固定・変更領域を別々に撮影しHostの分類と照合 | 次節の最大10更新の短いA/B |
| 推奨 | 現行OTPをfastest代替として採用するのは**NO-GO（速度）**。warm制御自体は追加判断が必要 | **GO（診断のみ）**。不具合が見つからないまま実装変更する案は採らない | **CONDITIONAL（限定比較）**。正式採用は現状NO-GO |

Fontや線幅を増やす案は光学濃度を上げる方法ではなく、既存140% UI/Zoomの変更にもなるため今回は候補にしない。保持黒を白→黒扱いにして毎回再駆動する案も、LUTを変えなくても電気的な作用・DC履歴を変えるので「安全なデータ側改善」とは扱わない。新しいholdパルス、独自電圧、頻繁なfull、自動pacingは採用しない。

## 6. 次に依頼する限定実験

推奨する依頼名は **「既存baseline/doseの初期濃度と保持濃度を分離した短期A/B」**。案2の分類確認を入口にし、案3を評価する。新規LUT、boot default、UI、Font、SPI/tile、async、保存データは変更しない。既存比較版を使えるならflash不要。実機更新とdoseでの追加試験は、今回の報告後にユーザーが別途判断する。

### 実験条件と実施上限

1. **初回黒化の比較**: 同じ白画像をOTP cleanupで表示して完了を確認→選択波形で同じ固定文字・線・黒patchを描く→小markerだけ9回更新。baseline/dose各1バッチ、各バッチは初回を含め10高速更新まで。
2. **保持そのものの比較**: 同じ文字・線・patchの画像をOTP fullで表示して完了を確認→初期濃度を撮影→固定画素のbitsを変えずmarkerだけ10回更新。baseline/dose各1バッチ。これによりdoseで最初から濃く描いた効果を分離する。
3. 4バッチの前後・間に、観察者が確認する診断用OTP cleanupを行う。各バッチは無人で走らせず、初期像と終了像で停止する。10更新は管理上の上限であり、候補の安全回数の証明ではない。通常アプリのfull周期へ反映しない。
4. 比較順序による差が疑われる場合だけ、結果を提示して順序を逆にした追加比較を判断する。30/100回へ自動延長しない。保持への効果が曖昧なら面積・遠近の追加試験に進まず、結論を保留してこの実験を終了する。

Font/Zoom、800×480像、40MHz/tile、更新位置・面積・回数を固定。照明・露出・ホワイトバランス・撮影位置を固定し、可能なら室温を記録する。表示を読み取る写真が取得できなければ、目視の結果を定量測定と呼ばない。初期全体画像と固定／変更領域を分け、near/farの固定patchを同じ画面に置く。面積効果を追加するのは最初の比較で保持差が確認された場合だけ。

### 測定と判断

- Hostでは同じ画像列に対する両RAM payload、fixed ROIのold/new、class 00の継続、変更画素数、snapshotの昇格時点を確認する。強制反転や画面カウンター等で固定領域を変えない。
- 写真では同じROIの白地Wと黒patch Bから相対contrast `C=(W-B)/W`を記録し、初回C、更新後C、保持比C10/C0を比較する。これは校正済み光学濃度・寿命測定ではない。細いglyphは黒patchとは別に評価する。
- 濃度、消去残り、ちらつき、欠けと、Driver時間・BUSY待機時間・2 plane変換・SPI時間を記録。coldとwarm、異なるbounds、診断終了後の再描画を分離する。
- Writer計測が必要なのは、診断で品質差が出た後。新しい同条件baselineを基準に、過去試験で使ったp50 +10% / p95 +15%以内を速度目安とする。旧160msを新しい合格値に流用しない。小標本のp95は探索値として扱う。
- 同じ初期黒の保持比がbaselineと差なしなら、doseは主に初回黒化の改善候補として結論を限定する。保持比の改善が出ても、隣接駆動への作用を断定せず、再現性の確認対象とする。
- 濃度差が観察誤差以下、速度目安に不達、強いちらつき・背景汚れ・読みづらさが出た場合は打ち切る。BUSY/transport異常では自動再試行をしない。

この短い実験で分かるのは「品質改善を再現できるか」と「速度を保てるか」まで。DC balance・長期寿命は未検証のまま残る。非公開LUTの入手待ちで調査を延長せず、差が確認できなければ新規波形設計へ進まない。

## 7. 今回の検証結果と完了判定

- 110-byte静的比較・decode: PASS。baselineのM5GFX/Writerdeck一致、dose差分2 bytes、保持黒/保持白/VCOMの設定期間一致、hold差分1 byteを確認。
- 過去17ログの再解析: PASS。適切な診断範囲に限定した結果は全て既存JSONと一致。
- Host `epd_ssd1677_test` / `epd_ssd1677_waveform_trial_test` / `display_async_test`: 今回再実行して全てPASS。plane、immutable snapshot、失敗時shadow不昇格、mode/cleanup、executor置換等を検証。光学濃度の証明にはしない。
- Python `test_fastest_quality_*.py`: 今回11件PASS。
- 新しい実機測定・写真・温度比較・DC積算・寿命試験: **未実施**。
- production source / startup / waveform実験履歴 / ユーザーデータ: **変更なし**。今回の成果物はこの報告書。

取得資料、pinしたソース、静的解析、再集計JSON、実行ログは`.research/quality-reevaluation/`に保存した。`downloads*.json`はURL/ファイルhash、`static-waveform-audit.json`は実配列のdecode、`log-audit.json`は原ログhashと集計の一致を記録する。資料PDFは再取得した同一hashのものと、既存のレンダリング画像および追加レンダリングで照合した。

Task 1の完了条件は満たした。**次へ進む判断はCONDITIONAL**。実装変更前に、既存doseの有望な観察を初期濃度と保持比に分けて検証するのが最も小さい次タスクである。公式OTP経路を優先して確認したが、現状ではfastest速度を満たす改善策としては採用できない。新規LUT設計やメーカー資料待ちの無期限調査、別タスクのSketch高速化には進んでいない。
