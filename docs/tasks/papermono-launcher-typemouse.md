# PaperMono Launcher / TypeMouse 引継ぎ

基点: `feature/papermono-type-mouse-mvp` の `c356b83e5727271701ab5099e60bcf9ff54732f5`。このコミットには、実機で文字入力・2点入力・BLE Mouse・Writer/Sketch/Shell回帰を確認したTypeMouse MVPと既存Launcherが入っている。確認結果は前セッションでの報告であり、この引継ぎ作成時に再試験はしていない。EPDのDirty Rectangle、Hybrid Refresh、SPI20 Trialなど後続の実験コミットは取り込まない。

## 開発目的と受入条件

M5Paper MONOを外部BLEキーボードなしで単体操作できるようにする。今回は資料のみで、以下の実装は次のセッションで行う。

1. 起動時の本体画面はShellではなくLauncherにする。USB Shellサービスは維持する。
2. Launcher表示中は本体タッチを有効化し、タップで項目を起動できるようにする。
3. LauncherにKeyboardアイコン付きTypeMouse項目を追加する。外部BLEキーボード未接続でも起動でき、BLE HID送信先が未接続でもTypeMouse画面を表示する。
4. TypeMouseから画面のタッチ操作だけでLauncherに戻れるようにする。切替・終了時は押下状態と旧アプリのタッチを残さない。
5. Writer、Shell、Sketch、既存BLE・保存データの動作を維持する。物理ボタンによる直接起動は将来候補とする。

## 現在の実装と調査入口

| 領域 | ファイルと確認事項 |
| --- | --- |
| 起動アプリ | [`src/main.c`](../../src/main.c) の `app_main`、特に現在 `solar_os_sessions_switch_to_app(solar_os_shell_app())` を呼ぶ起動末尾。表示がない場合のheadless Shell分岐を残す。 |
| USB Shell | [`src/services/solar_os_boot_services.c`](../../src/services/solar_os_boot_services.c)、[`src/services/solar_os_cdc.c`](../../src/services/solar_os_cdc.c)、[`src/services/solar_os_port_shell.c`](../../src/services/solar_os_port_shell.c)。本体の前景App変更とUSB接続経路を混同しない。 |
| Launcher App | [`src/apps/solar_os_launcher.c`](../../src/apps/solar_os_launcher.c) と[`solar_os_launcher_layout.c`](../../src/apps/solar_os_launcher_layout.c)。現在は`POINTER_EVENTS`を受け、項目は`SOLAR_OS_LAUNCH_CHILD_RETURN`で子Appとして起動する。既存のタッチFrameからpointerへ届く経路を確認する。 |
| Launcher設定 | `solar_os_launcher.c`内の`launcher_default_config`と、active storage rootにある既存`launcher.json`。**デフォルトJSONはファイルがない時だけ作成される**ため、既存のユーザー設定を壊さずにTypeMouseを表示する方法を検討する。既存データの自動上書きはしない。 |
| Icon | [`src/services/solar_os_gfx_icons.h`](../../src/services/solar_os_gfx_icons.h) と[`solar_os_gfx_icon_table.inc`](../../src/services/solar_os_gfx_icon_table.inc)。現行Open Iconic表には`key`/`laptop`はあるが`keyboard`名はない。要件のKeyboardアイコンを満たす描画資産と追加範囲を先に決める。 |
| App Registry / package | [`src/apps/solar_os_app_registry.c`](../../src/apps/solar_os_app_registry.c)に`launcher`と`typemouse`が登録済み。[`flavors/writerdeck.toml`](../../flavors/writerdeck.toml)で両packageは有効。 |
| TypeMouse | [`src/apps/solar_os_type_mouse.c`](../../src/apps/solar_os_type_mouse.c)と[`solar_os_type_mouse_engine.cpp`](../../src/apps/solar_os_type_mouse_engine.cpp)。Touch lease、BLE HID開始、Exit領域での`solar_os_context_finish`、停止時のHID解放を確認する。接続先がない時も画面に案内を表示する既存設計。 |
| Touch Driver | [`boards/manifests/m5paper_mono.toml`](../../boards/manifests/m5paper_mono.toml)は`pointer_ft6336`と`touch0`を指定する。**依頼文のGT911はこの基板定義と不一致**。本体ではFT6336Gを前提に[`src/drivers/ft6336.c`](../../src/drivers/ft6336.c)、[`src/services/solar_os_ft6336.c`](../../src/services/solar_os_ft6336.c)を調べる。GT911向けの改修をそのまま適用しない。 |
| Touch配送・切替 | [`src/services/solar_os_input_touch.inc`](../../src/services/solar_os_input_touch.inc)、[`src/services/solar_os_sessions_touch.c`](../../src/services/solar_os_sessions_touch.c)、[`solar_os_sessions_touch_adapter.inc`](../../src/services/solar_os_sessions_touch_adapter.inc)、[`solar_os_sessions.c`](../../src/services/solar_os_sessions.c)。epoch/lifetime、CANCEL、全離指gate、アプリ間の所有権と戻り先を確認する。 |
| BLE HID | [`src/services/solar_os_ble_hid.c`](../../src/services/solar_os_ble_hid.c)、[`src/services/solar_os_ble.c`](../../src/services/solar_os_ble.c)。送信先未接続時のUI起動と、切断・終了時の押下解放を確認する。 |

Launcherは現在Touch leaseを取得していない。一方、TypeMouseは`SOLAR_OS_BOARD_SKETCH_POINTER_DEVICE`を`solar_os_expansion_enable_acquire/release`する。Launcherに同様のleaseが必要か、既存pointer配送と合わせて調べる。TypeMouseの右上Exitはタッチの離指で終了するが、Shellから起動した場合とLauncherの子として起動した場合で復帰先が異なる可能性をセッション管理で検証する。

## 制約と作業手順

- EPD Driver、LUT、SPI設定、Dirty Rectangle、Hybrid Refreshを変更・取込みしない。大規模リファクタリングを避け、MVP First + Continuous Refactoringで進める。
- 元Worktreeへのmerge、GitHub push、実機Flashは行わない。Flashは別途ユーザーの明示的な指示を受けてから行う。NVS/FAT・BLE bond・保存文書を保持する。
- 起動Launcherに失敗した時の復帰経路とUSB Shellを維持する。既存の`launcher.json`はFAT上のユーザーデータとして扱い、破壊的な移行をしない。
- 起動App選択、Touch lease、TypeMouse項目、Keyboardアイコン、タッチ解除を必要な箇所だけ修正する。アプリコードを変更する前に、現在の`launcher`をShellから起動してタッチ入力経路を確認する。

## 検証項目

1. Launcher layout・TypeMouse engine・Touch input/sessionの既存Hostテストを確認し、変更に対応するHostテストを実行する。
2. `m5paper_mono / writerdeck / ota`のESP32-S3 build/linkと生成設定を照合する。各Worktreeの`.pio`は別ディレクトリであり、他のWorktreeの出力を再利用しない。
3. 実機は別途Flash指示後に、ZMK OFFでLauncher起動→タッチでTypeMouse起動→タッチでLauncher復帰、BLE HID未接続表示、USB Shell応答、Writer/Shell/Sketch回帰、切替直後の残留タッチ・押下の有無を確認する。未実施項目をPASSとしない。

新セッションの最初は、このWorktreeの`git status`・HEADを確認し、上表の起動・Launcher・FT6336・セッション経路を読み、ユーザー設定を保護できる最小変更を決めること。ブランチの基点はTypeMouse実用版であり、EPD試作ブランチからコードを持ち込まない。
