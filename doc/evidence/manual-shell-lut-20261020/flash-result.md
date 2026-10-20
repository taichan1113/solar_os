# 本体Shell 4モード版の書込み・確認結果

2026-10-20 Ubuntu。本版はApplication-only書込み済み。

- 実装commit: `1e0408f88eefa7f6f02ae848bbe77ac2989dd8bf`。
- Application: 3,465,424 B、SHA-256 `b1e78b6eba3639dff58b89cdab834e60ae89703db4eacdfe88d64a1350b1e218`。
- 新規全16MB backup、実機MAC・security・Partition Table・runtime slot・otadata CRCを照合し、`ota_0 @ 0x10000` のみ書込み。
- Application読戻し一致、Application領域以外の全バイト未変更。Bootloader／Table sector／NVS（BLE bond）／otadata／PHY／ota_1／FATの各SHAも一致。その後reset。
- 起動: Launcher switch=ok、Touch ready、USB Shell応答、boot E-level errorなし。ELF SHA先頭 `919ebf2e7` がBuildと一致。
- Host `writer_d_shell_test` PASS。Firmware Build PASS（124.590秒）。初回はmanualの新コマンド登録漏れで停止し、`doc/manual/commands.md`へ追記後に成功。
- 実機: Launcherのみを閉じ、本体Shell session 0をactiveにした。USB `session send 0 lut N` で本体Shellへコマンドを渡し、USBの読取り専用 `lut status` でA／D（Sketch A）／D（Sketch D）／E1／D（Sketch A）の順を照合。全てerr=0、driver_error=0。これは本体Shellの実行経路確認であり、人のキーボード操作・光学比較の代用ではない。
- 最終状態: 本体Shell表示、D（Sketch A）。USB consoleは閉じた。Writer／Sketchを開いた光学評価は未実施。
- アプリ切替で選択を上書きしていたSketch専用パネルは本Buildで無効。選択はRAM保持、再起動時は既定D（Sketch A）。
- 元checkoutと未追跡 `.vscode/` を保護。pushはしていない。

生ログ・全Flash・rollbackはprivate `.research/manual-shell-lut-20261020/flash-2233/` に保存し、Gitへ含めない。
