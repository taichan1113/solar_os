# PaperMono Candidate E1：実機試験準備

2026-10-19。E1=`3B F0`を既存A/Dに加える。**実機COMアクセス・Flash・撮影・実機評価は未実施。** Host成功を光学効果のPASSとは扱わない。

## 実装

- 基点：Integration `6d5c2028ae0db3c31cb8d90a7fba51f13b0b7d9a`。Launcher 2×3／Touch／Sleep-Wake、Dirty rows-dual、Sketch高速化、通常D／Sketch Aを保持。
- Branch：`feature/papermono-fastest-e1-trial`。Integration／mainへMergeしない。
- `epd_ssd1677_reference.inc`へE1の110B配列を追加。Aとの差はoffset0/1だけ、Dとの差はoffset0だけ（2byte範囲内）。LUT1〜4、TP/RP/FR、電圧、0x22/0x20、RAM転送、BUSY待ち、shadow・復元処理を維持。
- `epd_ssd1677_writer_d.inc/.h`にRAM内E1選択と専用実績を追加。D/A既存カウンタは別のまま。E1に限りSketch例外を通過し、Dへ戻すとSketch Aが復活。
- `solar_os_shell_writer_lut.inc`／helpへ`candidate_e1`を追加。status／traceの既存prefixとD項目を保持し、E1の項目・modeを追加。
- 既存Executor barrierを使用。実行中のBUSY→RAM同期→A LUT復元を終えてから切替え、pendingは元のタグを保持し実行開始時の選択を使う。Queue・Mutex・追加wait／pacingなし。

```text
Phase  0A  0B  0C  0D  1A  1B
Frames  2   2   2   2   1   1
E1 V    0  +5 -15  +5  +5  +5
```

10F、`0×2+5×2−15×2+5×4=0 V・frame`。これはSource側の公称計算であり実効DC／寿命の証明ではない。

## 切替（USB Shell）

```text
display test display0 fastest-lut baseline
display test display0 fastest-lut candidate_d
display test display0 fastest-lut candidate_e1
display test display0 fastest-lut status
display test display0 fastest-lut trace
```

| 選択 | 通常fastest | Sketch fastest |
|---|---|---|
| baseline / disable | A | A |
| candidate_d / 通常boot | D | A |
| candidate_e1（明示のみ） | E1 | E1 |

E1はNVS保存なし。他refreshは従来方式を尊重する。`E1_attempts/E10x20/E1_success/E1_error`、`display ... E1=`、Traceの`source=sketch mode=E1 0x20=1`で区別する。statusは選択状態、Traceは実行実績。

## Host検証

実Driver＋SPI捕捉＋Controller RAMモデルでE1の6Phase、10F、Source積0、A/Dとの配列・RAM payload・電圧・更新順／回数の一致、R0/R2、保持／swapモデル、shadow昇格・A LUT復元、通常boot D、全source E1、E1→DのSketch例外復活、E1→A、OTP／typing／no-opを検証。

実pthread ExecutorとDriverを接続し、E1中のBUSY／復元待ちにD/Aを選ぶ場合、pending Sketchが保持されること、切替後の実LUTが期待どおりであることを検証。LUT／BUSY／post-sync／復元失敗では既存latchが働き再試行しない。

既存Launcher・Touch・Sleep構成・Writer・Sketch・TypeMouse・BLE・波形のHost回帰を実施。`tests/host/run_fastest_e1_trial.sh`で再現可能。追加のFlash監査ツールはオフラインでテストし、正しいslot、誤table／slot／CRC／未終了Writer／非Appイメージ拒否、読戻し／保護差分検知を確認する。

## Build・Git結果

| 項目 | 結果 |
|---|---|
| Host回帰 | PASS：46 executable target、追加Async 3モード、Python 12テスト |
| Hostコマンド | `CJSON_DIR=<ESP-IDF>/components/json/cJSON sh tests/host/run_fastest_e1_trial.sh` |
| Firmware Build | PASS、248.09秒、warning／errorなし |
| PlatformIO環境 | `esp32_s3_devkitc1_n16r8`、pioarduino 55.3.312 |
| Board / Flavor / Layout | `m5paper_mono` / `writerdeck` / `ota` |
| ESP-IDF | 5.5.5 |
| GCC | `xtensa-esp-elf-gcc.exe (crosstool-NG esp-14.2.0_20260121) 14.2.0` |
| Buildした実装Commit | `b918c36a1f0b5d1014062149197d4670fde718f1` |
| Application binary | `firmware.bin`、3,461,488 B |
| Application SHA-256 | `73a2598434a4a4488859607a65fb9c4638a362853e73aceac6326f482a498260` |
| OTA slot容量 | 各7,340,032 B（7 MiB）、余裕3,878,544 B、sector切上げ後も適合 |
| 実効構成 | fastest / tile、20,000,000 Hz設定、Dirty rows-dual、Display Async |
| Build flags | `WRITER_D=1` / `DIRTY_HOLD_TRIAL=1` / `SPI20_TRIAL=1` / `HYBRID_TRIAL=1` / `WAVEFORM_TRIAL=0` / `SKETCH_LATENCY_TRIAL=0`（すべて`SOLAR_OS_SSD1677_`、最後のみ`SOLAR_OS_` prefix） |

SDKの表示上の汎用board名はESP32-S3 DevKitであるが、生成ヘッダと実コンパイル引数でPaperMono・800×480・Launcher Touch Grid・Sketch Asyncを照合した。`SKETCH_LATENCY_TRIAL=0`は旧計測Trialを無効にする値であり、既存のSketch高速化はBoard設定で有効のまま。20MHzは設定値・ビルド確認で、今回は実SPI測定をしていない。

実装後に追加するCommitは本報告・ログなどの資料だけであり、Build対象のFirmwareソースは上記実装Commit。Gitの最終資料Commit SHAはCommit後の完了報告と`git rev-parse HEAD`で確認できる。BinaryはGitへ入れず、ローカル`.research/fastest-e1-trial/artifacts/firmware-app-only.bin`へ保管した。

### Forkと既存Branchの反映

ユーザー指示により[taichan1113/solar_os](https://github.com/taichan1113/solar_os)を`nilseuropa/solar_os`からForkし、Remote **`fork`**（`https://github.com/taichan1113/solar_os.git`）を追加した。既存origin=`https://github.com/nilseuropa/solar_os.git`は変更していない。

E1を含む**48本**のCommit済み開発／研究Branchを通常Pushし、全Remote HEADのSHA一致を確認した。既存のmain／betaはPush対象外とし、Forkのmain=`bc1fa25f57d1201c92512dcc1977af00f1a60c74`、beta=`516c0a5d4f9384759f8979ed916df4eceeb56662`は不変。Force Push、Merge、元BranchのCommitは行っていない。追跡先を設定したのは新E1 Branchだけ。

Worktree：`E:\ESP32\M5PapserMono-SolarOS\.research\fastest-e1-trial\worktree`。既存32 WorktreeのBranch／HEAD／status／未CommitファイルのSHAを開始時と比較し、すべて不変を確認した。元Worktreeの`.vscode/`も未Commitのまま保持。未Commitファイルとignored研究資料はForkへ自動反映されない。

### 保存した証拠

- [Host回帰ログ](evidence/fastest-e1-trial/host-regression.log)、[Host結果](evidence/fastest-e1-trial/host-regression-result.json)
- [Buildログ](evidence/fastest-e1-trial/build.log)、[Build結果](evidence/fastest-e1-trial/build-result.json)、[Binary・構成manifest](evidence/fastest-e1-trial/manifest.json)
- [48 BranchのPush照合](evidence/fastest-e1-trial/push-result.json)、[Pushログ](evidence/fastest-e1-trial/push-branches.log)、[既存Worktree保護結果](evidence/fastest-e1-trial/worktrees-preserved.json)
- [証拠ファイルSHA-256](evidence/fastest-e1-trial/checksums.json)

Push照合は資料Commit前のE1実装SHAを記録し、他の47 BranchのSHAも列挙する。資料Commit後はE1だけを通常のfast-forward Pushし、Remote HEADと最終HEADの一致を改めて確認する。最終Git statusはcleanで引き渡す。

## 別PCへの引継ぎ・未確認事項

[手動Flash・Rollback・肉眼試験手順](evidence/fastest-e1-trial/manual-flash-and-test.md)を参照。補助`papermono_e1_flash_audit.py`はファイル解析のみでCOMを開かない。実機へ書く前に毎回freshバックアップ・slot／partition照合を行う。過去の固定offset・古いNVS/FATを復元に流用しない。

E1の実画面の黒保持、一瞬の白化、高速入力／削除時の濃さ、Sketch品質、BUSY実測、長期影響、実機上の切替、Flash成功は**すべて未確認**。手順の評価表も未確認のまま残す。

別PCでは以下で取得する。Build・slot確認・Application-only Flash・読戻し・Rollbackは、リンク先の手順書に従って人間が実施する。

```powershell
git clone --branch feature/papermono-fastest-e1-trial --single-branch https://github.com/taichan1113/solar_os.git SolarOS-E1
cd SolarOS-E1
git rev-parse HEAD
```
