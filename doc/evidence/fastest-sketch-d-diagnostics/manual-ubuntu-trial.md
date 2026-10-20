# Ubuntu：Sketch D診断版のBuild・Application-only Flash・手動試験

Phase B用。親PCでは実機アクセス・Flash・試験をしていない。[実装・検証結果](README.md)のBranch／実装Commit／最終Pushを先に確認する。黒保持・白化・長期影響は未確認。

## 1. Git取得と独立した作業場所

既存の未Commit編集・未追跡`.vscode/`・研究資料を保存し、`git status --short`／`git worktree list`／`git remote -v`を確認する。`fork`がない場合だけ追加し、既にある場合はURLを照合する。originは変更しない。

```bash
git remote get-url fork
# fork未登録の場合だけ実行：
# git remote add fork https://github.com/taichan1113/solar_os.git
git fetch fork feature/papermono-sketch-d-diagnostics
git worktree add --detach ../SolarOS-Sketch-D fork/feature/papermono-sketch-d-diagnostics
cd ../SolarOS-Sketch-D
git status --short
git rev-parse HEAD
```

`../SolarOS-Sketch-D`が既に存在する場合は上書きせず別の空き名を選ぶ。detached Worktreeなら同名ローカルBranchが別checkoutで使用中でも既存Branchを切り替えずに済む。報告書の最終Full SHAと一致することを確認し、必要ならそのSHAを指定してWorktreeを作る。既存Branch上で更新する場合はcleanを確認して`git pull --ff-only fork feature/papermono-sketch-d-diagnostics`を使い、分岐していれば停止する。reset／clean／Force Pushは不要。

## 2. Ubuntuで再Build

前回E1をBuildできたUbuntuのPython／PlatformIO環境を使う。Python実行ファイルを`PY`に指定する。実績はPlatformIO Core 6.2.0、pioarduino 55.3.312、ESP-IDF 5.5.5、GCC 14.2.0+20260121。`platformio.ini`のSDK指定を変更しない。

```bash
set -euo pipefail
PY=python3  # そのPCでPlatformIOを導入した環境に置き換える
"$PY" -m platformio --version
unset SOLAR_OS_FLAVOR_FILE SOLAR_OS_FLAVOR_NAME_OVERRIDE SOLAR_OS_SSD1677_WRITER_D_TRIAL
export SOLAR_OS_BOARD=m5paper_mono SOLAR_OS_FLAVOR=writerdeck SOLAR_OS_LAYOUT=ota
export SOLAR_OS_SSD1677_WRITER_D=1 SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL=1
export SOLAR_OS_SSD1677_SPI20_TRIAL=1 SOLAR_OS_SSD1677_HYBRID_TRIAL=1
export SOLAR_OS_SSD1677_WAVEFORM_TRIAL=0 SOLAR_OS_SKETCH_LATENCY_TRIAL=0
"$PY" -m platformio run -e esp32_s3_devkitc1_n16r8 2>&1 | tee build-sketch-d.log
BUILD="$PWD/.pio/build/esp32_s3_devkitc1_n16r8"
IMAGE="$BUILD/firmware.bin"
TABLE="$BUILD/partitions.bin"
stat -c '%s bytes' "$IMAGE"
sha256sum "$IMAGE" "$TABLE"
```

`firmware.bin`だけがApplication。factory／combined／bootloader／partition tableをFlash対象にしない。OTA slot容量はBuildでは7MiBだが、実際の書込み先・容量は次の実機監査で確定する。**WindowsのSHAと同一である必要はなく、Ubuntuで作ったそのイメージと読戻しを比較する。**

Hostを再確認するなら、`CJSON_DIR`をESP-IDFの`components/json/cJSON`へ設定し、`sh tests/host/run_sketch_d_diagnostics.sh`を実行する。本体は不要。

## 3. Flash前確認・fresh backup

ここからはユーザーが実機に実施する。前回終了時はWriter試験文書がactive、Sketch試験Canvasがsuspendedという記録だった。今回改めて保存の要否を確認し、**全Writer／Sketch sessionを閉じる**。前回の終了状態を現在の状態と決め打ちしない。ほかのセッションと本体／ポートを競合させない。

USB Shellで`version`、`sessions`、`ota status`を実行し、UTF-8の`preflight-usb.log`を保存する。`Running`／`Boot`のslot・addr、Shellのsession行を含める。USB consoleのopen/closeでDTR/RTS Resetを発生させない。前回Ubuntuで確立したraw-fd／HUPCL設定の接続方法を優先し、Flash中はconsoleを閉じてポートを解放する。

今回の実機・USBポートを識別する。以前のCOM39や`ota_0 @ 0x10000`を流用しない。以下は手動操作の例。`ESP_PY`にはesptool 5.4.0を使用できるPython、`PORT`には確認したポートを指定する。

```bash
ESP_PY="$PY"
PORT=/dev/serial/by-id/REPLACE_WITH_VERIFIED_DEVICE
ARCHIVE="$HOME/Documents/papermono-sketch-d-$(date +%Y%m%d-%H%M%S)"
mkdir "$ARCHIVE"  # 既存archiveを再利用・上書きしない
# 取得済みpreflight-usb.logをこの新規ARCHIVEへコピーする
test -s "$ARCHIVE/preflight-usb.log"
"$ESP_PY" -m esptool --chip esp32s3 --port "$PORT" --after no-reset chip-id | tee "$ARCHIVE/chip-id.log"
"$ESP_PY" -m esptool --chip esp32s3 --port "$PORT" --after no-reset flash-id | tee "$ARCHIVE/flash-id.log"
"$ESP_PY" -m esptool --chip esp32s3 --port "$PORT" --after no-reset read-flash 0 0x1000000 "$ARCHIVE/full-flash-before.bin"
sha256sum "$ARCHIVE/full-flash-before.bin" "$IMAGE" > "$ARCHIVE/input-sha256.txt"
"$PY" scripts/papermono_e1_flash_audit.py plan \
  --before "$ARCHIVE/full-flash-before.bin" --image "$IMAGE" \
  --partition-table "$TABLE" --preflight "$ARCHIVE/preflight-usb.log" \
  | tee "$ARCHIVE/plan-console.json"
```

最初のesptool接続はdownload modeへ入り、以後のbackup／書込み／読戻しが終わるまで通常Firmwareを起動しない。接続Resetの挙動はそのPCで確立したGuarded手順を優先する。Flash容量16MiBを確認する。`plan`は実機backupから実表の位置・Build表との一致・Running=Boot・raw otadata CRC/slot・容量と消去sector境界を照合し、**今回のApplication partition全体**を`application-before.bin`として退避する。オフライン専用ツールであり、COMを開かない。

## 4. Applicationだけを書き、起動前に検証

planを人間が確認してから実行する。**標準PlatformIO Upload、erase-flash、factory書込みは使用しない。**

```bash
APP_OFFSET=$("$PY" -c 'import json,sys; print(hex(json.load(open(sys.argv[1]))["app_offset"]))' "$ARCHIVE/application-plan.json")
"$ESP_PY" -m esptool --chip esp32s3 --port "$PORT" --before no-reset --after no-reset \
  write-flash --flash-size keep "$APP_OFFSET" "$IMAGE"
"$ESP_PY" -m esptool --chip esp32s3 --port "$PORT" --before no-reset --after no-reset \
  read-flash 0 0x1000000 "$ARCHIVE/full-flash-after.bin"
"$PY" scripts/papermono_e1_flash_audit.py verify \
  --before "$ARCHIVE/full-flash-before.bin" --image "$IMAGE" \
  --after "$ARCHIVE/full-flash-after.bin" | tee "$ARCHIVE/verify-console.json"
```

`OFFLINE_READBACK_AND_PROTECTION_PASS`だけを成功とする。Applicationイメージ一致と、**対象Application partition外の全byte不変**を確認するため、Bootloader／Partition Table／NVS・bond／FAT・文書／otadata／PHY／非対象OTA／その他領域が保護対象になる。不一致や容量・slot不明時は起動・再試行せず記録を保持する。ここまで成功してから通常Resetする。

## 5. 起動回帰と設定

Launcher 6項目、USB Shell、Touchを確認。`fastest-lut status`が通常`mode=D scope=all-apps-except-sketch sketch_d_trial=0`であることを確認する。通常再起動はこの既定へ戻り、試験モードをNVS保存しない。

Sketchで短い線→Launcherへ復帰、Writerで短い入力→保存せず復帰を一度だけ確認する。Power IC／FT6336／Sleep-Wakeの既知エラーが出たら、時刻・App/session・Touch ready・表示を記録し、波形が原因と決めつけない。通常の回帰で不具合があれば主試験へ進まずユーザーと方針を確認する。

必要な表示設定をUSB Shellで照合する。設定自体がモード遷移・再同期を招くので、試験前に行い、区間中に変更しない。

```text
display mode display0 refresh=fastest
display mode display0 fastest-path=tile
display mode display0 dirty-window=rows-dual
display mode display0 spi-mhz=20
display test display0 fastest-lut status
```

| CLIの最後の引数 | 通常App | Sketch Canvas／UI |
|---|---|---|
| `baseline`／`disable` | A | A |
| `candidate_d` | D | A |
| `candidate_d_sketch` | D | **D** |
| `candidate_e1` | E1 | E1 |

すべて`display test display0 fastest-lut <引数>`形式、USB専用。`candidate_d_sketch`は`mode=D scope=all-apps sketch_d_trial=1`になる。実行中のBUSY・RAM同期・LUT復元後に切替え、pendingは破棄しない。pendingは次の実行開始時の選択を使う。LUT切替コマンド自体は画像更新を発生させない。

## 6. 区間診断

初期化・モード選択・Canvas準備を済ませて入力を止め、`trial-start`。ユーザーが描き、最後の線分が落ち着いたら`trial-stop`、`trial-status`と`trace`を保存する。新しい区間を始めるまで停止後サマリーは固定される。

```text
display test display0 fastest-lut trial-start
display test display0 fastest-lut trial-status
display test display0 fastest-lut trial-stop
display test display0 fastest-lut trial-status
display test display0 fastest-lut trace
```

- `trial-start`は区間統計だけをゼロにする。表示／LUT選択／Shadow／累計counter／Queue／Driver failureをResetしない。active中の再start、未開始／停止済みstopは拒否する。
- CLIは既存barrierで実行中の更新を待つ。start前のactiveは区間外、stop時のactiveは完了まで区間内。pendingは保持され、stop直後に走ったpendingは区間外になる。入力を止めて待ってからstopすること。`pending_start/end`を記録する。
- `physical`は画像の成功送信`0x20`数。OTP fullは1presentに2回。電源制御だけの`0x20`は含まない。`frames`／`seq`はno-op・失敗も含むDriver snapshot処理の番号・件数。個々のActivation番号はTraceの`activation_seq=first,last`。
- `A/D/E1/OTP`と`writer/sketch/other`は実Activation数。`other`はLauncher／Shell／Files等の合計で、個別App名は分離しない。`noop`は成功した無Activation present、`error`は失敗present数。
- `busy`は既存時刻の「0x20送信直前→BUSY待機解除検出」のソフトウェア時間。SPIコマンド・既存guard・RTOS待機粒度を含み、電気的なBUSYパルス幅そのものではない。失敗presentでは完了が確実でない時間を除外し`timing_missing`で示す。
- `activation_start_to_start`と`busy_ready_to_next_start`は**区間内の隣接Activation**の開始間隔／前回完了検出→今回開始の間隔。最初の区間外からのgapは平均に入れない。Traceのgapは起動後の直前画像Activationからの値（前回なしは−1）なので、区間平均とは標本範囲が異なる。
- 各metricの`samples / sum_us / min_us / max_us / avg_us`を保存する。samples=0のmin/max/avg=0は測定値ゼロを意味しない。`overflow=1`なら飽和後の合計・平均で比較しない。
- `ram_bytes`は正常終了したRAMデータwriteのbyte合計。部分的に失敗したwriteの途中byte数は含めないため、error時は下限値。command／LUT byte数は含まない。
- `full / rows / rect / otp_ram`はRAMを送った**present件数**で、writeコマンド数ではない。`periodic_resync`はfullに含まれる32更新周期再同期の件数。周期同期が必ず区間内32回目に出るわけではなく、Driver履歴を引き継ぐ。startで周期counterをResetしない。
- `region`は差分領域、`ram_window`は実RAM窓（Driverのnative座標）。rows-dualでは全幅×変更行。これを物理駆動範囲と解釈しない。
- Queueの`submitted/replaced/cancelled/rejected/writer_coalesced`は既存Executor counterの区間差分。受付と実行は異なる集団なので、pendingがあると要求数と区間physicalは一致しない。rawタッチ座標数・入力線分数ではない。`queue valid=0`なら未取得、`wrapped=1`ならuint32折返しを検出しており比較値に使わない。短区間を使用し、2^32件以上にまたがせない。
- Traceは引き続き直近32件。区間集計はそれを超えて保持するが、全更新の時系列は保存しない。診断はRAMのみ、毎更新の動的確保・ログ大量出力・ファイル／NVS保存をしない。試験中の頻繁なstatus/traceは避ける。

## 7. 主試験：Sketch A／D／E1、各最大約20物理更新

撮影・動画・ROI測定は不要。ユーザーが目視できる状態で1条件ずつ進める。A→D→E1の順番・各初期化・OTP回数・ユーザー操作を外部の試験記録へ残す。

1. 各条件でAを選び、既存OTP full/cleanupを必要に応じ**同じ手順・回数**だけ使い、同じ空Canvasを準備する。`refresh=cleanup`は予約であり、次のpresentで実行されるので、Traceの`mode=OTP/noop 0x20>0`で実更新を確認する。設定コマンドだけでcleanup済みと扱わない。
2. Aは`baseline`、Dは**`candidate_d_sketch`**、E1は`candidate_e1`を選ぶ。初期画像が落ち着いたら`trial-start`。
3. 短い直線を1本描き、新規線の最初の黒さを確認する。入力を止め、`trial-status`で実更新数を記録。DではTraceに`source=sketch mode=D 0x20=1 err=0 restored=1`があることを確認する。
4. 同じような位置・長さ・速度の線を少数追加し、**physical約5回、約10回、最大約20回**で止めて濃さを確認する。指の移動回数と混同しない。1回の操作が複数更新になるため閾値一致は保証されない。近づいたら短く描いて少しずつ進め、超えた場合は実数を記録してそこで終える。自動入力や無制限反復はしない。
5. 既存の最初の線が薄まるのか、新しい線が初めから薄いのかを分けて記録する。これはLUT0保持とLUT2新黒を考える手掛かりで、実際の遷移画素数をこの診断で計測したわけではない。
6. 最終線分の反映後`trial-stop`→`trial-status`→`trace`を保存する。正常なno-opやLUT復元だけで追加の画像更新があったと数えない。

| 項目 | A | Sketch D | E1 |
|---|---|---|---|
| 一筆目の黒さ | 未確認 | 未確認 | 未確認 |
| 約5回更新後（実数も記入） | 未確認 | 未確認 | 未確認 |
| 約10回更新後（実数も記入） | 未確認 | 未確認 | 未確認 |
| 約20回更新後（実数も記入） | 未確認 | 未確認 | 未確認 |
| 既存線の薄まり | 未確認 | 未確認 | 未確認 |
| 新規線の濃さ | 未確認 | 未確認 | 未確認 |
| 一瞬の白化 | 未確認 | 未確認 | 未確認 |
| 白背景の汚れ | 未確認 | 未確認 | 未確認 |
| 描画追従性／最終線分 | 未確認 | 未確認 | 未確認 |

評価は良好／やや良好／やや悪い／悪い／判定困難。改善率や黒濃度数値は推測しない。急激な白化・著しい悪化・反転・timeout・入力異常があれば追加入力を止め、予定回数を満たすために続けない。異常の状態・区間・Traceを保持してユーザーと中止／復旧方針を確認し、無断の連続cleanupや自動再試行をしない。

## 8. 必要な場合だけ行う追加比較

- **D連続／間欠**：主試験でDに一定の黒保持効果が見えた場合のみ。同じ`candidate_d_sketch`、同じ初期化、同程度の実更新回数で、止めずに描く条件と数秒ずつ休む条件を別区間で比較。区間のstart-gap／ready-gapを使う。Firmwareのpacingを足す試験ではない。
- **RAM経路**：主試験で必要な場合のみ、`full/rows/periodic_resync`とTraceを照合する。今回RAM切替機能は追加していない。32周期を観察するためだけに20回上限から自動延長しない。追加が必要ならその範囲をユーザーと決める。薄まりの瞬間をseqへ対応付けられなければ因果を断定しない。
- 詳細なWriter z4/z7・削除方式比較は次段階。今回は起動回帰に留める。

## 9. 終了・Rollback

区間を止め、通常モードへ戻す。

```text
display test display0 fastest-lut trial-stop
display test display0 fastest-lut candidate_d
display test display0 fastest-lut status
```

既に停止済みの`trial-stop`拒否は正常。`mode=D scope=all-apps-except-sketch sketch_d_trial=0`、SketchはAに戻る。これは波形選択の復帰であり、旧FirmwareへのRollbackではない。終了時のsession、未保存文書／Canvas、画面状態を記録し、勝手に保存・破棄・Resetしない。

Firmware自体を戻す必要がある場合は、保存要否とユーザーの復旧方針を確認し、**その時点のfresh全Flash backup／preflightからもう一度planを作る**。`--image`を今回退避した正当な`application-before.bin`（Application partition全体）へ置き換え、新しいarchiveで上記Application-only書込み・読戻しを繰り返す。古い全Flashを丸ごと戻さず、現在のNVS／bond／FAT／otadata等を保持する。slot不一致や表変更があれば自動的に先へ進まない。

サブPCの提出物はSource Commit、Ubuntu Buildのsize/SHA、Flash監査結果、初期回帰、条件履歴、区間サマリー、Trace、ユーザー目視、エラーと終了状態。binary、NVS/bondや保存文書を含むbackup、生ログの私有情報をGitへ無断追加しない。肉眼評価の限界、未実施項目も残す。
