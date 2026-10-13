# PaperMono Writer更新回数・更新間隔の切り分け：Stage A停止報告

Branch: `experiment/papermono-writer-optical-isolation`。
出発点: `797ef62c7366a7aae2f9b7ee751d087871906ce7`。
解析実装commit: `08cd0d3db697c02b103fd3259302b669269507a4`。
2026-10-13、今回の判定は **NO-GO（測定環境が未成立）**。
表示方式の不採用判定ではなく、更新回数・間隔の実機比較へ進めないという判定である。

ユーザー回答は「撮影条件の改善は難しい」「PCで元画像を扱える」。
影の少ない均一な照明、AE/AF固定、撮影タイミングの統一を確認できないため、
添付指示の「改善できなければ、更新回数・間隔の比較に進まない」に従って停止した。
予備バッチも行っていない。今回の追加fast物理更新は0回。
元解像度の画像をPCで扱えることと、実際に元画像・適切な撮影条件を取得できたことは区別した。

## 実装の範囲

変更はPC解析・テスト・本報告に限る。

- `tests/hil/measure_writer_optical.py`：独立OTP参照での位置合わせ、撮影条件の事前検査、画像履歴・Exif・撮影時刻の記録。
- `tests/test_writer_optical.py`：濃度変化と位置合わせの独立性、条件不足・失敗時の非推定、時計対応のテスト。
- `doc/m5paper-mono-writer-optical-isolation.md`：本報告。

`--registration-reference fixed` は、画面四隅のhomography後、OTP参照マスクだけで残差の平行移動を推定する。
同じ補正を最初のaなどの測定ROIへ適用する。対象の黒さを最大化して位置を選ばない。
参照の探索範囲と対象ROIが重なる場合、探索端に達した場合、画像が平坦で位置合わせできない場合は拒否する。
参照自身のコントラストは位置合わせに関与するので補助値として扱う。
参照から各ROIへの距離も記録する。単一の平行移動ではレンズ歪みなどの局所的な変形を補正できない。
従来の `--register-local` は過去結果の再現用に残し、Stage Aとの併用は禁止した。

`--stage-a` は不足条件を明示し、不成立の場合は `regions={}` と終了コード2を出す。
濃度値を補完しない。条件が揃っても `ready_for_review` に留め、`stage_b_authorized=false` を維持する。
設定の自己申告を、撮影品質の自動合格や実機試験の開始許可とは扱わない。

現在の実機診断には左端aの旧レイアウトしかない。
中央a・同時にfast形成する反復文字列・回数／間隔指定のtrial拡張は、Stage A不成立で今回は実装しなかった。
旧ログにない中央／反復文字列のマスクを作り足さない。Stage A検査ではこれらが欠けた旧ログを合格にしない。
既存の `baseline slow|fast optical` をA1/A2/B1/B2として流用していない。
通常Writer、保存、checkpoint、recovery、UI140%、z4、Driver、LUT、電圧、SPI、tile、executorは非変更。

## 元画像・撮影条件を整えられた場合の手順

この手順は将来の再開用であり、今回は実施していない。

1. 本体とiPhoneを固定し、均一な照明と影の入らない位置を選ぶ。画面の四隅とOTP参照を撮影範囲に含める。
2. AE/AFを固定し、WB・HDR・自動補正の設定と不確実性を記録する。デジタルズームを使わない。
3. PCとカメラの時計差、時計差の不確実性、表示完了時刻とPC時刻の対応を記録する。
4. 表示完了後の撮影目標時間と許容差を事前に決め、T0/T1で揃える。撮影後は実際のExif時刻を使用する。
5. 元ファイルを `.research/writer-optical-isolation/originals/` 等へコピーして保持する。会話へ貼り付けた縮小画像を元画像として使わない。
6. HEICは解析環境に `pillow_heif` があれば読み込める。未導入時は元寸法を維持した変換を使い、変換履歴と元ファイルも保持する。

元画像の寸法、SHA256、Exif日時・露出・ISO・絞り・WB・色空間・Software等、申告された変換履歴を出力する。
Exif orientation、グレースケール化、3200×1920へのbicubic射影変換、bilinearサンプリングも処理履歴に含める。
解析用の拡大は元解像度の復元とは扱わない。元寸法の自己申告だけでは、アップスケール画像の真正性を証明できない。

撮影時刻は `DateTimeOriginal`、`SubsecTimeOriginal`、`OffsetTimeOriginal` を使う。
タイムゾーンがExifにない場合は明示的な `camera_utc_offset` が必要で、PCの地域設定から推定しない。
`clock_offset_s` はカメラ時計−PC時計で、Exif時刻から差し引く。
`completion_utc` は実機journalとPC時計を対応させた表示完了時刻。
秒精度のExifと時計差の不確実性は別に記録する。実時刻が欠けた場合は未測定にする。

Stage A用corners JSONの追加フィールド（数値・確認項目は実測値で埋める）：

```json
{
  "image_provenance": {
    "T0": {"original_resolution": false, "original_dimensions": null, "conversion_history": []},
    "T1": {"original_resolution": false, "original_dimensions": null, "conversion_history": []}
  },
  "registration_reference_role": "OTP",
  "completion_utc": {"T0": null, "T1": null},
  "clock_offset_s": null,
  "clock_uncertainty_s": null,
  "timing_tolerance_s": null,
  "stage_a_checks": {
    "panel_fixed": false, "camera_fixed": false,
    "shadow_free": false, "uniform_light": false, "ae_af_locked": false,
    "no_digital_zoom": false, "screen_and_reference_visible": false,
    "clock_difference_checked": false, "processing_uncertainty_acceptable": false,
    "timing_uncertainty_acceptable": false, "independent_alignment_reviewed": false,
    "white_variation_acceptable": false
  }
}
```

これにT0/T1の画面四隅座標を追加する。
`--stage-a --registration-reference fixed` を付けた解析は、中央の `first` / `group` マスクも要求する。
それを出力するWriter診断は今回未実装なので、この設定だけで実機比較を開始できるわけではない。
C=(W−B)/W、R=C1/C0、白背景の変動、±0.25画素の位置感度は既存方式を継承。
白MAD由来の代理値を統計的信頼区間とは呼ばない。JPEG/HEICの階調、シャープネス、HDR等は別の不確実性である。

## 自動検証・ビルド

実際のログを確認済み。証跡は `.research/writer-optical-isolation/` に保存。

- Host回帰：137実行、終了コード0。実Writer/raster、ROIの00/11、上限10、fault非再試行、Shell復帰を含む既存テスト。
- Python回帰：605テスト、OK（4skip）。実Hostのopticalログを `WQ_OPTICAL_HOST_DIR` で渡した。
- 光学解析単体：11テストPASS。独立参照、対象の濃度・位置が変わっても対象でposeを再最適化しないこと、探索範囲、条件不足のCLI終了コード2、Exif時刻・時計差・タイムゾーンを検証。
- 合成画像は解析アルゴリズムの検証であり、実機の保持率とは扱わない。
- trial：SUCCESS、PlatformIO 243.00秒（記録スクリプト全体247.86秒）。
- production：SUCCESS、記録スクリプト全体123.08秒。
- trialには `WQ protocol continuous=1 observer=roi` markerが存在し、productionには存在しない。
- 本番Cソースへの差分なし。中央配置・回数／間隔指定の新診断は未実装であり、その実機条件に対するPASSは付けない。

| artifact（未flash） | bytes | SHA256 |
|---|---:|---|
| `build/papermono-writer-optical-isolation-trial/firmware.bin` | 3,434,640 | `9e4879282dba2fb78380b19f1b3b5d90a8879b654afa12d9cf2eb2e474ec1a36` |
| `build/papermono-writer-optical-isolation-production/firmware.bin` | 3,421,664 | `91dca4e6ef8aac8ca7565fc3590c1ee2bca9088bf80d284e090c941815c1bd4c` |

ログ：`host-all.log`、`python-all.log`、`build-trial.log`、`build-production.log`、`build-results.json`。
これらは今回新しく生成したビルド成果物であり、実機に書かれているapplicationとの同一性は主張していない。

## 実機・光学結果

開始時のUSB `version` 読出し：COM39、SolarOS 4.16.1 / writerdeck。
今回はreset、flash、診断バッチを行っていない。新しいapplication SHA照合・保護領域の前後読出しは未実施。
NVS/FAT/otadata/PHY、既存文書・設定・bondを書き換える処理は実行していない。
現在の実機は前回の診断firmwareを維持。前回flashの照合証跡・旧写真・旧実験ログを保持した。

| 項目 | 今回の結果 |
|---|---|
| Stage A予備バッチ | 未実施 |
| 元解像度T0/T1画像 | 今回は未取得 |
| 影・露出・WB・HDR・撮影条件 | 改善困難／成立未確認 |
| ROI黒画素数・マスク整合性 | 新配置は未実装・未測定 |
| 白輝度変動・位置合わせ・感度 | 今回の実写真では未測定 |
| 初回／終了時コントラスト・保持率 | 未測定 |
| 撮影時刻・撮影までの秒数 | 今回は未測定 |

| 比較項目 | A1 | A2 | B1 | B2 |
|---|---|---|---|---|
| 計画上の後続更新数 | 5 | 9 | 5 | 5 |
| 計画上の目標間隔 | 約200ms | 約200ms | 180–200ms | 450–500ms |
| 実施 | 未実施 | 未実施 | 未実施 | 未実施 |
| 実更新数・間隔・Driver p50 | 未測定 | 未測定 | 未測定 | 未測定 |
| C0 / C1 / R / OTP参照R | 未測定 | 未測定 | 未測定 | 未測定 |
| a形成→T1・最終更新→撮影 | 未測定 | 未測定 | 未測定 | 未測定 |
| ROI整合性 | 未検証 | 未検証 | 未検証 | 未検証 |

H1（更新回数）、H2（更新間隔）、H3（経過時間）、H4（初期黒化方式）はすべて **未判定**。
前回S1/F1では更新回数・間隔・経過時間が同時に異なり、写真にも影・縮小・撮影処理等の不確実性があった。
前回の探索的保持率を今回の比較値として再利用せず、単一原因に帰属させない。

## 次に行うこと

本番向けの改善を選べるだけの新しい測定結果は得られていない。自動採用する変更はない。

| 調査案 | 期待効果／遅延／更新回数 | レイヤー・安全性・追加検証 |
|---|---|---|
| 撮影環境・元画像・時計対応を整える | 判定誤差を減らす。通常入力の遅延・更新回数への影響なし | PC解析／撮影。Driver非変更。白背景・位置合わせ・撮影間隔を検証 |
| 環境成立後、中央a＋同時形成反復文字列と回数指定をtrialに追加 | 黒集合を増やし、5対9回を比較。保持改善効果は未推定。完了待ちの診断限定 | Writer trialのみ。最大10回、置換・不足・fault・保存非実行・復帰をHost検証 |
| 回数比較が必要性を示した場合だけ間隔指定を追加 | 同じ5回で約200対500msを比較。長い条件は実験時間が増える。通常Writerの遅延は変更しない | 診断側。Driver/executor非変更。実開始間隔・経過時間交絡を記録。追加実機試験は個別確認 |

Stage A不成立のまま、効果を見るためだけに更新回数を増やさない。
