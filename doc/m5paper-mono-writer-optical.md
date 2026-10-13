# PaperMono Writer同一文字T0/T1・連続入力検証

Branch: `experiment/papermono-writer-optical`。出発点 `06537cc2`。
前回の結果・Files/UI変更は保持。写真は撮影可能との回答を受領したが、実機試験・光学数値はまだ未測定。

## 診断の変更

trialコマンド: `display test display0 writer-quality baseline slow|fast optical`。
以前の6引数の診断はそのまま使える。opticalはbaselineだけを受け付ける。
同じRAM Writer文書・z4/20px・UI140%・R2/800×480・SPI40MHz・tileを使用する。

白と固定参照をOTP cleanupで初期化。開始確認後にaを独立して物理完了まで描画する。
T0は完了から2秒後にready時刻とgenerationを記録し、写真のため停止する。
撮影を確認して次へ進むと、残りbcdefghijを途中のoperator停止なしで入力する。
Slowは毎文字の完了待ち、Fastは60ms目標間隔で入力し、最後だけdrainする。
T1も最終完了から2秒後に同じ停止点を置く。撮影・観察の回答を受けてからcleanupする。
S1→F1の自動移行は行わない。各バッチ開始はユーザーの許可を待つ。

## 低負荷監査

opticalのsnapshot識別は入力行の100×22px領域だけのFNVを使用し、重複は解析で拒否する。
固定参照250×30pxと最初のa（10×22px）のnative VLSB範囲だけで、差分と00/11保持を確認する。
全画面48,000バイトのhash/画素走査をworkerで行わない。
全画面変更画素数・dirty boundsはこのモードでは未測定と明記する。変換や転送の方式は非変更。
observer時間は各回記録し、減ったと仮定せず実機の値で判断する。

T0完了shadowから、論理画素の初期黒マスクを取得する。固定参照とaの各行をMSB-first hexで、終了後のjournalへ出す。
マスク取得はT0の停止中だけであり、b〜jの連続更新に負荷を追加しない。
diagnostic cursor/blink抑制、10submit予約とdriver入口10物理更新上限、正常復帰とfault時の非再試行を継承する。
通常Writer、LUT、VCOM/電圧、executor、本番更新ポリシー、保存・metadata/recoveryは変更しない。

## 撮影手順

1. 横持ち・USB-Cが左側面上寄り。本体とカメラを固定し、画面の四隅が入る位置にする。
2. 照明・露出・ホワイトバランス・フォーカスを固定。可能ならHDR/補正をOFFにする。
3. PCとカメラの時刻を揃える。シャッター時刻またはEXIF日時を保持する。
4. T0/T1の更新完了から2秒後にPCで短い音を鳴らす。合図後に撮影し、遅れの秒数が分かれば記録する。
5. S1-T0/S1-T1、F1-T0/F1-T1を区別して保持する。T0を撮影済みと回答してから連続入力を開始する。

2秒は撮影可能になる時刻であり、実際の撮影時刻を2秒と決めつけない。
PCのpoint受信・cue UTC、deviceのcompleted/ready時刻、gate応答時刻を別々に保持する。
撮影時刻が不明なら経過時間も未測定とする。

## 写真解析

`tests/hil/measure_writer_optical.py`はPillowを使い、EXIF orientation適用後の画面四隅をT0/T1それぞれ指定して位置合わせする。
黒は同じT0マスクの中心画素、白はその周囲から黒の隣接1pxを除いた画素を使用する。
代表輝度のmedianからC=(W-B)/W、R=C1/C0を算出する。
±0.25画素の位置ずれ感度と白領域MAD由来のノイズ代理値も出し、露出・フォーカス等の変動は別に確認する。
代理値は統計的信頼区間ではなく、校正済み反射率・寿命の値でもない。自動でGO判定にはしない。

例: `--analysis S1-analysis.json --t0 S1-T0.jpg --t1 S1-T1.jpg --corners S1-corners.json --output S1-optics.json`。
cornersは`{"T0":[[x,y],...],"T1":[[x,y],...],"camera_conditions":...,"elapsed_after_completion_s":{"T0":...,"T1":...}}`。
四隅の順序は左上→右上→右下→左下。写真の元データは書き換えない。

## 自動検証（実機結果ではない）

- Host回帰137実行PASS。
- 実Writer/raster/SPI両planeのscalar照合、連続Slow/Fast、T0/T1、mask、上限・fault・復帰PASS。
- 低負荷ROI監査を全画面監査と照合し、異常画素の変化も検出した。
- Fastのb〜i中に完了待ちした場合はHostで失敗する。
- Python597件、OK（4skip）。
- 人工写真のファイル読込・画面位置合わせ・相対濃度解析PASS。人工的なaのR=0.75、固定参照R=1を再現した。実機の濃度値ではない。
- trial build 122.70秒、production build 123.48秒、SUCCESS。productionから診断が除外されることはartifactで確認する。

## 実機・判定

S1/F1は未実施。C/R、observer実測負荷、保持率差、GO/CONDITIONAL/NO-GOは未判定。
結果が得られてから、追加の回数/間隔切り分けか、品質実験を一区切りとするかを提案する。
追加試験やSketch改善にはこのタスク中に着手しない。
