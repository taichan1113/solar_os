# PaperMono Files操作凡例の幅対応

2026-10-12。Branch: `fix/files-legend-wrap`。実装commit: `a76fc022`。

## 変更範囲

- `src/apps/solar_os_files.c`: 凡例の項目単位の折り返し、強調文字位置、共通レイアウト、入力カーソルの範囲制限。
- `tests/host/files_layout_test.c`: 実際のFiles描画・ページ移動をセル配列上で検証するHostテスト。
- `tests/host/Makefile`: 新しいテストのビルドと実行。
- `tests/test_files_key_policy.py`: 分割した項目表に対する操作名・強調文字の検証。

Services/TUI/Terminal/Driversは変更していない。ファイル操作、ストレージ、LUT、BLE等のロジックも変更していない。

## 行数と高さ

`solar_os_tui_cols(&files.tui)`の実列数を使う。全8項目を従来と同じ1スペースで連結した長さは70列。70列以上なら従来の1行、未満なら7か所の項目境界を調べ、両行が収まり、長い方の行が最短となる境界を選ぶ。56列では4項目ずつ31列／38列になる。通常モードで38列未満なら完全な2行に収まらないため、既存のtoo-small表示を使う。UIスケール値は参照しない。

80列:

```text
F3 View F4 Edit F5 Copy F6 Move F7 mKdir F8 Delete F9 Zip Alt+S Search
```

56列:

```text
F3 View F4 Edit F5 Copy F6 Move
F7 mKdir F8 Delete F9 Zip Alt+S Search
```

各項目に強調文字の相対位置を持たせ、従来と同じV/E/C/M/K/D/Z/Sを反転太字にする。固定行全体の文字列検索は使わない。

`files_layout()`で凡例行数、下部予約行数、ペイン高さ、一覧件数を求める。通常はメッセージ1行＋凡例1/2行を予約する。一覧はTerminal行数から上部1行、ペイン枠2行、下部予約行数を引いた高さ。描画、進捗ポップアップ、PageUp/PageDownが同じ計算を使う。

| Hostで検証したTerminal | 凡例 | 一覧件数 | ページ移動 |
| --- | --- | --- | --- |
| 80列×23行（100%相当） | 1行 | 18件（従来と同じ） | 18件 |
| 56列×15行（140%相当） | 2行 | 9件 | 9件 |

以前のPageUp/PageDownは通常モードで一覧より1件少ない移動量だった。今回、実表示件数に一致させた。上下端でクランプし、描画時に選択項目が見えるようtopを調整する既存処理を維持する。

Fullscreenの凡例非表示、入力中だけ下部1行を予約する処理、非入力時のメッセージ表示方針を維持した。Launcherは引き続き1ペイン、凡例・メッセージ欄なし。検索／mkdir／ZIP入力と削除確認は凡例より上に表示する。長い入力のカーソルは画面右端にクランプする。

## 自動検証

- Host全体135本: PASS。新しいFilesテストを`make test`に組み込んだ。
- Python全体578件: PASS、4件skip。
- 新しいFilesテストのASan/UBSan実行: PASS。
- PaperMono writerdeck、waveform評価機能付きのPlatformIOビルド: PASS（135.53秒）。
- `git diff --check`: PASS。

Filesテストは0〜120列・0〜30行、Fullscreen／Launcher各組合せ、5種類の入力状態で描画範囲を検証する。有効な通常レイアウトでは全操作名、強調属性、入力／メッセージ表示、進捗ポップアップの領域を照合。65件の両ペインでPageUp/PageDownを反復し、最終項目へ到達し、選択が画面内に収まることを確認した。70列での1行／2行境界、37／38列のtoo-small境界も検証した。

これは表示のHost検証であり、コピー／移動／削除／ZIP処理そのものの新しい実機試験を意味しない。これらの処理は今回変更していない。

## 書き込みと実機確認

COM39、ESP32-S3、MAC `28:84:85:44:79:68`を照合。デバイスとビルドのパーティションテーブルを照合して`ota_0`の`0x10000`へapplicationだけを書き込んだ。NVS/FAT/otadataは書き込み前後で全バイト一致。消去・全領域書き込みは行っていない。

Artifact: `build/papermono-files-legend-trial/firmware.bin`、3,429,696バイト。
SHA256: `c5fbfe959b322eb96d6bef4b705ccd31f5977ce7f7c3f0cd58c6421bb97be700`。
比較記録: `.research/files-legend/flash-20261012-221841/summary.json`。

起動後、UI140%、monoの基本文字サイズ20、向き270、statusbar show、BLE登録1台の保持をUSBで確認。dose評価設定をRAMで再選択し、Filesを起動した。

実機目視のTest A（140%）、B（100%）、C（140%へ復帰）は**確認待ち**。Hostでの描画結果を実機PASSとは扱わない。現時点で自動検証に回帰不具合は見つかっていない。
