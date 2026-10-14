# PaperMono Hybrid Refresh Host MVP

2026-10-15。判定: **CONDITIONAL**。既存の `fastest` の後に、静止時だけ SolarOS の OTP `partial` を実行する試作を独立ブランチに実装し、Host と ESP32-S3 ビルドを通した。**実機への書込み・表示更新・黒濃度測定は行っていない。** 現在の実用ファームウェアは変更していない。

## 波形と状態の比較

| 経路 | RAM書込み・更新命令 | LUTと電源状態 | 判明している時間・品質 |
| --- | --- | --- | --- |
| SolarOS `fastest` | 毎回 `0x24` に新、`0x26` に旧画像を各48,000 B、`0x21=00`、`0x22=CC`（初回）/`0C`（以後）、`0x20` | `0x32` にカスタムLUTをロード。更新後もawake | 過去の実機測定はwarm約113–114 ms、BUSY約52 ms。固定黒は繰返しで薄まった |
| SolarOS `partial` | 通常は `0x24` に新画像48,000 B、`0x21=00`、`0x22=FF`、`0x20`。Hardware reset後、`0x10=01` sleep | 既存M5Stack OTP Partial由来、`0x32` のカスタムLUTをロードしない | ユーザーは更新途中に黒が薄くなり、終了時に濃く戻ると観察。時間は今回未測定 |
| 過去試験の M5GFX `epd_fast` | 現在画像を両面へ送る独立経路 | M5GFXの波形。SolarOS `partial` と同一ではない | 約315 ms、固定黒の回復なし。**NO-GO** |
| OTP full / `cleanup` | `0x22=F8`で初段、次に`0x26`/`0x24`各48,000 Bと`0x22=14`で後段、各`0x20` | 正規OTP、最後にsleep。 `cleanup` はこの全画面経路を強制 | 過去の約4.3–4.5秒。試験間の初期化・復旧用 |

出典は[`epd_ssd1677_reference.inc`](../src/drivers/epd_ssd1677_reference.inc)、[`epd_ssd1677.c`](../src/drivers/epd_ssd1677.c)、[過去のNO-GO記録](../../../epd-quality-mvp/worktree/doc/m5paper-mono-epd-quality-mvp.md)、[SSD1677 Rev 1.0仕様書 §6.5, §6.6およびcommand table](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf)、[M5Stack OTP Demo](https://github.com/m5stack/M5PaperMono-OTP-Demo)である。相対リンク先の過去記録は同じリポジトリ内の研究worktreeにある。

`fastest` の110 B LUTは、実装上、白→白と黒→黒のクラスに駆動フェーズを設定せず、変化する白→黒、黒→白の2クラスを駆動する。非ゼロのTPは6相、計10 waveform frames（8+2）。白→黒はVSL 2 framesとVSH1 8 frames、黒→白はVSH1 2 framesとVSL 8 framesとデコードされる。これはVS=0の保持クラスに電界が全くないという意味ではない。**OTP LUTの中身は読めていない**ため、`partial` の黒戻りを特定の終盤パルス、DCバランス、VCOMまたはGateの寄与と断定できない。固定黒の薄まりは画像データの変化なしでも観察されたが、どの波形相が原因かも未確定である。

## 採用候補とHost実装

Candidate Aを試作した。SolarOSの `partial` は、過去に失敗した M5GFX `epd_fast` と異なり、ユーザーの目視でも良好な黒品質を示しているため、光学的な改善可能性がある。ただし**fastest後の黒を実際に回復できるかは未検証**。Candidate Bの独立OTP経路は既存 `partial` と重なるため追加しない。Candidate CのDirty Rectangle/HOLDはMode-2物理RAM状態が未確認で、SPI削減はBUSY短縮や黒改善を直接示さないため今回組み込まない。

`SOLAR_OS_SSD1677_HYBRID_TRIAL=1` のビルドにだけ `refresh=hybrid` を公開する。通常構成ではflagはOFFで、コマンドも存在しない。`SOLAR_OS_SSD1677_WAVEFORM_TRIAL=0`のままなのでdose/hold等の波形試験は入らない。SPI20 Trialは独立flagで移植し、Hybrid試験ビルドでは20 MHzに制限する。既存の通常ビルドは従来値のまま。

Hybridは描画要求を待機・統合しない。最初の変更は既存 `fastest` に直送し、Display Async Executorの完了済みSnapshotだけを静止後にメンテナンス更新する。既定の静止時間は1,200 ms、`hybrid-idle-ms=500..30000`で変更可能。最速更新の連続上限は既定6回、`hybrid-max-updates=1..10`で変更可能。これは**試験用の天井であってパネルの安全値ではない**。上限到達後の次の変更はOTP `partial` で表示し、以後fastestに戻る。変化がない通常Snapshotは従来どおりskipするが、明示的な静止時品質更新は同一画像でも1回だけ実行する。

カスタムLUT経路の直後にOTP Partialへ移る時だけ、更新前に `0x26` を明示同期する。変更を伴う上限更新では旧shadow、同一画像の品質更新では最新画像を使い、その後`0x24`に最新画像を書いてOTPを実行する。これによりHostが記録する両RAM面とshadowは一致する。OTP完了後にshadowを確定し、次のfastestはsleepからcold起動する。モード切替は既存barrierを使用し、離脱時は安全なbaseline再形成に戻す。SPI/BUSY失敗時はfaultをラッチし、shadowを成功扱いせず自動再試行を止める。

**未解決の技術条件:** SSD1677 Mode-2のping-pongで `0x24`/`0x26` の物理保持・面切替が実際にどうなるかは仕様とHostモデルだけでは確定しない。ここで加えた事前`0x26`同期は公式OTP Demoの通常partialと同一の手順ではない。両面を明示するHost上の整合策であり、実Controller上の動作、黒品質、DC・寿命を保証しない。この点を実機投入前の明示的リスクとする。

## 検証結果

| 項目 | 結果 |
| --- | --- |
| Driver Host: 1文字→fastest→idle partial→再入力 | PASS。`0x22=CC`→`FF`→`CC`、shadowは各成功後だけ更新 |
| 上限・同一画像・古いSnapshot・失敗 | PASS。上限1の試験で次の変更はOTP、同一画像の通常更新はskip、古い品質SnapshotはI/O前に拒否、`0x26`失敗はfault停止 |
| Async Executor: 最新Snapshot、入力中のidle取消、maintenance中の新規入力、切替・suspend・fault | 既存`display_async_test` PASS。Hybrid専用の実機BLE競合・App切替試験は未実施 |
| SPI転送量（Host記録、コマンド込み） | fastest 96,194 B、idle OTP partial 96,045 B。後者の96 KBはHybridの事前`0x26`同期を含み、通常OTP Partialの約48 KBより多い。速度節約の実績ではない |
| 通常Trial OFFのDriver Host | PASS。`refresh=hybrid` は拒否 |
| ESP32-S3 build/link | `m5paper_mono / writerdeck / ota`、Waveform OFF・Hybrid ON・SPI20 ON・Dirty HOLD OFFで成功。Hybrid OFF・SPI20 OFFの通常構成も成功 |

入力イベントから初回描画**要求**までの遅延は今回計測していない。入力待機ロジック・frame throttleは追加していないが、実機の入力応答、BUSY、光学状態は未測定であり、Hostの数値と混同しない。試験用application-onlyイメージは `.research/hybrid-refresh/artifacts/hybrid-spi20-firmware.bin`、SHA-256 `5fa229a8b367816b552b131368215977c63964a353e78a08e1ef2369a99055d6`（3,449,360 B）。factory imageは使用しない。

## 限定的な実機確認の準備

今回の判定は **CONDITIONAL**。次回、実機評価を明示的に指示された場合のみ、まず現在のポート・チップ識別・partition table・稼働OTA slot・実用アプリイメージを読取確認し、実用アプリのrollbackイメージを確保する。NVS/FAT/otadata/PHYの範囲とhashを記録し、起動slotに対応するapplication partitionだけへ書き、読戻しhashと保護領域の不変を照合する。固定offsetやfactory imageをそのまま使わない。実用ファームウェアへ戻す時も同じapplication-only手順を用いる。

評価操作は正規cleanup後の小さなテスト文書で、`display mode display0 refresh=hybrid`、必要に応じ`display mode display0 hybrid-idle-ms=1200`、`display mode display0 hybrid-max-updates=6`を使用する。最初の文字と少数の追加入力を観察し、更新ログの`REF`、`OTP fast`、BUSY時間、静止時1回のみのOTPを照合する。`refresh=fastest` と `refresh=partial` は比較・切戻し用の既存コマンド。反転、予期しない全画面フラッシュ、残像や黒濃度の悪化、BUSY timeout、入力・保存の異常があれば直ちに停止し、更新回数を増やさない。試験後は実用アプリへ戻し、Shell・Writer・Sketch・Type/Mouseと保存データ・bondを確認する。

以上は**準備手順**であり、この作業では書込みもEPD更新も実行していない。
