# M5Paper MONO 本採用台帳（2026-10-27 更新）

## 今回の確定事項（2026-10-27）

Sketch候補は **B：過去の全画面描画のみ**。局所描画高速化は現行に取込済みで、独立した高速化候補は未発見。既存の全画面fallback/localをHost比較し、24描画のCanvasと合成XBMが一致、XBM出力量865,536→37,771 Bを確認した。これはSPI量・実機遅延の測定ではない。現行local＋Fを維持し、形式的なruntime selectorは追加しない。下記履歴中の「selector必須」はこの調査結果で更新する。

ShellはF既定のHelp/Statusへ修正、writer-lut aliasは互換・非推奨として非表示化。A/F復旧・Trace・OTP cleanup・Display Asyncを維持。研究コマンドの既存Build guardを確認し、大量削除はしない。ユーザーによるstartup4行削除・正常rebootは完了扱いで、今回本体startupは読書きしていない。

今回の報告は[Sketch比較・コマンド整理](m5paper-mono-sketch-render-command-cleanup-20261026.md)。既存の実機Acceptance結果は[再Acceptance報告](m5paper-mono-production-fixes-reacceptance-20261026.md)を参照し、以下の実装時点の「実機未確認」と混同しない。今回の作業ではFlash・実機評価・SD保存FAIL修正は行わない。

## 統合実装時の記録（2026-10-26）

今回の統合実装依頼に従って更新。**実装・Host・Build完了、実機Acceptance未実施。** 根拠・Commit・成果物は[統合実装報告](m5paper-mono-production-integration-20261026.md)。実機と既存Integrationは変更していない。

| ID | 現在の実装・採用状態 | 検証 |
|---|---|---|
| EPD-01/02 | F `00 C0` 全App、fastest/tile/20MHz/rows-dual/32周期/Async、A/F復旧 | Driver/Async Host PASS、Build SUCCESS。光学・寿命未確認 |
| TM-01 | 現行42キー/固定/反転/HIDを維持、新Frame案保留 | Engine/lifecycle回帰 PASS |
| SK-01 | **今回確定：現行局所描画を維持してF適用**。方式比較/selectorは次回 | Pipeline/Canvas/PNG回帰 PASS |
| BOOT-01/USB-01 | USB Shell自動、cdc0再利用、startup実効Flash | Boot mock/保存先Host PASS、実再接続未確認 |
| NET-01/TIME-01 | 保存OFF尊重のWi-Fi start/connect、NTP/AP/repeater非自動 | Boot Host PASS、実機startup整理は未適用 |
| SH-01 | 空Esc→Launcher分岐のみ削除、取消/明示launcher/他App維持 | 差分確認・Shell回帰、実機未確認 |
| UI-01 | 2行×3列 `[Manual,Reader,Wi-Fi;Keyboard,Sketch,Clock]` | Grid/JSON非破壊/起動先Host PASS |
| UI-02 | 既存Manualへtap/fold/swipe/Reader復帰/lease/CANCEL | 実App Host PASS、実タッチ未確認 |
| SD-01 | 既存SDMMC 4bit＋IOE電源/検出、電源保持Sleep/Wake、旧1bit維持 | Service mock Host PASS、実カード未確認 |
| SD-02 | Flash `/`、SD `/sdcard`、保存NVSを消さずstartup実効Flash | 保存先・SDエラー時Boot Host PASS |
| DOC-01 | SD状態変更で署名index再評価、update/read競合拒否、旧索引保持 | 実Docs Host PASS（暗号primitiveはstub） |
| DOC-02 | 独自版の正規署名配布は次回 | 配布元/鍵/OTA/検証方式は変更しない |

## 設計時の記録（2026-10-25、実装前の履歴）

以下の「未実装」「設計のみ」は当時の記録。現在は上表と統合実装報告を優先する。元Worktreeの原本は変更していない。

# M5Paper MONO 本採用台帳 — 追加要件版

更新：2026-10-25 JST。**採用方針・設計の記録であり、実装済み一覧ではない。Firmware変更、Merge、Build、Flash、USB/COMアクセスは今回行っていない。**

このローカルRepositoryの既存WorktreeとoriginのBranch一覧を調査したが、前回の「本採用機能の棚卸し」に対応する専用台帳を特定できなかった。既存の`m5paper-mono-platform-acceptance.md`は2026-10-10のPlatform試験記録であり、上書きしない。本書を今回追加要件の補完台帳とする。前回台帳の全項目を再現・置換したものではなく、前回資料が得られたらIDを対応付ける。

調査基準は現在のIntegration `aa08bbfdfa65c72ada0a0bc7737d36b47a521186`、F実装 `70e1c0ffeaf4d4e9e616d47a28ee3e5aaf0386cc`、F評価文書HEAD `5d32ac1810dcf8cc0b9434a95c6c4b1c7fe72c56`。originの該当Integration/F Branch HEADも一致した。現在本体に書かれているFirmware・startup・NVSは今回読んでいない。

## 1. 確定した方針と実装状態

「採用方針確定」と「実装済み／実機確認済み」を分ける。過去の実機評価は履歴であり、以下の未実装要件をPASSにしない。

| ID | 本採用仕様 | 現状と不足 | 採用状態・次の処理 |
|---|---|---|---|
| EPD-01 | Fを全アプリ共通の既定LUTにする | F配列・選択・復元・診断は既存。起動既定はD、Sketch A。Fは明示選択 | 方針確定。Board限定の既定化が必要。SketchもF対象で、従来のD用Sketch A例外をFへ持ち越さない |
| EPD-02 | fastest＋tile＋SPI20＋rows-dual＋Display Async | 経路は既存。Driverのrefresh初期値はAUTOで、startup依存が残る。SPI20等はBuild/Board条件付き | 方針確定。初回OTP基準生成・失敗時契約を残してBoard既定値へ移す |
| TM-01 | TypeMouseの現行基本機能を維持 | 42キー、Tap/Hold固定、キー反転、BLE Keyboard/Mouse、現在のFrame経路が既存 | 維持。別案の新Touch Frame経路は保留。現行TypeMouseに必要なFrame配送を削除する意味ではない |
| SK-01 | 現在の局所描画版と高速化候補を同一FirmwareでShell切替 | 局所描画・Asyncは既存。二方式の独立したruntime selectorは見つからない。局所描画自体が過去の高速化成果の一部 | 最終方式は保留。候補差分の同定→Shell切替設計→同条件の実機比較後に決定。RAM/LUT切替で代用しない |
| BOOT-01 | startupを簡素化 | 画面内Shellを起動してstartupを実行後、Launcherを表示。USB Shell等がスクリプト依存 | 方針確定。既定化済みの重複行だけを調査後に整理。ファイル全体を削除・上書きしない |
| NET-01 | Wi-Fi起動時ON | NVS未保存時のenabledはtrue。ただしBootではinitのみでradio startとは別。保存OFFは優先する | 方針確定。PaperMono Bootから既存startを呼ぶ最小追加。保存OFFは保持・移行時に明示。AP/NATを新規自動起動しない |
| TIME-01 | NTPをstartupから自動起動しない | NTPは明示同期API/job。Wi-Fi initからの自動呼出しはない。現在の本体startupは未読 | 方針確定。該当`job start ntp-sync`等の自動実行行を事前調査し除去案。手動同期・RTCを残す |
| USB-01 | USB Shellを既定で自動起動 | 既存port Shell APIあり。画面搭載時のUSBセッション作成はstartup依存 | 方針確定。CDC準備後、同じportへの重複作成を防いで既存workerを利用。startup再実行はしない |
| SH-01 | 画面内Shellの空行EscでLauncherへ飛ばさない | 対象の条件分岐が局所的に存在 | 方針確定。当該分岐だけ削除予定。入力取消、USB Shell、他AppのEsc、明示launcherは維持 |
| UI-01 | Launcherは**2行×3列** | ユーザー補足で確定。既存PaperMono専用6枠を再利用 | 方針確定。上段Manual/Reader/Wi-Fi、下段Keyboard/Sketch/Clock。Sleepはヘッダー。Files/Shellは枠からだけ除外 |
| UI-02 | Manual一覧のタッチ操作 | `help`一覧は現状CHARのみ。折畳み・選択・Reader子起動・復帰時state保持は既存 | 方針確定。既存lease/Pointer/CANCEL/Session＋terminal hit-cellを利用して最小追加 |
| SD-01 | 内蔵microSDをSDMMCで追加 | SDMMC・mount/unmount・1/4bitは既存。PaperMonoのstorage device未定義、IOE電源/検出/スリープ接続は不足 | 方針確定。既存サービスを拡張。PYG14=`ioe0:13`、PYG1=`ioe0:0`。自動formatなし |
| SD-02 | Flash保存先・startupを保持、SDは`/sdcard` | 単にBoard SDを有効化するとFlashが`/`→`/flash`、既定保存先がSDへ変わるコードがある | **先に解決する統合条件**。物理SD有無と既定保存先を分離し、PaperMonoのみFlash優先・Flash startupを維持 |
| DOC-01 | SDの完全版Manualを更新・オフライン閲覧 | 署名付きcatalog/ZIP、ページhash、内蔵fallback、版警告は既存 | SD接続後に既存機能を再利用。抜差し時のindex再評価と独自版配布を追加整理 |
| DOC-02 | 独自FirmwareのManualを検証付き配布 | URL・鍵がOTA設定と共用。上流版と同じversion文字列だけでは独自機能を識別できない | 方針確定。独自版ID・正規署名・配布元を準備。署名無効化は不可。ManualのためにOTA更新先を無断変更しない |

## 2. Launcher確定配置

| 行＼列 | 1 | 2 | 3 |
|---|---|---|---|
| 1 | Manual → `help` | Reader → 既存Reader用Files選択画面 | Wi-Fi → `wifi` |
| 2 | Keyboard → `typemouse` | Sketch → `sketch` | Clock → `clock` |

Readerの既存`files --reader`を保持し、Filesアプリの削除やReaderの直接無文書起動へ置き換えない。Wi-Fi画面は既存TUIを利用できるが、現状キーボード中心であり、全機能のタッチ化は今回要件に含めて勝手に追加しない。

## 3. 削除・維持の区分

| 区分 | 対象 | 方針 |
|---|---|---|
| startupの重複行候補 | `lut 5`、fastest/tile/SPI20/rows-dual指定、USBセッション作成、`wifi on` | 既定化が実装・確認された後、実ファイルの内容を確認して該当行だけ整理 |
| startupの自動実行停止候補 | NTP job/同期、Hybrid/typing/旧LUT等の試験設定 | 狙った既定動作と競合する行を特定して除去案。NTP機能自体は残す |
| 通常版から外すShell公開面の候補 | 過去のD/E1/dose/hold実験選択、`writer-lut`旧alias、試験専用強制反復 | 実験Buildに隔離・非推奨化を先行。機能・LUTの即時消去とは分ける |
| 維持する運用・復旧経路 | `launcher`、`shell`、Files、`wifi`、手動NTP、disk mount/umount/status、help/man/update/status、OTP cleanup、Aへの切替・Fへ戻す経路、Trace/Async状態 | 保持。Sketch描画方式selectorも最終比較まで必須。診断出力の読出しで自動Refreshを起こさない |

startup削除候補の一覧は[設計書§2](m5paper-mono-production-adoption-design-20261025.md#2-startup既定設定化とnvs)に詳細化した。現在のユーザーstartupを確認済みとはしない。保存済みWi-Fi OFF、startup source、OTA URL等のNVSを一括消去しない。

## 4. 推奨統合順序

1. コード基点と本体のFirmware/startup/NVS・保存先を記録。Sketch候補のCommit/差分を確定。
2. PaperMonoの表示既定F/fastest/tile/SPI20/rows-dual/Async、USB Shell、Wi-Fi ON方針を独立変更。NTP自動起動を追加しない。
3. 実startupの差分案をレビューし、重複行と実験行だけを整理。空行Esc削除、Launcher配置を小さな独立変更にする。
4. ManualのPointer操作とlease/Session復帰を追加。
5. **Flash保存先/startup sourceの保護方針を先に実装**し、その後にSDMMCのIOE電源・検出・mountを接続。未挿入で起動確認。
6. SDのSleep/Wake・再挿入・使用中I/O処理、Manual index再評価と署名付き独自配布を完成。
7. Sketchの二方式runtime切替を追加し、共通F/RAM条件で実機比較。最終採用を決定。

各段階で将来実施するHost/Build/実機回帰を分ける。今回はどの段階も実装・試験していない。

## 5. 人手確認が必要な項目

- 再起動後のF選択と各Appの黒線・文字、Sketch二方式の追従性/末端/保存再読込。
- KeyboardなしのLauncher→Manual/Reader/TypeMouse、誤タップ・長押しB・CANCEL・Sleep/Wake後の初回タッチ。
- 画面Shell空行Escは無遷移、入力中Escは取消、USB Shellは継続。他Appの戻る操作と明示launcherを確認。
- Wi-Fi保存OFF/未設定/接続先なし、通常OFF→再起動の選択、NTP jobが勝手に始まらないこと、RTC時刻。
- SD未挿入起動、1bit/4bit、mount/unmount後再挿入、Sleep/Wake、使用中の抜去時エラー、内蔵Flashの文書/履歴/設定/startupが同じパスに残ること。
- オンラインManual更新、Wi-Fi OFFでの閲覧、カードなし/破損/署名不一致/版違いのfallback・警告。

詳細な根拠、変更候補ファイル、懸念、将来の検証項目は[追加要件の調査・設計書](m5paper-mono-production-adoption-design-20261025.md)を参照。現段階の判定は**実装設計の準備完了／本採用Firmware完成は未判定**。
