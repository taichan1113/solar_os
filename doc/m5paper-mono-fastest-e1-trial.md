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

実装Commit・Host／Buildの確定結果、サイズ・SHA、Remote確認結果は、ビルド完了後にこの欄へ記録する。

Push先：ユーザー指示により`https://github.com/taichan1113/solar_os.git`へFork。既存origin=`https://github.com/nilseuropa/solar_os.git`を維持する。既存のCommit済み開発／研究BranchもForkへ反映し、既存未Commit変更はローカルのまま保護する。

## 別PCへの引継ぎ・未確認事項

[手動Flash・Rollback・肉眼試験手順](evidence/fastest-e1-trial/manual-flash-and-test.md)を参照。補助`papermono_e1_flash_audit.py`はファイル解析のみでCOMを開かない。実機へ書く前に毎回freshバックアップ・slot／partition照合を行う。過去の固定offset・古いNVS/FATを復元に流用しない。

E1の実画面の黒保持、一瞬の白化、高速入力／削除時の濃さ、Sketch品質、BUSY実測、長期影響、実機上の切替、Flash成功は**すべて未確認**。手順の評価表も未確認のまま残す。
