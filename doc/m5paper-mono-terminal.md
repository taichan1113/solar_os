# PaperMono standalone terminal UX

後続BLE securityフェーズでは、既存SM procedureを重複要求で削除する原因を修正し、
USBなし・同bondのcomplete power cycle / 後からkeyboard ON / OFF→ONを各5/5確認した。
3分不在からの復帰と文書・設定・bond保持も確認済み。
詳細は[m5paper-mono-ble-security.md](m5paper-mono-ble-security.md)を参照。
以下のBLE未解決という記録はterminalフェーズ時点の調査結果として残す。

現在の実機設定（2026-10-07）：ユーザーの追加依頼により、標準の
`/.shell/startup` で `refresh=fastest` を起動時に選択するよう変更済み。
再起動後のmodeと40MHzでの実更新を確認した。下記の実装前レビューにある
AUTO維持は当初の要件・driver初期値を指し、現在の運用設定はこの追記を優先する。

## 提出前の実機確認状況（2026-10-07）

以下は後段の追試を含む現在の状態。履歴中の「未実施」はその記録時点を示す。

| 項目 | 現在の確認状況 |
|---|---|
| USBなし・ZMK OFFでShell起動、後からONで入力 | software rebootと完全電源OFFからの起動をユーザーが確認。後からONにしたZMKは認識する場合としない場合があり、接続の信頼性は未合格。 |
| Shell文字サイズ・欠け | mono18pxをユーザーがひとまずOKと確認。拡大UIの目視確認もOK。14/16/18pxの全候補比較は未実施。 |
| Shell/WriterのKEY1停止・復帰、KEY2、保存・切替 | 実キーで確認済み。前のShell出力、Writerの記号行・K・z4を保持。 |
| 起動時fastest | 設定、再起動後のqueryと40MHz実更新、ユーザー観察で確認済み。 |
| Writer新規入力の正確な保存・再読込 | 新規の短い複数行文書を入力・保存・終了・再度開く手順で、全文一致をユーザーが確認。初回SolarOS JPが保存本文にない理由は未特定。 |
| 拡大UIの目視 | help/manの長い出力、Writer dialog、launcherの欠け・重なり・はみ出し確認をユーザーがOKと報告。 |
| fastestの残像 | 残像はユーザーの許容範囲。full cleanupで白背景が正常になることも確認済み。 |
| BLE再接続 | 成功例と失敗例があり、キーボードOFF/ONで復旧。信頼性改善済みとはしない。 |

後続の[BLE再接続診断](m5paper-mono-ble-reconnect.md)で、検出・GAP接続後のsecurity待ち失敗を取得した。
独立したworker終了競合の修正、host検証、登録と文書の保持確認まで実施済みだが、
securityの主因とUSBなし5回の信頼性acceptanceは未解決。

## Task validity review（実装前、2026-10-07）

起動経路を調査すると、mainは標準でshell appを選び、sessions initが
board displayへ入力focusを自動設定する。実機の `/.shell/startup` は
`session create shell cdc0 --term dumb --size 80x25` の1行のみ。
今回の調査時点でもsession 0はdisplay0 Shell、session16はcdc0 Shell、
focusはdisplay0だった。前フェーズでWriterをforegroundにしたのは
診断用のruntime session操作であり、board/flavorの起動既定ではない。
したがってShell専用bootstrapやPaperMono分岐を追加せず、標準の起動と
現行startup scriptを維持する。USB shellはlocal display focusを奪わない。

BLE initはバックグラウンドの接続処理へ委譲し、peer接続待ちは
Shellの起動経路に含まれない。既存の1 remembered peer方針を維持する。
Alt+Tab等の切替と終了後のShell復帰も既存sessionsの責務であり変更不要。

フォント所有は2系統：terminalがShell・TUI・files・less・help/manと
共通terminal popupを描画し、gfxがWriterやlauncherを描画する。
Writer本文はdoc layoutの独立した12/14/16/18/20pxのzoomを使う。
U8g2には全サイズとregular/bold/italic/bold-italicのbitmap fontが既にある。
現行PaperMono Shellはcompact/textsize16で、実fontは14px。
graphicsのWriter titleは12px、dialogは12/14px、launcher labelは12/14px。

採用設計はgeneric compile-time UI font preset。normalは現行寸法・faceを
保ち、readable/largeを追加。PaperMono manifestのみlargeを指定する。
Shellの保存済みfont/textsizeはpreset既定より優先し、既存settermの
display限定・一時設定で同一firmwareの14/16/18pxを比較できる。
全gfx enumの意味を一括で変える方式は、doc本文と固定寸法の別appを
壊しうるため採用しない。明示サイズを維持し、Writer UIとlauncherに
役割別のfont/geometryを適用する。TUI類はterminalのgeometryを継承する。
固定寸法のmedia player widget等を一括改修することは今回の範囲から除く。

Shell cursorは静的な下線で、tick点滅はない。一方、status barの時計・
BLE scanning変化はdirtyを立てるため、入力なしでも更新を起こしうる。
PaperMono manifestからgeneric terminal status refresh policyを選択し、
statusの最新値は保持するが、それ単独では再描画を要求しない。
trade-offはstatus表示が次の入力・再描画まで古いままになること。
他boardのstatus更新は従来どおりで、display backend/波形/電源には触れない。

ユーザーは「実装と自動検証を先に進める」を選択。
光学的なサイズ選定、ZMKの後からON、USBなし運用、物理KEY操作は
自動検証と区別し、観察済みとは記載しない。

## 実装・検証記録

### 起動・session（最終報告 1–6）

before/afterとも、標準mainのboot完了後はdisplay0のShell（session 0）と
cdc0の独立Shell（session 16）。startupは上記1行を保持し、追加bootstrapは
作らなかった。標準sessions initがdisplay0へfocusを設定するため、手動の
`session focus display0` は不要。USB Shellを作成してもfocusはdisplay0のまま。

app-only更新後、COM39で再起動を記録した。BLE scan開始は7894ms、runtime
readyは7898ms、Shellへのswitchは7914ms。keyboard未接続でShell起動を
完了し、Writerは自動起動しなかった。background scanはその後も継続。
USB診断Shellからsession/focus/JP/files/powerを照会できた。
これは「peerが未接続のboot」のログ検証であり、ZMKの物理OFF操作、
後からONによる接続、USBを抜いた運用、表示の目視確認とは区別する。

### Font ownership・preset（最終報告 7–14）

`src/services/solar_os_ui_font.h` にnormal/readable/largeを定義した。
既存bitmap fontと役割別geometryを選択するcompile-time設定で、
`SOLAR_OS_BOARD_UI_FONT_PRESET` 未指定ならnormal。
PaperMono manifestが `SOLAR_OS_UI_FONT_PRESET_LARGE` を選び、
bringupもmanifest継承で同じ設定となる。他boardには設定を加えない。
apps内でboard名を比較する分岐はない。

| Preset | Shell実font | 小さいUI | Writer title/status | heading | status高さ |
|---|---|---|---|---|---:|
| normal | compact/textsize16 → regular/bold/italic/bold-italic 14px | regular12 | regular12 | bold14 | 16px |
| readable | mono/textsize16 → 各style16px | regular14 | regular16 | bold16 | 20px |
| large | mono/textsize18 → 各style18px | regular16 | regular18 | bold18 | 24px |

UI body/bold/dialog actionはnormal/readableが14px、largeが16px。
choice dialogのtitleは14/16/18px。fontは既存
`u8g2_font_solar_os_default_{r,b,i,bi}_{size}_tf` を使い、font assetの追加はない。
gfxの明示サイズenumとdocument fontの意味は変更しない。

| PaperMono UI | Before | After（large） |
|---|---:|---:|
| Shell本文 | compact実14px | mono実18px |
| Shell cell / line height | 6×15px | 9×18px |
| Shell columns / rows | 132×30 | 88×25 |
| Shell status font / height | 12px / 16px | 16px / 24px |
| Writer本文 | zoom4 / 20px | zoom4 / 20px、独立維持 |
| Writer title/status font / header | 12px / 16px | 18px / 24px |
| Writer choice dialog title / action | 14px / 14px | 18px / 16px |
| Writer input・format dialog | 12px | 16px |
| Launcher label / selected label | 12px / bold14px | 16px / bold18px |
| Writer本文viewport（幅×高さ、y） | 788×458px、y=19 | 788×450px、y=27 |

Writer headerは実text widthで長いタイトルをUTF-8境界まで短縮する。
dialogはfont拡大分に応じて高さ・行間を増やし、launcherもlabel領域を増やす。
normalでは従来geometryとfont選択を保持する。
terminalのTUI/files/less/help/man/common popupはShellのfontとrows/columnsを
継承する。固定寸法のmedia widgetや各appの明示fontを全面的に置換する
変更は行わなかった。共通roleを導入する範囲はterminal、Writer UI、launcher。

PaperMonoの採用既定はlarge。コード上の候補比較とreal U8g2 bitmap previewを
確認したが、ユーザーの実機可読性選定は未完了なので、この値は目視確認前の
実装既定として扱う。Shell previewのprompt、静的cursor、status、本文に
800×480からのclippingは認めなかった。実パネルの残像・色・光学的品質と
Writer/launcher/dialogの全画面の目視評価は未検証。

同一firmwareで既存の一時設定を使って候補を比較できる。NVSへ保存しない。

```text
# current相当（実14px）
setterm --display display0 font compact
setterm --display display0 textsize 16
# +1 step（実16px）
setterm --display display0 font mono
setterm --display display0 textsize 16
# +2 step / 今回の既定（実18px）
setterm --display display0 font mono
setterm --display display0 textsize 18
```

実機で3候補とも設定照会、local Shellのclear/help writer/version、
help TUIのapp-exitを実行した。runtime比較中の共通UIはlargeのまま。
完全なpresetごとのhost描画では132×30 / 99×27 / 88×25。
同一firmwareでShellのみ16pxへ変えた場合はstatus高さ24pxのままなので99×26。
保存済みterminal font/textsizeがあればpresetより優先する。今回の実機には
terminal NVS namespaceがなく、既存設定を書き換えずlargeを適用できた。

Writer zoom0..4、PaperMono default4、saved/recovery zoom優先は変更していない。
JP layoutでは既存のJIS mappingに従い、zoom inはCtrl+; / Ctrl+Shift+;、
zoom outはCtrl+-。US配列のCtrl+=という印字と区別する。

### 待機表示と通常操作（最終報告 15–18）

PaperMonoだけ `SOLAR_OS_BOARD_TERMINAL_STATUS_REFRESH_ON_CHANGE=0`。
clock/BLE scanning状態は最新値を保持するが、それだけではdirtyにしない。
静的underline cursorは従来どおり。入力・再描画・resume時はstatusも更新する。
trade-offは、接続アイコンや時刻だけが変化しても画面の表示値が次の操作まで
古いこと。他boardのlive status更新は維持する。

AUTO/OTPで約90秒間、driver timingを有効にして無入力を記録し、
背景BLE scan 7回、物理refresh 0回だった。hostのreal U8g2描画でも
120回のclock/BLE状態変更でlarge/readableの追加presentは0、normalは120。
次の入力で1回presentし、同一frame再描画では0、resume invalidationで1回。
続いてruntimeをfastest/40MHzにした状態でも約90秒を測り、背景scan 7回、
物理refresh 0回だった。診断後はtiming off / log info / CDC log sink offへ戻す。
実行時のrefreshはopt-inのfastestを保つが、再起動時は従来のAUTO/OTP。

実機local Shellへ標準 `session send 0` でstatus/version/board/commands/apps/ls/
session list/mem/ble status/power statusを送信できた。出力先はdisplay0で、
CDC側の応答は送信成功を確認するもの。表示内容すべての目視検証ではない。
`writer /transport-test.md` でsession 1が起動し、parent Shellがsuspendedに
なった。USBの `input emit ALT+TAB / ALT+LEFT / ALT+RIGHT` でShell⇄Writerを
各回切り替え、focusはdisplay0を保持。標準の合成入力による検証で、実ZMKの
キー操作とは区別する。最後のapp-exitでWriterが閉じ、Shellへ復帰した。

Writerを開くと残っている未保存checkpointの `Recovery found` が表示された。
その確認中はapp-exitで直ちに終了しない。Escで質問を取り消してからapp-exit
すると閉じる。復元・破棄・保存の選択は行わず、未保存checkpointを保った。
初回スクリプトの「直ちに閉じる」「checkpointはsaved=true」という前提は
この状態に適合しなかった。診断手順を修正し、ファームウェアの終了挙動は
変更していない。

同一Writer session IDは切り替え中に保持された。既存本文とzoom記録の保持は
読み戻しで確認したが、cursor/scrollの実画面とZMK編集・Ctrl+Sの確認は未実施。
power/KEY1/KEY2処理は変更せず、host power回帰は通過。Shell/Writerそれぞれの
物理KEY1 suspend/resumeおよびKEY2は今回未操作。
BLE scan/reconnectのhost回帰は通過し、実機でも背景再接続処理を確認したが、
後からZMKをONにして接続・入力する最終確認は未実施。

### データ保持・artifact・回帰（最終報告 19–24）

COM39のota_0/app領域0x10000だけを書き換え、esptoolのhash確認成功。
partition/FAT/NVS/bondをerase・flashしていない。boot後もshared keyboardと
Python BLE layoutはjp、remembered keyboardは1/1、power keyはsuspend。
rootの既存文書一覧とサイズも保持している。

`/transport-test.md` は173 bytesで前フェーズの保存本文とbyte単位で一致。

```text
a82889b15c8d7078570a9de577afc29f9a927742c8bdb6c46b1621073e78eb69
```

今回読み戻した対応checkpointは200 bytes、zoom4、saved=false、本文0 bytes。
有効な「空の未保存snapshot」であり、古い検証fixtureのsaved=trueとは異なる。
Escで質問を取り消してWriterを閉じた後も、このcheckpointの全byteが一致した。
過去のsaved=true fixtureへ強制復元する操作はしていない。

| Build size | Before（JP phase） | After | Delta |
|---|---:|---:|---:|
| static RAM | 52,464 bytes | 52,464 bytes | 0 |
| app flash（PlatformIO） | 3,319,163 bytes | 3,317,927 bytes | -1,236 bytes |

runtime heap/peakの条件をそろえた前後比較は未計測。
最終通常firmwareは `m5paper_mono/writerdeck/ota`。

- BIN: `build/papermono-terminal/firmware.bin`（3,323,712 bytes）
- ELF: `build/papermono-terminal/firmware.elf`
- SHA256: `1fcb5de9b13f517e9f2463b79fd921d63716383d9f05b8b44811e66ec3bab7de`

Pythonのboard manifests/BLE TUI/input actions/script bindingsは54 tests通過。
hostは次の17 binariesが通過した：doc_layout（zoom0..4）、writer_files、
writer_checkpoint（saved/recovery）、shell_startup_policy、shell_launch、
shell_line、shell_parse、app_exit_result、terminal_preferences、terminal_geometry、
input（JP/US/DE）、hid_keyboard_report、ble_keyboard_scan_policy、ble_hid、
m5papermono_power、display_async、epd_ssd1677。
追加したreal U8g2 terminal_renderはnormal/readable/largeの3 binariesが通過。
normalのbitmapは変更前HEADのterminal renderer/preferencesで同じ入力を描いた
baselineと全pixel一致した。新fontでのlayout、idle status、同一frame、resume
invalidationを実rendererで検証した。既存明示gfx font/Writer bodyは変更しない。
通常ビルド成功、`git diff --check` 通過。

主要記録：`.research/terminal-boot.log`、`terminal-idle.log`、
`terminal-font-basic-hardware.log`、`terminal-ux-hardware.log`、
`terminal-retention-pass.log`、`terminal-final-retention.log`、
`terminal-fastest-idle.log`、`terminal-final-build.log`。
画像preview：`.research/terminal-normal.png`、`terminal-readable.png`、
`terminal-large.png`。中間診断スクリプトのassertion失敗は、session一覧のtitle列、
help TUIの終了待ち、recovery確認と古いsaved fixtureを扱う検証手順の修正記録。
最終の成功ログと区別する。

### 自動検証終了時点の未確認事項と引き継ぎ（最終報告25・履歴）

実装・ビルド・自動回帰・上記のUSB経由実機診断を完了した段階。
「Shellが起動した」だけでstandalone運用の最終acceptanceを完了とはしない。
ユーザーが観察できる次回に、以下を確認する。

1. USBを外し、ZMK OFFで起動→Shellの表示→ZMKを後からON→自動接続とShell入力。
2. 14/16/18px候補を実パネルで比較し、help/manの長い出力、prompt/status/cursor、
   Writer/dialog/launcherのclipping・overflow・見た目を確認して最終サイズを選ぶ。
3. ZMKでWriter起動、JP記号、編集、Ctrl+S、Alt切り替え後のcursor/scroll/zoom保持。
4. ShellとWriterでKEY1短押しsuspend/resume、KEY2 Enter、session保持。

現在のRecovery foundは、ユーザーが復元・破棄を判断する状態のまま保持する。
停止時のざらつき、BLE再接続の信頼性改善など前フェーズの課題は今回の対象外。
LUT/SPI/transpose/async/電源/入力mapping/起動AUTO方針には今回の変更を加えていない。

### 実機確認の続き（2026-10-07）

ユーザー復帰後、ZMK travelers-splitは自動接続済み（1 remembered peer）。
display0 Shell/focus、mono18px、JPをログで確認。ユーザーがZMKからversionとlsを
実行し、入力成功、文字サイズはひとまずOK、表示欠けなしを確認した。
ただし白地にゴーストがあり、少し見づらいとの報告。発生時はfastestだった。
Shellの可読性評価にはこの残像を含め、全面的な表示品質合格とはしない。

既存のrefresh=cleanupを要求し、一時的なSPACE入力でShell frameを再送した。
OTP fullはerr=0、4,519,978usで完了（`.research/terminal-ghost-cleanup.log`）。
cleanup後、ユーザーが「背景はとてもきれいな白」と確認した。
fastest実行中に生じたゴーストはfull cleanupで除去できたが、通常Shell操作を
続けたときの蓄積・許容範囲は引き続き確認する。定期cleanupや波形の変更は
追加していない。prompt末尾の一時SPACEは目視確認後にBACKSPACEで戻した。
波形・refresh既定方針は変更していない。

ユーザーがZMKで `writer /terminal-check.md` を起動し、SolarOS JPとJIS記号の
入力、左右カーソル、Ctrl+S、Alt+Tabの往復を実施し、1〜4の手順に問題なしと
報告した。カーソル操作、保存、切替、Writer titleの可読性の確認に進展があった。
一方、SolarOS JPが自動選択されたように見えるとの観察があり、実選択か残像か
は未確定。USBで保存文書を読むと `@:;+^~[]{}` の記号行だけで、SolarOS JPは
残っていなかった（`.research/terminal-manual-writer-saved.log`）。そのため元の
2行が保持されたというacceptanceは未完了とする。full更新後にも選択表示が
残るかを次のsuspend/resume確認で観察する。

続くWriterのKEY1短押し停止・短押し復帰で、suspend/resume両方のログを確認。
resume後のOTP fullはerr=0、4,524,349us。ユーザーが「選択らしい表示は残らず、
記号行は保持され、KEY2でKが次の行に出た」と確認した。選択らしい表示は
full更新で除去されることが分かり、残像の可能性が高いが、初回のSolarOS JPが
保存本文にない理由までは確定していない。正常な範囲選択とは区別する。
Writer session 1と既存power profileを保持して復帰した。
記録：`.research/terminal-manual-power-writer.log`。
USBの保存本文読み戻しでも記号行とKを確認した。BLE最終キーはCtrl+Sに対応する
key=19で、release時のmodifiers=0。保存操作とCtrl解放も確認できた。
停止時のCPU power-down設定診断は前フェーズにも記録された既存事項であり、
今回のKEY1停止・復帰とfull更新は成功した。実際の消費電流やCPU power-downの
改善・検証は今回の範囲に含めない。

既存の手動full更新方法：Shellで `display mode display0 refresh=cleanup` を
実行すると、次のpresentだけOTP fullとなり、fastest設定自体は維持される。
Writerで画面を保って行う場合は、KEY1短押しでsuspendし、再び短押しでresumeする。
resume後の再描画はbaselineが無効なのでfull更新になる。短押しは約0.8秒、
長押し判定の1.2秒未満にする。今回、新しいshortcutや自動cleanupは追加しない。

ShellでもJP Ctrl+]によるWriter終了・Shell復帰を実キーで確認。KEY2でversionと
boardを実行し、KEY1短押し停止・復帰後も前の出力が保持されたとユーザーが
確認した。ログでもsession 1のclose→Shell復帰、suspend/resume、OTP full
err=0 / 4,520,368usを確認した。記録：`.research/terminal-manual-power-shell.log`。

KEY1の10秒待ちは検証手順の余裕であり、固定の仕様ではない。
コードの短押し判定は30ms以上・1200ms未満で、通常のsuspend/resumeには
押下間隔の固定タイマーはない。今回、PM低電力切替開始からsuspendedログまで
約197ms。ただしその前のcheckpoint drainと進行中displayの完了待ちは文書・
状態で変わる。画面更新停止後なら1秒程度を待ち時間の試行目安にできるが、
実測済みの最短間隔・保証値ではない。復帰後に再度停止する場合は約4.5秒の
full更新終了を待つ。KEY1判定やpowerの実装は変更していない。

USBなしの再起動・ZMK OFF時のShell表示・後からONの自動接続を最後に確認する。
COM39の記録を終了し、runtime timing off / CDC log sink offへ戻した。
保存したテスト文書とJP、display0 focus、remembered 1/1を直前に確認済み。

ユーザーがUSBを外したままShellのreboot入力→ZMK左右OFF→KEY2で再起動を実施。
「ZMKがOFFでもShellが起動し、後からONにして入力できた」と確認した。
USBなしの再起動、keyboard未接続のShell表示、後からの接続と入力focusは
実機観察で確認できた。これはバッテリー上のsoftware rebootで、電源を完全に
切ってからのcold power-onとは区別する。USB切断状態はhostでもCOM39の消失を
確認した。USB shellへ依存しない主要なstandalone動作の確認に進展した。

再起動後に文字の更新が以前より遅く感じるとの報告。再起動前はruntimeで
fastest/40MHzを明示選択していたが、driver initは従来どおりrefresh=auto、
board clock=10MHzに戻る。この設定差による遅さと考えられ、runtime opt-inで
`display mode display0 refresh=fastest` を再実行して比較する。boot AUTO/OTPの
維持は今回の明示要件であり、startupをfastestへ変更したりNVSへ保存したりする
修正は行わない。切替直後はbaseline作成のfull更新が入るため、以降の更新で比較する。

ユーザーがruntimeでfastestを再指定し、「前の速さに戻り、WriterでKとz4が
残っている」と確認した。USBなし起動後の速度差はmode差で説明できること、
再起動後のテスト文書・zoomの保持も確認できた。

続いてユーザーがboot時のfastest既定を希望したため、以前のAUTO/OTP維持要件は
この実機の運用設定について更新された。標準 `/.shell/startup` の既存USB Shell
作成を保持し、`display mode display0 refresh=fastest` を追加する方法を採用する。
driverの初期modeや他board、波形、SPIの実装は変更せず、初回baselineはfullで作る。
変更前startupを読み戻してバックアップし、追記・readback・再起動後のmode確認を行う。

### 起動時fastest設定の反映結果（2026-10-07）

COM39再接続後、実機の `/.shell/startup` を以下へ変更した。

```text
session create shell cdc0 --term dumb --size 80x25
display mode display0 refresh=fastest
```

既存のUSB Shell作成行はbyte単位で保持した。新しいファイルをstagingへ書き、
readbackを確認後、元ファイルを `/.shell/startup.before-fastest` へrenameし、
stagingをstartupへrenameした。両ファイルのreadbackも一致。
ホスト側にも `build/papermono-terminal/startup.before-fastest.txt` と
`build/papermono-terminal/startup.fastest.txt` を保存した。
設定ツール `.research/hardware/terminal_boot_fastest.py` は再実行時の重複追記を避け、
既存backup/stagingを上書きしない。最初の試行は実機にos moduleがないためimportで
停止し、書き込み前だった。標準solaros.storage APIへ修正した後の試行は成功した。

保存済みWriterを標準app-exitで終了し、Shell復帰を確認してから再起動した。
再起動後はUSBからrefresh設定コマンドを送らず、queryだけで
`mode: refresh=fastest` を確認した。Shellはsession 0/display0でactive、
USB Shellはsession 16/cdc0でactive、focusはdisplay0。
font=mono/textsize=18/orientation=270、keyboard=jpを保持し、
ZMK travelers-splitは1 remembered peerのまま自動接続した。

標準 `session send 0 version` で表示を更新すると、SPI requested/actualとも
40,000kHz、tile path、REF err=0、cold=1、total=454,333us、BUSY=133,728usだった。
これはcold更新の実測であり、通常更新すべての所要時間を保証する値ではない。
初回の基準画像作成は既存のOTP fullのまま、以後の更新にfastestが適用される。
ファームウェア、driver、波形、NVSの実装は変更していない。
起動スクリプトはFAT上の設定なのでappのみの更新では保持されるが、
ストレージを初期化する場合には同じstartup設定を再導入する必要がある。

設定変更前後で `/transport-test.md` は173 bytesのまま全byte一致。
`/terminal-check.md` は13 bytes（記号行・空行・K）、SHA256
`b8b27318e0c81c44b2fca2c16e86f22926f3a584e9eb16cdc63e4e4992ed844f` のまま。
再起動後も旧文書とその200-byte未保存checkpointを保存済みhost referenceと比較し一致した。
最後にtimingとCDC log sinkをoffへ戻し、Shellを操作できる状態でCOM39を解放した。

元の起動設定へ戻す場合は標準Shellで以下を実行し、再起動する。

```text
cp /.shell/startup.before-fastest /.shell/startup
reboot
```

主な証跡：`.research/terminal-boot-fastest-config-result.log`、
`terminal-boot-fastest-boot.log`、`terminal-boot-fastest-display.log`、
`terminal-boot-fastest-retention-result.log`、`terminal-boot-fastest-final.log`。

### 再起動後のBLE接続不成立の報告（2026-10-07）

その後のユーザーによるrebootではfastestは適用されたが、BLEが接続できなかった。
キーボードを何度かOFF/ONすると接続が成立したとの報告。USB診断の開始時点には
すでに `connected travelers-split` / remembered 1/1となっており、24秒の記録中も
接続状態を保持した。失敗時点のログは採れていないため、接続不成立の原因や
fastest設定との因果関係は未特定であり、BLE再接続問題を解決済みとはしない。
bond消去・再pairing・ファームウェア変更は行わず、診断終了後はCDC log sinkをoffへ戻した。
証跡：`.research/terminal-ble-reboot-issue.log`、`terminal-ble-reboot-recovered.log`。

### 提出前の実機追試結果（2026-10-07）

ユーザーが以下の手順を実施し、結果を報告した。今回の追試はユーザーの
目視確認であり、追加のUSBログ採取やファームウェア変更は行っていない。

1. 新規の短い文章を2〜3行入力し、Ctrl+Sで保存、Writer終了後に再度開き、
   全文一致を確認：OK。新規複数行文書の保存・再読込の確認を完了とする。
   以前のSolarOS JPが保存本文にない原因は、この追試だけでは確定しない。
2. help/manの長い出力、Writerダイアログ、Launcherの文字の欠け・重なり・
   画面外へのはみ出し確認：OK。拡大UIの目視確認を完了とする。
3. USBなし・ZMK OFFで完全電源OFFから起動し、後からZMKをONにする確認：
   ZMKが認識される場合とされない場合がある。接続の信頼性は未合格とし、
   BLE再接続の不安定さを既知の未解決事項として残す。

残像については「許容範囲」との報告。現行サイズとfastestの運用表示を
ユーザーが受け入れたものとして記録する。残像がゼロであるという評価ではない。
提出に必要な文書保存と拡大UIの確認は完了し、BLE接続の信頼性に制限がある
報告書として提出できる。BLEの原因調査・改善は別途必要。
