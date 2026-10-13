# PaperMono Writer 動画による更新履歴比較

Branch: `experiment/papermono-writer-video-quality`。実装・自動検証・予備動画の確認を完了。最初のA1を実施し、ユーザーのカメラ環境変更希望により別の再A1を準備中。A2/B1/B2は未実施。

## 実装

trial限定で、既存RAM Writer診断へ次のコマンドを追加した。

```
display test display0 writer-quality baseline video 5 200
```

末尾の値は後続物理更新数（1〜9）と目標開始間隔ms（180〜2000）。初回を含む物理更新上限は1+指定数、最大10回。入力・render予約・driver入口で制限する。初回は20個の実際のCHARイベントを通常のWriter parse/layout/renderへ渡し、1つのsnapshotで形成する。後続は物理完了を待ち、各1文字を追加する。active/pending/replacementとgeneration、開始・完了、Driver/BUSY/tile/SPI/observerをRAM journalに保存する。異常後の自動再試行はない。

中央の最初のa、20個のa群、独立したOTP参照を追跡する。Hostでの座標はfirst=(304,202,10,22)、group=(304,202,200,22)、fixed=(164,202,120,22)。初回黒画素は47／940／589。実機では実際の座標・マスク・数をjournalから確認する。後続の固定画素差分0、黒00、白11を検証する。

white/reference/T0/T1で停止する。T0/T1は完了+2秒の通知とPC音（880/1100Hz、200ms）で動画音声と対応づける。測定フレームは完了後0.5/1/1.5秒と事前指定し、各時刻以降の最初の実PTSを使う。中間の撮影操作は不要。動画停止・ユーザー確認後にだけ終了gateを進め、既存OTP cleanupとShell再描画を行う。

再A1以降ではcaptureの `--video-auto-following` を使用する。ユーザーが1バッチの開始と参照画面を確認した後、T0完了+2秒通知／PC合図音が済み、初回20CHAR・物理1回・faultなしを確認してT0 gateだけを1度進める。T1の終了gateは手動のまま。複数バッチの自動実行は行わない。これによりT0時点の回答待ち時間をそろえる。最初のA1はT0回答待ちがあり、初回→次回の開始間隔88.24秒だったため、再A1と同一条件の定量結果として混ぜない。

PC解析はFFmpegとPillowを利用する。元動画を上書きしない。四隅の射影と独立OTP参照による位置合わせを全ROIに適用し、同じ初回黒マスクを使う。C=(W-B)/W、R=C_T1/C_T0、中央値・範囲・MAD・白背景変動を記録する。フレームは相関した観測であり独立試行や信頼区間にしない。カメラ補正、圧縮、同期遅延が残るため探索的比較とする。

変更はWriterのtrialコードとHost/Python診断に限定。通常Writer保存、production scheduler、Driver/LUT/VCOM/SPI/tile/async、UI、各platform service、boot waveformは変更していない。productionから新しいvideo診断文字列が除外されていることを確認した。

## 自動検証

- 全Host: 137実行成功。最終Writer品質fixtureも成功。
- 全Python: 最終613テスト成功、4件skip。FFmpeg・実Writer Hostログを指定してvideoテストを実行。
- 実Writer Hostで後続5/200ms、9/200ms、5/500msを検証。物理回数6/10/6、replacement 0、初回group 940画素、後続不変、開始間隔を確認。
- 不正引数、物理上限6/10、途中abort、driver fault、journal欠落・改変を拒否。
- FFmpegの実PTS抽出、既存出力保護、原動画欠落、同期不整合、白背景変動の検出を検証。

trial: 3,436,816 bytes、SHA256 `bba838c9cac85aa4a43f4ca0e8d7f25cdb55d09ba83531698afb155c4f4714bd`。

production: 3,421,632 bytes、SHA256 `777e39cd9f6727151e002cf151969fe271fae3f85b81747e616b4d96725ede29`。

PC解析の最終調整後、video対象8テストが成功。実動画の抽出→四隅射影→独立参照の位置合わせ→940画素group測定を、Hostマスクから作ったlossless動画で通して検証した。音声の880/1100Hz候補検出も検証。欠落・複数候補時には同期を推測しない。

実機flash: COM39、ESP32-S3、MAC `28:84:85:44:79:68`。現行アプリSHA256 `586b360b5487d7e2cdb439aa0b2070a46079d1cbfe5a140bfac1ea49c775e840` とpartition table、OTA選択を照合した。ota_0（0x10000）だけにtrialを書き込み、全NVS／FAT／otadata／PHYの前後バイト比較が一致。Shell起動、version、refresh=fastest、ShellのみのsessionsをUSBから確認。証拠は `.research/writer-video-quality/flash-20261013-155840/summary.json`。書き込みに伴う再起動でRAM設定は初期化されるが、boot waveform・保存設定は変更していない。

## 予備動画（追加fast更新0回）

`IMG_5894.MOV`、SHA256 `0136ac2b59decd9092413dd2e4b9622754a22c16e72c259bbc8e5ec564f13067`。

iPhone 12 mini、H.264、3840×2160、回転後2160×3840、公称30fps、15.67秒、AAC音声。可変フレーム間隔はあり得る。AE/AF/WB固定は未確認。動画メタデータの時計を同期根拠にしない。

事前に20/50/80%を指定し、実PTS 3.166667／7.866667／12.566667秒を抽出。中央領域と白背景は写っており文字が判読できる。大きな画角移動は見られない。論理座標(350,240,200,140)の白背景中央値は3点とも約162（8bit階調）、空間MADは1.0／1.0／1.27。これは3時点の静止Shellの確認であり、露出固定の保証やWriter濃度測定ではない。左端近辺より中央ROIを用いる。

判定: 最初の限定試験を開始できる。数値濃度・保持率は未測定。

## 最初のA1（カメラ環境変更前）

実Writer経路で25CHAR、render／物理更新6回、replacement 0。後続5回内の開始間隔は201.992〜202.103ms、中央値約202.05ms。Driver p50 112.074ms、observer p50 0.785ms。初回Driverは451.005ms（cold更新を別途記録）、後続は約112ms。固定589黒／2051白、group940黒／3460白、最初47黒は後続全回で不変、保持00／11一致。journal検証成功、終了err=0／healthy=1／restored=1。

ユーザー目視: T0正常、PC音は聞こえなかった。T1で録画終了、濃さはほぼ変わらない、背景正常。元動画は未提供。カメラ環境を変更して同じ更新条件でやり直す希望を受けたため、原試行は履歴・目視のみの記録として保持する。C／R／同期は未測定。rawは `.research/writer-video-quality/A1/`。

## 実機比較の予定と判定

| 項目 | A1 | A2 | B1 | B2 |
|---|---|---|---|---|
| 後続更新数 | 5 | 9 | 5 | 5 |
| 目標開始間隔 | 約200ms | 約200ms | 約200ms | 約500ms |
| 実施 | 未実施 | 未実施 | 未実施 | 未実施 |
| C/R・白背景・実間隔・Driver・ROI整合 | 未測定 | 未測定 | 未測定 | 未測定 |

A1/A2を1バッチずつ確認する。Aで有用な関係を観測できた場合にだけBを行い、条件一致時のB1はA1を再利用する。更新回数と追加文字数・経過時間の交絡、Bの間隔と全経過時間の交絡を残す。無更新の待機対照は自動追加しない。

H1（更新回数）、H2（間隔）、H3（時間・撮影）、H4（OTP対fast形成）はすべて現時点で未判定。次の改善案は実測後、最大3案まで整理する。

原動画・抽出PNG・journal・build/flash証拠は `.research/writer-video-quality/` に保存する。
