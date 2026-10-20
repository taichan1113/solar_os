# 本体UI追加前のUbuntu実機結果

2026-10-20、Source `aacd0f819d2b5e008f0029c98ddd44ceb49d6c39`（実装 `e0009c3d546fa67e809d1d00bcb50349a43ebfa8`）。この記録はUSBによるモード切替／区間制御と、人手による本体描画の結果であり、新しい本体パネルの実機検証ではない。

Application 3,464,704 B、SHA-256 `a16010b78be790704abc42dd890c4575d90a0d172600c739aea97b93ab96b325`。fresh 16MiB backupと実機表／otadata／Running=Bootの監査後、ota_0へApplicationだけを書き込んだ。起動前の全Flash読戻しでApplication一致、Application partition外の全byte不変、保護領域個別SHA一致を確認。

初回起動にPower IC読取り失敗2件、I2C timeout、Launcher切替失敗、Touch disabled。USB Shellは応答。Resetや再Flashをせず、`session create launcher display0`を一度実行するとLauncher activeとTouch readyを確認。ユーザーがSketchへの切替／短い線の描画、Writerの短い入力・削除を確認。Driver／Queue error=0。

| 条件 | 入力後の実更新数 | ユーザーの目視 |
|---|---:|---|
| A・1本目 | 8 | やや濃い、追従性OK |
| A・2本目 | 13（追加5） | 最初の線は薄まっていないように感じる。2本目は始めより終わりが濃い |
| A・3本目／終了 | 16（追加3） | 最初の線は薄まっていないように感じる。新しい線は始めから終わりまで濃い |
| D・1本目 | 2 | やや濃い目、白化なし。追従性は未回答 |
| D・追加描画／終了 | 36（追加34） | 「たくさん書いた」「白化あり」。終了時の線の濃さ／白化の継続性／背景汚れ／追従性は未確認 |
| E1 | 未実施 | ユーザーの5分以内終了指定により中止 |

D準備中にはClear後の追加A 5回／Sketch D 5回があり、ユーザーは指が当たった可能性を述べた。この区間を本試験へ算入せず、Aへ戻して再cleanupした。最後の空Canvas cleanup（OTP 2回）以降に余計な更新がないことを確認してから、D本試験をtrial-startした。Dの実Traceは `source=sketch mode=D 0x20=1 err=0 restored=1`。準備のやり直しと条件履歴を保存。

| 区間 | seq／物理更新 | RAM | BUSY平均 | Queue |
|---|---|---|---|---|
| A | 92–107／16 A | 411300 B、full 2／rows 14、周期同期1 | 56491 us（16標本） | submitted20、replaced4、rejected0 |
| D | 120–155／36 D | 496200 B、full 2／rows 34、周期同期1 | 53691 us（36標本） | submitted42、replaced6、rejected0 |

両区間ともsource Sketchのみ、error=0、overflow=0、pending_start/end=0。BUSYはソフトウェア観測値。会話待ちの休止を含むため、間隔平均を連続描画速度と解釈しない。手動の長さ・速度・本数は厳密に統制されず、最初の一筆の実更新数も異なる。周期同期と目視変化の瞬間はseqへ対応付いておらず、白化や濃さの原因を断定しない。Aの白化／背景汚れは未回答。

Dは予定の最大約20回を超えて36回となった。報告後ただちに区間停止し、追加描画やcleanupで継続しなかった。従って規定の20回比較ではない。Aは全16件のTraceを保持。D最終Traceは124–155の直近32件で、先に保存した120–121と併せ34/36件が残る。122–123の詳細は失われたが、区間統計は36件を集計。

終了時は通常D／Sketch Aへ戻し、USB consoleをclose。Canvasと保存不要Writer試験文書は未保存で保持し、勝手に保存・破棄・resetしなかった。ユーザーにUSBを外せることを伝えた。実機の次回状態は改めて確認する。

private証拠：`/home/bp/Documents/SolarOS/.research/sketch-d-diagnostics/ubuntu-20261020/archive-1650/`。全Flash backup／NVS／bond／FAT／生USBログはGitへ追加しない。本体のみでの切替が必要という指摘を受け、以降の専用branchでStandaloneパネルを追加。今回までの目視結果をその新機能のPASSへ転用しない。
