# Ubuntu実機試験の公開用証拠（2026-10-20）

全体の結果・限界・親PCへの依頼は[引継ぎレポート](../../m5paper-mono-fastest-e1-ubuntu-followup-20261020.md)に記載。

- `results.json`：Build、2回のApplication-only Flashの保護照合、11試験区間のユーザー目視・カウンタ差分・終了状態。before/afterは同じboot epoch内で比較する。
- `trace-excerpts.log`：私有USBログから取得済みのフィールドを抽出して再整形した記録。原文ログ全体ではない。seqはbootで再使用されるのでcaseとboot_epochを組み合わせる。各ケースは取得時点で保持されていた記録だけを含み、32件を超えた試験の全更新を復元したものではない。

更新数はキー入力数・文字数・線の本数ではなくパネル画像の実更新回数で、UI更新を含む。`err=0`や`restored=1`は目視の黒さのPASSを意味しない。目視評価はユーザー報告の定性的記録で、文字サイズ・描画領域・反復回数の因果を確定していない。

Flash binary、NVS/bond/保存文書を含むバックアップ、生USBログ、写真・動画は同梱しない。Firmware・Build設定を変更したcommitではない。Host回帰はこのPCで再実施していない。
