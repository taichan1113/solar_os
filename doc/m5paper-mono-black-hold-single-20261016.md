# SSD1677 黒→黒補償 単発Trial準備（2026-10-16）

> 以下は10月16日時点の準備・保留記録。10月17日にユーザー承認を受け単発A/BとIntegration復元を完了した。最新結果は[実機報告](m5paper-mono-black-hold-single-hardware-20261017.md)を参照。

## 状態と基点

Host検証・ESP32-S3ビルド済み。実機Flash・波形実行は未実施。依頼書Step 4の指定に従い、結果と候補を提示してFlash承認を得るまで実機へ進まない。短期反復試験は対象外。

**ユーザー判断：今回は実機試験を保留（2026-10-16）。** Flash承認への回答は「今回は実機試験を保留する」。TrialのFlash、A/B、Candidate波形実行は行わず、現在のIntegrationを維持する。再開には改めてユーザーの指示を待つ。保存したHost結果・ビルド成果物は実機評価済みとは扱わない。

| 項目 | 内容 |
|---|---|
| Branch | `experiment/papermono-black-hold-single` |
| Worktree | `E:\ESP32\M5PapserMono-SolarOS\.research\black-hold-single\worktree` |
| 基点 | `391925abff2d261d1d5856711aa0a1f101ac435a` / `integration/papermono-dirty-sketch` |
| 実装Commit | `60012235` |
| 比較元の実機Application | 3,455,712 B、SHA-256 `278665c184653f06a403bd56713e7bf5073131bead0420b3fd2191fa54b12ef6` |

この基点は今回の依頼直前にApplication-onlyで書込み済み。ユーザーがLauncher表示・TypeMouse起動・Exit復帰を正常確認した。Flash直後にLauncher開始時 `ESP_ERR_INVALID_STATE` が1回発生し、通常再起動1回で復帰した記録も保持する。詳細は `.research/dirty-sketch-integration/archive/20261016-integration-live/DEPLOYMENT.md`。同ApplicationのWriter／Sketchはこの直前の書込みでは再試験していない。各機能の過去の実機確認と、この統合バイナリの確認範囲を混同しない。

## LUTの事実

一次資料：[SSD1677 Rev1.0](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf) の6.6・Table6-6（P16）、6.7・Figure6-6（P17）、Command Table 0x03／0x04／0x2C／0x32。LUT0は先頭10バイト、各バイトのD7:6→A、D5:4→B、D3:2→C、D1:0→D。TP=0は省略、RP+1回繰返し。

Baseline 110バイトのSHA-256は `e8a7abbb2110cb8d941e80cc2869bbe1d0814e1d4a38e5d0cc5173b10f39ce50` で従来と一致。Candidateとの差分はoffset 0=`00→8F`、offset 1=`00→F0` の2バイトだけ。LUT1〜4、時間設定、フレームレート、末尾電圧設定は全バイト一致。

| Phase | code | Frames | 公称Source |
|---|---|---:|---:|
| 0A | 10 | 2 | −15V |
| 0B | 00 | 2 | 0V |
| 0C | 11 | 2 | ＋5V |
| 0D | 11 | 2 | ＋5V |
| 1A | 11 | 1 | ＋5V |
| 1B | 11 | 1 | ＋5V |

計10フレーム、公称Source時間積は `−15×2＋0×2＋5×6=0 V・frame`。これはVCOM・キックバック・残留電荷を含む実効DCバランス、寿命、パネル適合性の証明ではない。メーカー検証済み波形ではなく、固定黒の一時白化や回復不足は未確認の可能性として残る。実際のBUSY時間・速度・光学品質はHostから判断しない。

## 実装とガード

`SOLAR_OS_SSD1677_BLACK_HOLD_SINGLE_TRIAL=1` の専用経路。既存の持続的dose／hold選択は `SOLAR_OS_SSD1677_WAVEFORM_TRIAL=0` のまま無効で、両方を同時に有効にするビルドは拒否する。起動時、通常Writer／Sketch／TypeMouse、通常の無変更更新省略はBaselineの動作を維持する。

- `epd_ssd1677_waveform_trial.inc`：別Candidate配列。Baseline配列は無変更。
- `epd_ssd1677_black_hold_arm()`：排他・drain済みのDisplay Executorへ次の1 Snapshotだけを予約。Candidateは正常な温間fastest/tile/20MHz状態のみ。グローバルなB使用済みフラグは失敗・キャンセル・display再初期化でも返却しない。再起動時はBaseline・未予約へ戻るが、再試験を自動実行しない。
- `epd_ssd1677_present_snapshot()`：実Snapshotが完了Shadowと同じことを確認し、専用要求でだけ無変更の物理更新を許可する。終了時に予約・選択を解除。
- `ssd1677_reference_refresh()`：既存RAM転送・rows-dual・0x21／0x22／0x20・BUSY処理・Shadow確定を維持。専用要求の無変更時だけ全画面RAM範囲を明示する。Dirtyアルゴリズムを変更しない。
- 正常終了後、A/Bとも同じ `0x32` Baseline復元を追加する。各要求で `0x32` は2回、`0x03/04/2C` は各1回、温間 `0x22=0x0C` と `0x20` は各1回。復元LUTには追加activationがなく10フレームの物理更新は1回だけ。
- エラー時は候補選択を解除し、BUSY timeout後に無条件でLUT復元命令を送らない。Driver fault時は追加更新を拒否する。失敗時のController内LUTの物理復元は未確認であり、Baselineを実際に再設定できるのは正常な復元または再初期化後の更新時。

シェル専用コマンドは `display test display0 black-hold-single A|B`。Serial Port Shellかつ本体foreground Shellを必須とし、Display claimと既存のimmutable Snapshot executorを再利用する。Launcherや他アプリを閉じてから実行する。A/B各1回のShell budgetとDriverのB単発budgetを併用。T0/T1でoperator gateを持ち、後続試験や再試行は自動実行しない。

既存試験の固定文字・48×48黒四角・線・右側aを再利用する。U8g2 R0/R2、800×480、buffer pointer、黒の極性を検証する。毎submit前にcanonical画像を戻し、強制更新はSnapshotとShadowの一致もDriverで照合する。共有bufferの編集を無視して誤った画像で試験しない。

Apps、Services、Board、partition、SPI設定、更新pacing、電圧値は基点と同一。新規の持続的波形設定やNVS設定はない。

## Host結果

- 既存主要19実行ファイル＋Python 9件：PASS（Dirty／Sketch連携、32回境界、Writer、TypeMouse、Launcher、Shell等）。
- 新Driverテスト：PASS。実Driver/SPI、既存Controller RAM retain/swapの2仮説モデル、R0/R2、両RAM/Shadow整合性、BB=00分類、通常no-op、同一画像の1回activation、A/B全コマンド・RAMバイト一致、LUT差分限定、Baseline復元、B再要求拒否、失敗・キャンセル・display再初期化でもbudget非返却。
- 新Shellテスト：正常R0、正常R2、BUSY失敗の3シナリオPASS。実U8g2と共通画像、OTP準備、Baseline初回custom、A/B単発、T0/T1 gate、cleanup、A/B再起動なし再実行拒否を確認。
- 既存C `waveform_frames()` を再利用：VSS=2／VSH1=0／VSL=2／VSH2=6フレーム。他LUTのデコードはBaselineと一致。既存dose/holdテストもPASS。
- 独立Python unittest：110バイト長、Baseline SHA、差分offset、6フェーズ、10フレーム、公称時間積、全電圧・FRバイトの一致を確認してPASS。

ログは親ディレクトリの `host-regressions.log` と `host-single-final.log`。Controller RAMの内部挙動をHostで確定したわけではない。retain/swapは代替モデルであり、実機成立の保証ではない。

## ビルド結果と成果物

ESP32-S3 compile/link PASS、224.79秒。ログ中の `warning:`／`error:` は0件。ESP-IDF 5.5.5、Xtensa GCC 14.2.0、`m5paper_mono`／`writerdeck`／`ota`、UI140%。Dirty=1、SPI20=1、Hybrid=1、旧Waveform=0、Black Hold Single=1、Sketch計測=0。Hybridはコンパイル有効という意味で、実行時のrefresh選択を変更していない。

- Application：3,458,656 B、OTA枠7,340,032 B内。
- SHA-256：`97d92273012ea11fa7b423ea38be8a8c9261071c5502e380028c3b35ceaa2cea`。
- 成果物：`E:\ESP32\M5PapserMono-SolarOS\.research\black-hold-single\artifacts\firmware-app-only.bin`。
- `manifest.json`：構成、LUT差分、ハッシュ、実機未アクセスを記録。
- `partitions-reference-only.bin`：現在のIntegrationビルドのpartitionと全バイト一致。ただし実機のslot選択は承認後に再取得する。
- 監査：親ディレクトリ `audit_candidate.py`。ビルドログは同 `build.log`。

実装Commit後の追加変更はHostデコーダ再利用テストと本報告書のみで、ビルド対象の `src`／`scripts` は `60012235` と一致する。元Integrationと他の実用Worktreeは変更していない。

再ビルドは既存PlatformIO仮想環境を使用し、以下の環境変数を明示する。

```text
SOLAR_OS_BOARD=m5paper_mono
SOLAR_OS_FLAVOR=writerdeck
SOLAR_OS_LAYOUT=ota
SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL=1
SOLAR_OS_SSD1677_SPI20_TRIAL=1
SOLAR_OS_SSD1677_HYBRID_TRIAL=1
SOLAR_OS_SSD1677_WAVEFORM_TRIAL=0
SOLAR_OS_SSD1677_BLACK_HOLD_SINGLE_TRIAL=1
SOLAR_OS_SKETCH_LATENCY_TRIAL=0
```

```powershell
& C:\Users\takec\.platformio\penv\Scripts\python.exe -m platformio run -e esp32_s3_devkitc1_n16r8
```

## 実機承認後の手順（未実施）

1. 他セッションとのFlash競合、未保存編集、画面・撮影条件を確認。実機MAC、Flash容量、partition、実行・起動slotをその都度読む。現在のIntegrationを含む新規バックアップを取得し、Application-only書込みと保護領域の前後比較を行う。
2. 本体をShellへ戻し、Launcher・他アプリによる更新を止め、cameraの露出・WB・焦点・位置・照明を固定する。動画がなければ更新途中の白化・ちらつきは未確認とする。
3. Aを1回：OTP full白→白画面確認→OTP full共通画像→Baseline初回custom→T0撮影→Baseline温間1回→BUSY後2秒→T1撮影・目視→OTP full cleanup。
4. Bを1回：同じ準備→T0→Candidate温間1回→BUSY後2秒→T1→OTP full cleanup。Bの追加実行はしない。
5. 各条件の準備はOTP fullコール2回（現実装の0x20は計4回）＋Baseline custom 1回。測定は1回。cleanupはOTP fullコール1回（0x20=2回）。専用コマンド内は各条件で計8回の0x20、そのうちCandidateはBの1回だけ。Flash起動や通常画面への復帰描画は別途ログで数える。
6. `BH phase=...` で0x03/04/2C/32/20の成功送信数、SPI actual、start/end、SPI呼出し所要時間、BUSY、全体時間を記録。OTPは複数activationの各BUSY時間を既存OTPログで見る。SPI時間は純粋なwire時間とは区別する。
7. 元写真の同一ROIで `(白−黒)/白` を比較し、目視・動画と合わせる。撮影条件の差を厳密な測光値と扱わない。単発結果から反復保持性能・DCバランスを断定しない。
8. 終了後は、試験直前に退避したIntegration Applicationだけを戻す。NVS／FAT等の最新データを全Flash復元で巻き戻さない。起動・保護領域・画面を再確認して終了。

BUSY timeout、更新失敗、想定外反転、著しい白化、白背景汚染、状態不整合、電源異常時はCandidateの追加実行を禁じる。自動リトライ・反復cleanupは行わず、ログと状態を保持してユーザーへ報告する。

## 判定

コード・Hostで確認できたのはバイト列、遷移クラス、制御経路、単発制限、論理RAM整合性まで。光学的改善、途中の白化、実時間、パネルへの長期的影響はすべて未測定。短期反復Trialの推奨可否は、承認後の単発A/B結果が出るまで判断しない。

現在の実機はIntegration版のまま。このTrialをFlashしたとは報告しない。
