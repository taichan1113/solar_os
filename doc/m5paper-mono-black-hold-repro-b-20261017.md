# SSD1677 Candidate B再現性確認・Phase 1準備（2026-10-17）

## 状態と範囲

Phase 1のHost検証・ESP32-S3ビルドを完了。依頼書§4 Step 3の指定に従い、**実機Flash前の明示承認待ち**。今回はUSB Shellの読取確認のみで、Flash・診断のEPD更新は行っていない。

**Candidate Dは未実装。** B-1「黒化再現」を確認し、実用Integrationへ復元して結果を報告し、Phase 2の承認を受けるまでDには着手しない。B-2／B-3／B-4ならDへ進まない。反復更新は今回の全Phaseで対象外。

| 項目 | 内容 |
|---|---|
| Branch | `experiment/papermono-black-hold-repro-b` |
| Worktree | `E:\ESP32\M5PapserMono-SolarOS\.research\black-hold-repro-b\worktree` |
| Trial基点 | `8f060acae3bbece08934985d85092ace3e2ae53b` |
| 実装Commit | `e9f5989671c968f69e5c35893fb4c7573dd7da8a` |
| Integration基点 | `391925abff2d261d1d5856711aa0a1f101ac435a` |
| 新Application | 3,457,728 B（OTA枠7,340,032 B内） |
| Application SHA-256 | `4e8e7ed51d0d6dbf2adf8fdf5ca8f6c4d55456f153a8aa7325a63fe2b8d38b8c` |

実行基点は旧Bの単発Trial。旧B／C／Integrationの各Branchを変更していない。現在の元Worktreeは `experiment/papermono-sketch-latency-measurement`／`31abd6db`で、既存の未追跡`.vscode/`等は保持する。

## 現在の実機確認

COM39のUSB ShellからSolarOS 4.16.1／writerdeck、Launcher active、実行・起動ともota_0、offset `0x10000`、size `0x700000`、validを確認。Writer／Sketchのセッションはない。`refresh=fastest`とstartupの`fastest-path=tile`を確認した。

前回復元Applicationの既知SHAは`278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6`。**今回の実機Applicationの読戻しhash・MAC・Flash容量・partition内容はまだ再照合していない。** Flash承認後、新規バックアップの中身と現物を照合し、過去のCOM39／OTA0を決め打ちしたまま書き込まない。現状ログは親ディレクトリ`archive/20261017-repro-b/initial-usb.log`。

## 変更点：待機条件だけを固定

前回のT0確認gateはユーザー応答時間を含み、初回customから測定までA/Cで差が生じた。今回、通常描画や物理駆動には触れず、既存診断の白画面確認後の進行だけを次のようにした。

| 時点 | 固定した基準 |
|---|---|
| 共通画像→Baseline初回custom | 既存どおり |
| 動画用T0マーカー | 初回customのDriver完了時刻＋5,000 ms |
| 単発測定要求 | 同完了時刻＋10,000 ms |
| 動画用T1マーカー | 測定のDriver完了時刻＋2,000 ms |
| T1以降 | 確認gateで保持し、回答後に予定のOTP cleanup |

白画面確認の`g`は共通画像・準備・固定待機・単発測定までを許可する。**途中のT0でユーザー応答を待たない。** 動画は連続録画し、T0はマーカーに対応する安定区間から抽出する。T0／測定／T1のマーカーはUSBログだけで、EPD描画を追加しない。

`esp_timer`の絶対期限に対して1 RTOS tickずつ待機し、目標・実際・基準時刻を`BH mark=... anchor_us=... target_us=... actual_us=...`へ出力する。実測のずれも比較し、指定時刻と完全に一致すると事前保証しない。待機中もq／Esc／Ctrl+Cで中断できる。通常Writer等に待機やpacingは追加しない。

コード変更は次の2ファイルだけ。

- `src/shell/solar_os_shell_black_hold_single.inc`：固定時刻マーカー・中断可能な待機。既存のclaim、Snapshot、単発guard、cleanupを再利用。
- `tests/host/black_single_shell_test.c`：固定時刻・実測要求時刻の一致、R0/R2、途中中断、C/D拒否を確認。

## Bの波形は旧実装と同一

`ssd1677_trial_black_hold_single_lut[110]`は旧Bと全バイト一致。LUT0は`8F F0 00 00 00 00 00 00 00 00`、Baselineとの差分はoffset 0・1のみ。

| Phase | 公称Source | Frames |
|---|---:|---:|
| 0A | −15V | 2 |
| 0B | VSS 0V | 2 |
| 0C / 0D | ＋5V | 各2 |
| 1A / 1B | ＋5V | 各1 |

計10フレーム、公称Source時間積0 V・frame。LUT1〜4、FR、Timing、Gate／Source／VCOM、Driver本体・reference更新シーケンス、Dirty rows-dual、SPI、Apps／Services、通常no-op処理は旧Bから無変更。実効DCバランス・残留電荷・寿命の保証はない。

全110バイトB SHA：`45c5419f6986e3b1a81f8394e386fd5bc627503dcaa6d2c483a9113a63209422`。
Baseline SHA：`e8a7abbb2110cb8d941e80cc2869bbe1d0814e1d4a38e5d0cc5173b10f39ce50`。

通常fastestはBaseline。Bは専用USB Shellから1起動につき最大1回。失敗・cancel・display再初期化でbudgetを返却しない。成功後はA/BともBaseline LUTを再送するだけで、復帰時の0x20追加はない。BUSY timeout時に無条件のLUT復元命令を送らない。実レジスタreadbackはせず、異常時Controller内LUT状態は未確認として扱う。

## Host・Build結果

- 既存Driver／SPIテスト：PASS。A/BのRAM、Shadow、Dirty、03／04／2C、22／20の一致、差分のLUT限定、単発制限、Baseline復帰、通常no-opを再確認。
- 診断Shell：R0／R2、BUSY失敗、T0後・測定前の中断の4シナリオPASS。T0＋5秒／測定＋10秒／T1＋2秒をfake clockで検証。正常時の操作gateは白・T1の2回だけ。C/Dの条件名は拒否。
- 独立Python LUTデコード：PASS。Bと旧バイト列の一致、110バイト、6フェーズ、10フレーム、公称時間積、他LUT・FR・電圧バイト維持。
- 既存waveform Trial回帰：PASS。
- 既存主要19実行ファイル＋SketchトレースPython 9件：PASS。Dirty／Sketch／Writer／TypeMouse／Launcher／Shell等。
- ESP32-S3 compile/link：PASS、252.25秒、ESP-IDF 5.5.5／GCC 14.2.0、ログのwarning/errorなし。
- 実効構成：m5paper_mono／writerdeck／ota、BLACK_HOLD_SINGLE=1、DIRTY_HOLD=1、SPI20=1、HYBRID=1、旧WAVEFORM=0、Sketch計測OFF。Hybridコンパイル有効はruntime選択を意味せず、試験はfastest＋tile。

ログは親ディレクトリ`host-single.log`／`host-regressions.log`／`build.log`。`audit_candidate.py`でDriver／LUT／Apps無変更、実効マクロ、partition一致、バイナリSHAを照合した。成果物は親の`artifacts/firmware-app-only.bin`。factory／combined imageは書込みに使わない。[manifest.json](evidence/black-hold-repro-b-20261017/manifest.json)に構成を保存。

## 撮影条件（ユーザー申告）

| 項目 | 設定 |
|---|---|
| カメラ／アプリ | Insta360 Link／Insta360 Link Controller |
| 解像度・FPS | 4K30 |
| 露出・Gain | 手動固定。数値は未提供 |
| フォーカス | 19% |
| 色温度 | 4100 K |
| 明るさ／コントラスト | 81%／45% |
| 彩度／シャープネス | 0%／26% |
| ちらつき防止 | 60 Hz |
| 露出曲線 | 線形 |
| パネル温度 | 未測定（温度計なし） |
| 外乱 | 自然光が入る恐れあり |

設定はA/Bを通じ固定する。Camera Controllerの実状態をこちらでreadbackしたわけではない。近傍白の時間変動、黒clip率、ROI内の位置ずれ等を確認し、自然光や自動制御の疑いを濃度変化と混同しない。撮影条件の記録は[camera-settings.json](evidence/black-hold-repro-b-20261017/camera-settings.json)。

## 承認後のPhase 1手順（未実施）

1. USB接続・未保存編集・他セッションとの実機競合を確認。実機MAC・容量・partition・実行／起動slotを現物で確認し、新規全Flash／Applicationバックアップを取得。現行Applicationの正当性が確定できなければ書き込まない。
2. Application-onlyで書込み読戻し一致を確認。各書込みの直前／直後でbootloader／partition／NVS／FAT／otadata／PHYを照合。起動・startup・Shellを確認する。
3. foreground Shellを確保しLauncherの表示割込みを防ぐ。A動画を開始し、既存コマンド`display test display0 black-hold-single A`を開始。白画面確認後に固定時刻のシーケンスを1回だけ進め、T1で保持・目視確認後に予定のcleanup。
4. 同じ撮影条件でB動画を開始し、同コマンドのB条件を1回だけ実行。Cは実行しない。測定の0x20は各1回。準備・cleanupを含めると診断内は各8回（OTP 3工程×2＋準備custom 1＋測定1）、合計16回、そのうちBは1回。マーカーは0回。通常UI起動・復帰は別枠。
5. 元動画を読取専用で解析。ソフトウェアマーカーに対応するT0／T1の安定区間（各マーカー後0〜1秒）をA/Bで同じ方法で抽出。ROIと文字マスクを動画内で固定し、近傍白から`(白−黒)/白`を計算。T0／T1、最小値、移動中央値、低下・回復量、clip率・白地変動を記録する。
6. 黒四角と文字の各`ΔC=C_T1−C_T0`、`Δ改善=ΔC_B−ΔC_A`を算出。T0差や外乱を明記し、統計的有意差は主張しない。特定LUT位相への光学応答帰属もしない。
7. 重大な異常時は停止・状態記録し、反復や自動再試行をしない。軽微な過渡薄まりも必ず報告。T0が大きく異なる、黒clipや外乱で比較困難な場合、追加更新で揃えようとせずB-3の判断材料とする。
8. A/B終了後は退避したIntegration Applicationだけを復元。Application読戻し、保護領域、Launcher／USB Shell／startupを確認する。試験期間の保存データを全Flash復元で巻き戻さない。

Phase 1をB-1〜B-4で報告して必ず停止する。B-1ならPhase 2を提案し、**ユーザー承認を得てから**Dの実装を開始する。DのHost・Build後もFlashには別の明示承認を得る。B-2／B-3／B-4ならDへ進まない。今回、短期反復は実施しない。
