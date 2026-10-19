# Candidate E1：別PCでのBuild・Application-only Flash・肉眼試験

このBranchは**実機未検証**。CodexはHost・Buildのみを実施し、COM探索／Flash／撮影をしていない。実験波形E1の効果・白化軽減・実効DCバランス・長期影響は未確認。

通常起動はD（SketchのみA）。E1はUSB Shellから明示選択するRAM設定で、NVS保存しない。**本体再起動／Driver再作成でE1は解除される。** コントローラのcold初期化や通常Sleep/Wakeの経路は変更していない。

## 1. 別PCで取得し、Commitを固定する

Remote URLと最終Full SHAは[実装・Build報告](../../m5paper-mono-fastest-e1-trial.md)のGit欄を確認する。既存Integrationやmainをcheckout／mergeせず、独立フォルダで実験Branchを取得する。

```powershell
$trialUrl = 'https://github.com/taichan1113/solar_os.git'
git clone --branch feature/papermono-fastest-e1-trial --single-branch $trialUrl SolarOS-E1
Set-Location SolarOS-E1
git status --short
git rev-parse HEAD
```

既存の実験checkoutを更新する場合は、未保存変更がないことを確認して以下を使う。強制reset／cleanは不要。

```powershell
git fetch origin feature/papermono-fastest-e1-trial
git switch feature/papermono-fastest-e1-trial
git pull --ff-only origin feature/papermono-fastest-e1-trial
git rev-parse HEAD
```

報告のSHAを含む同Branchか確認する。後続が資料Commitだけの場合、FirmwareソースCommitとの関係も報告書に記載する。実装のない古い版へ戻らない。

## 2. Buildする（この段階では本体接続不要）

Python／PlatformIOと当リポジトリ指定のSDKを使用。実測済み構成はESP-IDF 5.5.5、GCC 14.2系。別PCのPython実行ファイルを`$py`へ設定する。既存SDKを適当な別バージョンへ置換しない。

```powershell
$py = 'python'
$env:SOLAR_OS_BOARD = 'm5paper_mono'
$env:SOLAR_OS_FLAVOR = 'writerdeck'
$env:SOLAR_OS_LAYOUT = 'ota'
$env:SOLAR_OS_SSD1677_WRITER_D = '1'
$env:SOLAR_OS_SSD1677_DIRTY_HOLD_TRIAL = '1'
$env:SOLAR_OS_SSD1677_SPI20_TRIAL = '1'
$env:SOLAR_OS_SSD1677_WAVEFORM_TRIAL = '0'
$env:SOLAR_OS_SSD1677_HYBRID_TRIAL = '1'
$env:SOLAR_OS_SKETCH_LATENCY_TRIAL = '0'
Remove-Item Env:SOLAR_OS_FLAVOR_FILE -ErrorAction SilentlyContinue
Remove-Item Env:SOLAR_OS_FLAVOR_NAME_OVERRIDE -ErrorAction SilentlyContinue
& $py -m platformio run -e esp32_s3_devkitc1_n16r8
if ($LASTEXITCODE -ne 0) { throw 'Build failed; do not flash' }
$build = (Resolve-Path '.pio/build/esp32_s3_devkitc1_n16r8').Path
$image = Join-Path $build 'firmware.bin'
$table = Join-Path $build 'partitions.bin'
Get-Item -LiteralPath $image | Select-Object Length
Get-FileHash -LiteralPath $image -Algorithm SHA256
```

`firmware.bin`がApplication単体、`partitions.bin`は**照合専用で書き込まない**。`firmware.factory.bin`、全体イメージ、bootloaderを使わない。7MiB OTA構成が今回のBuild容量だが、実際の書込み先は後述の実機照合から決める。別PCでは時刻やパスによりBinary SHAが変わり得るため、そのPCでBuildしたイメージのSHAを記録し、同じイメージの読戻しと比較する。

Host検証を再現する場合は、WSL/Linuxで`CJSON_DIR`をそのPCのESP-IDF cJSONソースへ設定し、`sh tests/host/run_fastest_e1_trial.sh`。これはCOMを使わない。

## 3. 本体確認と新しいバックアップ

ここからは**人間が本体へ実施**する。別セッションとの競合がないことを確認する。Writer／Sketchは保存してすべて閉じる（suspendedセッションにも注意）。本体の表示状態を記録する。

1. USB機器と接続ポートを確認する。COM39、過去のMACやslotを決め打ちしない。
2. USB Shellで`version`、`sessions`、`ota status`を実行し、出力をUTF-8で`preflight-usb.log`へ保存する。Shellのsession行・`Running`／`Boot`のslot・addrを含める。
3. USB terminalを閉じ、他のmonitorも終了する。以後の書込み検証完了までアプリを動かさない。
4. **新しい恒久保管フォルダ**を用意する。過去バックアップで代用しない。フルバックアップにはNVS／bond／保存文書も含まれるため、Gitや公開Remoteへ追加しない。

```powershell
$port = '<確認したCOMポート>'
$archive = Join-Path (Split-Path (Get-Location).Path -Parent) ('papermono-e1-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $archive -ErrorAction Stop
# 保存したログを、この新しいフォルダへコピーする。
# Copy-Item -LiteralPath '<今回のpreflight-usb.log>' -Destination $archive
& $py -m esptool --chip esp32s3 --port $port chip-id
& $py -m esptool --chip esp32s3 --port $port flash-id
```

ESP32-S3／実機MAC／16MBを照合し、異なる場合は書込みを進めない。これらはユーザー自身の操作であり、Codexによる実行はしていない。

以下はesptool 5.4系のCLI表記。別PCの同じ`$py`で`-m esptool version`が利用できることを確認し、未導入なら専用Python環境へ`esptool==5.4.0`を導入する。既存PlatformIOのSDKやプロジェクト依存関係を更新する必要はない。

以下でROM/download状態を維持して読み取る。必要なら本体の正規手順でdownload modeへ入り直す。`no-reset`接続に失敗した場合は無理に進めず、同じバックアップセッションとして条件を再確認する。

```powershell
$before = Join-Path $archive 'full-flash-before.bin'
& $py -m esptool --chip esp32s3 --port $port --after no-reset read-flash 0 0x1000000 $before
if ($LASTEXITCODE -ne 0) { throw 'Backup failed; do not flash' }
Get-FileHash -LiteralPath $before -Algorithm SHA256
& $py scripts/papermono_e1_flash_audit.py plan --before $before --image $image --partition-table $table --preflight (Join-Path $archive 'preflight-usb.log')
if ($LASTEXITCODE -ne 0) { throw 'Slot/partition/backup audit failed; do not flash' }
$plan = Get-Content -LiteralPath (Join-Path $archive 'application-plan.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$plan | Format-List active_slot,app_offset,app_partition_size,image_bytes,image_sha256,rollback_sha256
```

この補助スクリプトは**オフラインのファイル解析だけ**を行う。USB／COM／esptoolを呼ばない。既存Guarded Flashで使った、実機partition table一致・Running=Boot・raw otadata CRC／slot・容量／消去sector境界・未終了Writer/Sketchの確認を移植した。

partition tableの位置はフルバックアップ中の実データ一致から求める。実機表とBuild表が異なる、OTA状態が不整合、Application容量不足の場合は拒否する。成功すると、**今回実機のApplication partition全体**を`application-before.bin`として保存する。過去のSHAやota_0のオフセットを使って書込み先を決めてはいけない。

## 4. Applicationだけを書き、起動前に読戻す

本体をdownload状態のまま維持する。**`erase_flash`、`pio run -t upload`、factory書込みは禁止**。以下は人間がplanを確認してから実行する。現行Firmwareが再起動してしまった場合は、読み取り前後のNVS/FAT比較条件が変わるため新しいバックアップからやり直す。

```powershell
$address = '0x{0:x}' -f [long]$plan.app_offset
& $py -m esptool --chip esp32s3 --port $port --before no-reset --after no-reset write-flash --flash-size keep $address $image
if ($LASTEXITCODE -ne 0) { throw 'Write failed; keep archive and inspect before retry/reset' }
$after = Join-Path $archive 'full-flash-after.bin'
& $py -m esptool --chip esp32s3 --port $port --before no-reset --after no-reset read-flash 0 0x1000000 $after
if ($LASTEXITCODE -ne 0) { throw 'Readback failed; do not assume success' }
& $py scripts/papermono_e1_flash_audit.py verify --before $before --image $image --after $after
if ($LASTEXITCODE -ne 0) { throw 'Readback/protection mismatch; keep device stopped and inspect' }
```

`OFFLINE_READBACK_AND_PROTECTION_PASS`を確認する。候補イメージの読戻し一致に加え、**対象Application partition外の全バイト**を比較するため、bootloader／partition table／NVS／FAT／otadata／非対象OTAも保護対象となる。アプリの末尾消去範囲もpartition内であることを事前検証する。

検証後だけ通常Resetする。USB Shell起動、6項目Launcher表示、Startup設定・保存文書保持を確認する。最初はDのままでE1は有効になっていない。

## 5. A／D／E1の切替とTrace

**USB Shell専用**の既存CLIを拡張した。本体画面Shellからの波形選択は引き続き不可。別PCからUSB terminalで次を1コマンドずつ実行する。terminalがDTR/RTSでResetしないよう設定する。

```text
display mode display0 refresh=fastest
display mode display0 fastest-path=tile
display mode display0 dirty-window=rows-dual
display mode display0 spi-mhz=20
display test display0 fastest-lut baseline
display test display0 fastest-lut status
display test display0 fastest-lut candidate_d
display test display0 fastest-lut candidate_e1
display test display0 fastest-lut trace
```

| 選択 | 通常アプリのfastest | Sketchのfastest | status |
|---|---|---|---|
| `baseline`／`disable` | A | A | mode=A |
| `candidate_d`／通常boot | D | A | mode=D、scope=all-apps-except-sketch |
| `candidate_e1` | E1 | **E1（Canvas/UIとも）** | mode=E1、scope=all-apps |

`writer-lut`は既存aliasとして残る。切替は物理更新を発生させず、既存Executor barrierが実行中のBUSY・post RAM同期・A LUT復元完了まで待つ。pendingは破棄せず、**実行開始時の選択**を使用し、元のSketch/Writerタグを保つ。実行中の画像へ途中で別LUTが入ることはない。

`err=0`を確認する。FASTEST/tile/20MHz/rows-dual等の条件不適合やDriver failure latch時は選択を拒否する。typing／hybrid／OTPへ切替えても、それらの既存波形をE1に置換しない。選択はRAMなので、本体再起動後はE1をもう一度選ぶ必要がある。

既存`attempts/D0x20/success/error`はD専用の累計を維持。追加`E1_attempts/E10x20/E1_success/E1_error`と`display ... E1=`がE1専用。選択のたびに累計はリセットされないので差分を取る。Traceは直近32記録。

E1中にSketchを描いたあと、次のような**物理更新を伴う行**があるか確認する。

```text
WL ... source=sketch mode=E1 0x20=1 err=0 restored=1 ...
```

`mode=E1`のstatusだけでは実パネルへの更新実績にならない。無変更画像は今までどおりskipされる。`restored=1`はE1のBUSY・RAM同期後にA LUTをRAMへ戻したという意味で、E1選択が解除されたという意味ではない。復元の0x32は追加0x20を発行しない。

## 6. 手動の肉眼比較（未実施）

写真・動画・ROI解析はしない。机上の明るさ、同じRAM文書、フォント、表示領域、入力速度・長押し時間をできるだけ揃える。まず短い操作とし、不必要な長時間反復をしない。pacingや追加waitをFirmwareへ入れていない。

各条件開始時に、Aを選択→既存`display mode display0 refresh=cleanup`を1回→次の画面提示でOTP fullが完了したことを目視／Traceで確認→同じ初期文書表示→比較対象A/D/E1を選択、の順を揃える。初期化の回数・内容も記録する。cleanupは次のpresentへ作用するので、コマンド入力だけで完了したと思い込まない。

1. **Writer：A→D→E1。** 空のRAM文書`writer`を使い、同じ固定文字`BLACK`と短い共通文を表示する。同じキーをまず約1秒だけ長押し、その後Backspaceを同じ時間長押しする。新文字と以前の文字の濃さ、途中白化、欠け、背景、体感遅延を記録。各条件のTrace差分も記録する。保存不要の試験内容を既存文書へ上書きしない。
2. **Shell：A→D→E1。** 空の入力行で同じ短い文字列／1秒程度の連続入力・削除を比較する。Enterで実コマンドを実行しない。初期の既存Shell文字と入力中の文字を別々に観察。Ctrl+Uで行を消す操作も記録する。
3. **Sketch：A→E1。** 新しいCanvasで同程度の短い直線・往復線を少数描く。最初の線、新しい線、後続更新後の先の線の濃さ、途切れ、離指後の末端、追従性を比較。**D選択時のSketchはAなので、Sketch Dとの比較と書かない。** Traceの`source=sketch mode=E1`を確認する。
4. **固定文字保持。** Writerの上の行に`BLACK`を残し、下の行へ`012345`等を少しずつ追加／削除して実際に画像を変える。固定文字の白化・薄まりを観察。同じ画像をただ送るだけではskipされるので、0x20累計の差分またはTraceを確認する。

異常反転、明瞭な濃度悪化、強い残像、BUSY timeout／Driver errorがあれば人間が追加入力を止め、状態を記録する。無制限cleanupや自動再試行はしない。目視差が小さい場合は「判定困難」とする。

| 項目 | A | D | E1 |
|---|---|---|---|
| 通常文字の黒さ | 未確認 | 未確認 | 未確認 |
| 連続入力後の黒さ | 未確認 | 未確認 | 未確認 |
| 連続削除後の黒さ | 未確認 | 未確認 | 未確認 |
| 一瞬の白化 | 未確認 | 未確認 | 未確認 |
| 白背景の汚れ | 未確認 | 未確認 | 未確認 |
| 文字／線の欠け | 未確認 | 未確認 | 未確認 |
| 描画の体感速度 | 未確認 | 未確認 | 未確認 |
| Sketch描線 | 未確認 | 対象外（Aになる） | 未確認 |

評価は「改善／同等／悪化／判定困難」、コメント、初期化回数、入力時間、更新カウンタ差分を併記。厳密な黒濃度・百分率や寿命の数値は出さない。

## 7. Application-only Rollback

一時的な波形解除は`fastest-lut baseline`、通常方針への復帰は`candidate_d`。**Firmware自体の復旧とは異なる**。本体再起動でもE1は解除され、D／Sketch Aへ戻る。

Firmwareを戻す場合は、今回採った`application-before.bin`とSHAを使用する。Writer／Sketchを保存・終了し、現在の`version/sessions/ota status`を再取得する。新しい別archiveで**Rollback直前の全Flash**を採り、次を行う。

1. `plan`の`--image`へ最初の`application-before.bin`を指定し、最新の実機Running/Boot・partition・otadataを再照合する。最初のFlashと同じslotと決めつけない。
2. 新しいplanの`app_offset`だけへ、上記と同じ`write-flash --flash-size keep`でApplicationを復元する。7MiB Application partition全体のバックアップでも、対象はこのApplication内だけ。
3. Reset前の全Flashを読み戻し、**Rollback直前のバックアップとの間で**`verify`を実行する。最初の古いNVS/FATと比較・復元しない。
4. PASS後にResetし、USB Shell、Launcher、通常D／Sketch Aを確認する。

古い16MB全体バックアップを丸ごと書くと新しい保存文書やbondを巻き戻すため、日常のRollbackには使わない。失敗時に自動復旧を走らせるスクリプトは追加していない。
