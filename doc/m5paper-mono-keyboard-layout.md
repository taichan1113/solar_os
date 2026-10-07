# PaperMono: Windows日本語配列の記号入力

2026-10-07。SolarOSの共有HID入力変換に `jp` を追加。
Writer、shell、およびHID usageを渡す入力元で同じ配列を使用する。
既存の `us=0`、`de=1` は保持し、`jp=2` を追加した。
初期設定のない機器は従来どおりUS配列で起動する。

## 使い方

USB shellで `setterm keyboard jp` を実行すると即時に有効になり、
NVSへ保存される。現在値は `setterm keyboard` で確認できる。
US配列へ戻す場合は `setterm keyboard us`。
BLE設定画面、setterm設定画面、Python/Luaの `solaros.ble.layout("jp")`
からも選択できる。BLEの再pairingやZMKの再書き込みは必要ない。

Windows日本語106/109配列の英数入力時の対応に合わせた。
[Microsoftの106配列実装](https://github.com/microsoft/Windows-driver-samples/blob/main/input/layout/fe_kbds/jpn/106/kbd106.c)
の文字テーブルと、
[PFUの日本語配列USB usage表](https://www.pfu.ricoh.com/hhkeyboard/hhkb_support/brochures/lite2scancode.pdf)
でキー位置を確認した。

| HID usage | 通常 | Shift |
|---|---|---|
| 0x1E〜0x27 | 1234567890 | !"#$%&'()、Shift+0は文字なし |
| 0x2D | - | = |
| 0x2E | ^ | ~ |
| 0x2F | @ | ` |
| 0x30 | [ | { |
| 0x32（0x31も対応） | ] | } |
| 0x33 | ; | + |
| 0x34 | : | * |
| 0x36 | , | < |
| 0x37 | . | > |
| 0x38 | / | ? |
| 0x87、ろ | U+005C backslash | _ |
| 0x89、￥ | U+005C backslash | &#124; |

ZMKの `INT_RO` / `INT_YEN` はそれぞれInternational1 / International3。
[ZMKのキー定義](https://github.com/zmkfirmware/zmk/blob/main/app/include/dt-bindings/zmk/keys.h)
にも対応する。キーマップ内の `AT` などの記号名はUS配列のusageと
修飾キーを表すため、JISホストでの出力はWindows日本語設定時と同様に
変わる。SolarOSは受け取ったusageを変換し、キーキャップの印字を
自動判別する処理は追加していない。

￥キーが入力するデータはASCII `U+005C`。フォント上の表示はbackslash。
日本語IME、かな漢字変換は今回の実装範囲に含めない。
半角/全角（usage 0x35）や変換・無変換は文字を挿入しない。

## ショートカットと設定の保持

Writerの拡大は `Ctrl+;` / `Ctrl+Shift+;`、縮小は `Ctrl+-`。
USモードの `Ctrl+=`、DEモードの既存拡大キーは保持した。
保存の `Ctrl+S`、矢印・編集キーは従来のまま。
日本語配列の `Ctrl+]` はSolarOSのapp exitへ、`Ctrl+[` はEscapeへ変換する。
Control文字の規約はSolarOSの既存操作を継続するもので、Windowsの
IMEやすべてのControl組合せを再現するものではない。

`solar_os_input_init()` は共有 `input/layout` を優先し、未設定の場合だけ
旧 `blekbd/layout` を読む。BLE起動時にも旧値を読む二重の復元を削除し、
`setterm keyboard jp` がBLE起動・再接続で上書きされないようにした。
BLE APIでの設定は従来どおり共有設定と旧設定の両方へ保存する。

## 検証

ホストテストで全記号、左右Shift、CapsLock、英字、Controlショートカット、
JP保存・再初期化、旧DEからの移行と共有設定の優先、US/DEへの切り替えを確認。
HIDの7byte/8byteレポートについて0x87/0x89の受信・保持を確認。
入力アクション、スクリプトAPI、BLE画面の既存22テストも通過。

通常版 `m5paper_mono/writerdeck/ota` のビルド成功。静的RAM 52,464 bytes、
flash 3,319,163 bytes。ファームウェアは `build/papermono-jp/firmware.bin`、
同じディレクトリにELFを保存。SHA256:

```text
bd9be7b6c21607813f3655179ece1ea720b6267b73b2a3d670c868bad5ade870
```

COM39のapp領域0x10000のみ更新し、esptoolのハッシュ検証成功。
`setterm keyboard jp` で共有設定へ保存後、実機を再起動して
shellの `keyboard: jp` とPythonの `solaros.ble.layout()` が `jp` のままで
あることを確認した。旧BLE保存領域を書き換える操作は実行していない。
旧BLE値が共有設定を再度上書きしないこともこの起動確認で検証できた。

更新前後の `/transport-test.md` は173 bytesで一致、SHA256:

```text
a82889b15c8d7078570a9de577afc29f9a927742c8bdb6c46b1621073e78eb69
```

saved checkpointはsaved=true、zoom4のまま。Writerを同じ文書で再表示し、
実行時はrefresh=fastest、SPI40MHz、timing off、ログinfo/CDC sink offへ戻した。
起動時のAUTO/OTP方針、表示高速化とz4既定値は前フェーズを保持した。

最後のBLE状態は登録済みtravelers-split 1/1を保持した再接続待ち。
bond消去やpairing置換は行っていない。ユーザーの画面確認時間終了後のため、
追加の打鍵やキーボード再起動は依頼しなかった。実キーボードの全記号打鍵と
印字との一致は未確認として区別する。変換結果はホストテストで確認済み。
COM39は解放し、ビルド・書き込み・シリアル記録プロセスはすべて終了した。

実機ログ: `.research/jp-apply.log`、`jp-reboot-restore-pass.log`、
`jp-final-status.log`。`jp-reboot-restore.log` はブートの残留promptを
次コマンドの応答と誤認した検証スクリプトの失敗記録で、保持確認の根拠から
除外した。残留promptを読み切った再確認ではJP保存・文書保持とも成功。
