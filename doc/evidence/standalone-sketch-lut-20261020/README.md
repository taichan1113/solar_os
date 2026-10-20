> 追記: 本体Shellでの4モード選択が必要なため、後続版ではSketch専用パネルを無効化した。[修正版の操作手順](../manual-shell-lut-20261020/README.md)を参照。以下は専用パネル版の準備時点の記録。

# Standalone Sketch LUT試験版：書込み待ち

本体タッチだけでSketchのA／D／E1を選択・初期化・計測開始／停止できる専用Build。USBは次回の安全な書込みと起動確認に使用し、本体試験中のShell操作は不要。[本体操作・書込み要件](manual.md)／[UI追加前のUbuntu実機結果](prior-ubuntu-observations.md)／[Build・検証manifest](build-result.json)。

Branch `feature/papermono-standalone-lut-trial`、Firmware実装commit `be95f5b6b815fe48b9ef824ab98229f9c8a77a23`。後続のFlash前監査／資料変更はFirmwareに影響しない。

Build **PASS**。最終9.944秒、初回114.287秒。Application **3,466,992 B**、SHA-256 **`752799ffec15605c35fcb4df39d14e4a0a8c8ec509100b0253eba64cd2e7da56`**。表は3,072 B、SHA-256 `fd976d7c1eaa8f9d0a01ac87a8d05fc1cb1e59d3c5970954b30ad0c3aa12a488`。実compile引数の本体UI flag、Application内のUI文字列とcandidate_d_sketchを照合した。Firmware Buildのwarning／errorなし。

Host **PASS**。本体パネル専用target（実Sketchの既存PNG／touch／lifecycle回帰も含む）、既存46 executable target、追加Asyncモード、既存Python 9件、最終Flash監査4件。新UIの取消、3モード、初期Canvasとパネルの区間除外、停止後の統計保持、一筆終了時の約20更新停止、suspend復帰、Driverモード拒否／timeout／OTP未実行時の開始拒否を確認。

Ubuntuにmake／gcc／g++コマンドがなかったため、Ubuntuパッケージを`/tmp`へ展開してGCC/G++ 15.2.0とmakeを使用し、システムへインストールしていない。既存の未変更TypeMouse C++にGCC15の`maybe-uninitialized`警告が出たので、C++側だけ`-Wno-error=maybe-uninitialized`を指定して既存回帰を実行した。新UIのC試験は`-Werror`でPASS。最初の専用test compileではfixture内の`sketch`マクロとsummaryの同名fieldが衝突したため、fixtureの参照名を修正して再実行した。Firmwareの実装制御にはこの修正を加えていない。依存ツール不足とC++警告の失敗ログはprivate資料に保持する。

次回のFlash前監査も修正した。session TITLEに空白がある実出力（`writer untit`／`Sketch * - u`）では旧regexがWriter／Sketchを見落とし得たため、APPとSTATEを見て確実に拒否する。空白／tabのあるtitleと複数語Shell名を回帰で確認。前回の実Flashは実際にWriter／Sketchがない状態で実施済みだった。新しいguarded tool／preflight封印にも同じ保護を適用する。

**この新機能で実機接続・Flash・reset・光学評価は行っていない。** 前版の実機評価を新UIのPASSとして扱わない。Driver／Service／LUT配列／SPIの物理送信シーケンス／電圧／Power／Touch／SDK／Partitionは基点と同一。専用UIは一筆終了後の完了待ちを追加するので、通常Sketchとストローク間の時間条件が完全に同一とは主張しない。20更新はRELEASE境界の停止で、長い一筆は上限を超え得る。

このUbuntuでの準備済み成果物は `/home/bp/Documents/SolarOS/.research/standalone-lut-20261020/ready/`、次回手順はその親の `NEXT-FLASH.md`。App-only binary、照合用table、manifest、SHA256SUMS、新規private archive作成tool、DTR/RTS非操作console、fresh preflight封印、単一接続のbackup→plan→Application-only→全Flash読戻し→照合後resetのguarded toolを保存する。現行Firmwareのbackupとpreflightは次回接続時に新規取得し、以前のslotを決め打ちしない。binary／全Flash／NVS・bond／文書／生USBログはGitへ入れない。pushはユーザーが手動で行う。

準備toolもUSBを開かずoffline確認済み。新規archiveのimage／table／audit SHA照合、空白を含むWriter／Sketch sessionの拒否、複数語Shell titleの受理、古いpreflightと誤imageの拒否を検証した。esptoolの実機entry pointは失敗stubへ置換し、検証中の実機アクセスを防止した。
