# PaperMono Writer同一文字T0/T1・連続入力検証

Branch: `experiment/papermono-writer-optical`。出発点 `06537cc2`。
診断実装commit: `87be1872`。
前回の結果・Files/UI変更は保持。S1/F1の実機試験とT0/T1撮影は完了。
写真ファイルは未受領のため、光学C/Rは未測定。下記の更新履歴と目視結果だけで定量的な濃度低下を証明したとは扱わない。

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
- trial build 122.70秒、production build 123.48秒、SUCCESS。productionに診断markerがないことをartifactで確認済み。

COM39、ESP32-S3/MAC `28:84:85:44:79:68`、既存application SHA・partition・OTA選択を照合し、
0x10000のota_0 applicationだけを書き込み。written-data verification正常。
NVS/FAT/otadata/PHYは前後の全byteが一致した。
証跡: `.research/writer-optical/flash-20261013-121846/summary.json`。
trial 3,439,760 bytes、SHA256 `586b360b5487d7e2cdb439aa0b2070a46079d1cbfe5a140bfac1ea49c775e840`。
production（未flash）3,426,688 bytes、SHA256 `c15c0ffcaab81bb25afb26d6c5888b5838cc3f982bb0b2166c1436a08f892088`。
両試験は `err=0 healthy=1 restored=1` で終了。事前のRAM dose選択・refresh設定を復元する終了経路を通った。
最後のUSB読出しで `refresh=fastest`・SolarOS 4.16.1 / writerdeckを確認。
ユーザーもShellの文字・白背景が正常と確認した。boot waveformは変更していない。

## 実機・判定

2026-10-13、COM39、同一trialでS1/F1を各1バッチ実施。各開始前にユーザーから許可を受領し、自動で条件を切り替えていない。
白画面・固定参照をOTPで初期化し、最初のaでT0撮影、以後bcdefghijは途中の観察停止なし。
T1撮影・観察の確認後にcleanup。両条件で全10文字を表示し、イベント欠落・overflow・driver errorは0。

| 指標 | S1 Slow | F1 Fast |
|---|---:|---:|
| 入力 / Render / Present要求 | 10 / 10 / 10 | 10 / 10 / 10 |
| 物理更新数 | 10 | 6 |
| aの後続保持更新数 | 9 | 5 |
| pending置換数 | 0 | 4 |
| burst入力間隔の平均 | 162.470ms | 60.463ms |
| burst物理更新間隔の平均 | 162.415ms | 116.714ms |
| b送信〜j表示完了 | 1460.768ms | 629.715ms |
| Driver total p50 | 111.891ms | 114.326ms |
| observer p50 | 0.993ms | 0.991ms |
| observer min–max | 0.984–1.000ms | 0.977–1.030ms |
| aの初回 / 終了時コントラスト | 未測定 / 未測定 | 未測定 / 未測定 |
| a / 固定参照の保持率R | 未測定 / 未測定 | 未測定 / 未測定 |
| 最初のaの目視評価 | T0より薄くなった | T0より薄くなった |

連続入力中の平均にはT0撮影待ちを含めていない。初回a更新→b更新の開始間隔はS1 57.723秒、F1 47.602秒で、撮影・回答・再開の待ち時間に由来する。
その後b〜jの間にoperator gateはない。入力送信→Writer受付は中央値S1 14µs、F1 11µs。
前回約11.7〜11.9msのobserverに対し、今回は約1ms。負荷は低減したが0ではなく、通常Writerと完全に同じ時間特性とは断定しない。

物理表示の履歴（generation:文字数）:

- S1: `3:1, 4:2, 5:3, 6:4, 7:5, 8:6, 9:7, 10:8, 11:9, 12:10`。置換なし。
- F1: `16:1, 17:2, 19:4, 21:6, 23:8, 25:10`。generation `18,20,22,24` はpendingで置換され、物理表示していない。

全物理更新でSPI40MHz、tile、2 RAM plane転送、activation 1回を確認。
固定ROI `[4,37,250,30]` は黒703画素が00、白6797画素が11で保持。
最初のa `[4,147,10,22]` は黒47画素。初回を除く全更新で変更画素0・保持黒00が47、座標不変。
両条件の初期黒マスクも一致している。全画面変更数・boundsは低負荷モードでは未測定と明記し、ROIの不変性と混同しない。

T0/T1の測定可能時刻（PC受信 / 合図、JST。撮影時刻とは別）:

| 条件 | T0 | T1 |
|---|---|---|
| S1 | 12:28:37.630 | 12:29:36.305 |
| F1 | 12:33:27.783 | 12:34:15.521 |

各pointのdevice readyは対応する物理完了の約2秒後。
ユーザーはiPhoneを一定距離で固定して4枚を撮影したと回答。露出・フォーカス・WBの固定状況、実際の完了→撮影時間は未確認。
写真ファイル未受領のため、EXIF・位置合わせ誤差・背景変動・C/Rの評価は保留。

証跡: [更新履歴JSON](m5paper-mono-writer-optical-results.json)、[指標CSV](m5paper-mono-writer-optical-metrics.csv)。
生USBログ、control、status、回答記録は `.research/writer-optical/` に保持し、BLEログは提出JSONに含めない。

暫定評価:

- H1: 両条件で同じaの薄まりを目視確認。数値濃度は未測定。
- H2: Slow/Fastの保持率差は未判定。回数と間隔が同時に変わるため、どちらかの因果効果と断定できない。
- H3: 固定参照・最初のaの論理画素不変と保持黒00を実機で確認。
- H4: observer負荷は低減。撮影条件と撮影時刻の不確実性は残る。

現段階は **CONDITIONAL（写真解析待ち）**。追加の実機試験は開始しない。
写真を受領して測定誤差を評価した後、回数/間隔の追加切り分けを優先する根拠があるか判断する。
明確な濃度低下が確認できなければ調査を一区切りとし、無制限に再試験しない。
Sketch改善の実装にはこのタスク中に着手しない。
