# 本体Shell版の試験中止・復元報告

## 利用者の実機観察（2026-10-20）

- A／D（Sketch A）／D（Sketch D）／E1のどれも白化する。特にShellで顕著。
- 黒化（黒保持）もしなくなった。
- 利用者から、前の状態へ戻し、報告書だけ更新する指示を受けた。追加の実装修正・光学比較は中止する。

## 確認済み事項と未確定事項

- 本体Shell版のApplicationは `b1e78b6eba3639dff58b89cdab834e60ae89703db4eacdfe88d64a1350b1e218`。Application-only書込みと保護領域照合、起動、4モード選択の反映は確認済み。
- これらの確認は濃さ・白化・黒保持の合格を意味しない。今回の利用者観察により光学評価は不合格として扱う。
- 今回の変更はShellコマンド追加、Terminalからの選択許可、Sketch専用パネル無効化。描画Driver／LUT／電圧／Partitionは直前版から未変更。
- Shellへ詳細状態を多数行出力するため、スクロールと画面更新範囲が比較条件に影響する可能性はある。ただし白化・黒保持消失の原因と確定していない。前回答の説明は仮説であり、解決確認ではない。
- 黒保持消失の原因は未確定。変更量が小さいことを理由に利用者観察を否定しない。

## 復元

利用者の指定「全アプリD、ただしSketchのみA」に従い、本体UI追加前の診断版（`aacd0f819d2b5e008f0029c98ddd44ceb49d6c39`）を復元した。Firmware新規Buildは行わず、以前書込み済みのApplicationを使用。

- 復元Application: 3,464,704 B、SHA-256 `a16010b78be790704abc42dd890c4575d90a0d172600c739aea97b93ab96b325`。
- 現在のFirmwareも全16MB新規バックアップを取得して保護。実機MAC・security・16MB容量・Partition Table・runtime/boot slot・otadata CRCを再確認。
- `ota_0 @ 0x10000` にApplication-only復元。Application読戻し一致、対象Application外の全バイト未変更。Bootloader／Table／NVS・BLE bond／FAT／otadata／PHY／ota_1の各SHA一致後にreset。
- 起動ログ: Power IC `update_reg(34): register read failed` が1件。その後Touch readyと `launcher switch=ok`、USB Shell応答を確認。既知の起動時読取失敗として記録し、白化・黒保持不良の原因と断定しない。
- 状態: `fastest-lut err=0 mode=D scope=all-apps-except-sketch sketch_d_trial=0`、driver_error=0。今回追加の本体Shell `lut` コマンド／Sketch専用パネルはこの診断版に含まれない。
- Launcher active、Writer／Sketchなし。USB接続は閉じた。新たな入力・描画比較や黒保持の再評価は行っていないため、光学症状が解消したとは未判定。
- private証拠: `.research/manual-shell-lut-20261020/rollback-to-d-sketch-a/`。生ログ・全Flash・NVS・文書はGitへ入れない。

Gitの実装履歴は調査用に保持し、今回のソース変更は報告書のみ。元checkoutの未追跡 `.vscode/` は保護し、pushしない。
