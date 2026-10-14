# PaperMono SSD1677 RAM読出し診断と20 MHz Host試作

2026-10-15。対象は `experiment/papermono-ssd1677-dirty-hold-host` の独立Worktree。実機通信、Flash、EPD更新は行っていない。前回のDirty Rectangle / HOLDは試作フラグのままで、通常Firmwareへ統合していない。

## A. RAM読出しの実現性

| 項目 | 結果 | 根拠と制約 |
| --- | --- | --- |
| 配線上の可否 | **B：GPIO14の双方向利用が条件付きで可能** | [M5StackのPinMapと回路図](https://docs.m5stack.com/en/core/PaperMono)はGPIO14をEPDのMOSIとして示し、独立MISOはない。[SSD1677 Rev 1.0 §6.1](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf)は4-wire SPIでもSDI/SDO共通SDAの双方向利用を定義する。[M5GFXのPaperMono設定](https://github.com/m5stack/M5GFX/blob/master/src/M5GFX.cpp)も`spi_3wire=true`だが、同パネルの`readRect`はHost画像を読むだけでController RAM受信の実証ではない。FPC内のBS1設定、返送経路、方向切替時の電気的安全性は資料だけで実証できない。 |
| ESP-IDFでの実装 | API上は可能、現行Driverでは未対応 | [ESP-IDF v5.5.5 `spi_master.h`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_driver_spi/include/driver/spi_master.h)の`SPI_DEVICE_3WIRE`はMOSI線で送受信し、`SPI_DEVICE_HALFDUPLEX`は送信後受信を許す。これはSSD1677の「4-wire SPI」（CS/SCL/DC/SDA）と数え方が異なる。現行の`epd_ssd1677.c:847–860`は送信専用、MISO未接続、両フラグなし。コマンド時DC低、受信時DC高を同一CS期間に保つ経路もない。分割transactionでCSを維持する場合は`SPI_TRANS_CS_KEEP_ACTIVE`に加え`spi_device_acquire_bus()`が必要。 |
| OTP Display Option（0x2D） | 仕様上読めるが、実機は未確認 | [Command Table 0x2D](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf)はOTP Display Optionの読み取りを定義する。これはOTP内容であり、実行中に変更された0x37の現在値と同一とは断定できない。SolarOSは0x37を書いていない。 |
| RAM 0x24 / 0x26 | 仕様上は0x41→0x27で選択して読めるが、実機は未確認 | [Command Table 0x27/0x41](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf)による。0x27の最初の受信byteはdummyとして捨てる。読み出し前の窓・カウンター設定は0x44/0x45/0x4E/0x4Fで変更するため、続く表示処理との排他と状態復元が必要。 |
| 実機診断時のリスク | 現時点では採用しない | DC切替とGPIO14の出力解放に失敗するとバス競合の可能性がある。読出しは[§12.1 Table 12-1](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf)の上限2.5 MHz以下。0x41・アドレス変更後の復元も未実装。 |

**判定：CONDITIONAL。** MISO不在だけでは不可能と判断できないが、安全な受信ハンドオフとFPC上の返送経路が未確認である。「実現可能性が高い場合のみ」という条件に照らし、読出しHost試作は今回追加しなかった。コマンド順のmockだけではGPIO14の競合や読出し値の信頼性を証明できないためである。診断コマンドやOTP書込みも追加していない。

## B. Mode-2でなお未確認のこと

[SSD1677 Rev 1.0の0x37](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf)はF[6]をMode-2 RAM ping-pongの設定とする。実機のOTP値は未読で、0x20後の物理0x24/0x26内容、論理faceの切替、部分同期の成立は未確認である。前回Hostテストが証明したのは**仮定したRAMモデルとの整合性**であり、実ControllerのRAM保持ではない。将来の診断では、OTP読出し値だけでなく、既知の両RAM画像を作った後に各面を更新前後で読み比べる必要がある。実際の表示面を変更しない排他・窓復元・読出し失敗時のfull fallbackを先に成立させる。

## C. SPI 20 MHz試験構成

通常ビルドのfastest初期値40 MHzは維持した。PaperMono専用の`SOLAR_OS_SSD1677_SPI20_TRIAL=1`でfastest初期値を20 MHzとし、試験構成中の`spi-mhz=40`を拒否する。`spi-mhz=10/20`、他のrefresh modeのboard初期値10 MHz、`spi_device_get_actual_freq()`による実効値記録は既存経路のまま。フラグはDirty HOLDと独立し、ビルド選択stampに両フラグを入れて切替時のCMake再構成を保証した。Controller再初期化・mode切替後もDriverの`fastest_spi_hz`を使う。

根拠：SSD1677 Rev 1.0 §12.1は書込み最大20 MHz、読出し最大2.5 MHz。[M5Stack OTP Demo](https://github.com/m5stack/M5PaperMono-OTP-Demo/blob/main/components/EDP_OTP_LUT_demo/src/EDP_SPI.cpp)は20 MHz、[M5GFXのPaperMono設定](https://github.com/m5stack/M5GFX/blob/master/src/M5GFX.cpp)は40 MHzだが、後者はRev 1.0保証値の変更根拠ではない。今回は実機を動かしていないので20 MHz試験構成の実効値も未測定である。

| 10更新のHost同一Trace | SPI byte | 理論転送時間 | 0x20回数 |
| --- | ---: | ---: | ---: |
| 従来全画面、40 MHz | 961,571 | 192.3 ms | 10 |
| 従来全画面、20 MHz | 961,571 | 384.6 ms | 10 |
| Dirty HOLD、20 MHz | 191,360 | 76.5 ms | 10 |

時間は`byte × 8 ÷ clock`の理論下限であり、HostのSPI byte数を使った見積り。初回両面全画面96000 byteのみなら40 MHzで19.2 ms、20 MHzで38.4 ms。コマンド間隔、BUSY、EPD光学応答、ESP32実効clock、実表示完了時間は含まない。DirtyによるSPI byte削減約80.1%は維持するが、物理表示範囲や黒濃度改善は示していない。

Hostでは通常/20 MHz × Dirty OFF/ONの4構成で、OTP→fastest、連続更新、mode切替、Controller RAM不明時のfull fallback、SPI override失敗時のfault latchを確認した。既存DriverテストとDirty Controller RAMモデルもPASS。変更したHost経路はASan/UBSanでもPASS。

ESP32-S3 / `m5paper_mono` / `writerdeck` / `ota`で次の4構成すべてcompile/link成功。Sketch latencyとwaveformの両trialはOFF。各フラグ変更でCMakeが再構成された。最後に通常構成を再ビルドし、生成stampとDriverのcompile commandの両trialフラグがOFFであることを確認した。

| Dirty HOLD trial | SPI20 trial | ESP32 compile/link |
| ---: | ---: | --- |
| OFF | OFF | PASS |
| OFF | ON | PASS |
| ON | OFF | PASS |
| ON | ON | PASS |

ビルド成功は実機SPIの実効値・信号品質・表示品質の確認ではない。

## D. 採用判定

1. **RAM読出し診断：CONDITIONAL。** 受信配線・GPIO方向切替・同一CS下のDC切替・状態復元を確認するまでは実機投入しない。
2. **Dirty Rectangle + 20 MHz：CONDITIONAL。** Host上のSPI効率と20 MHzビルド構成は成立するが、Mode-2物理RAM保持と表示品質は未確認。通常実装への統合は保留する。

## E. 次の判断

Dirty Rectangleは通常版へまだ採用しない。20 MHzは仕様に沿う試験構成として保持し、通常版への切替判断には実効clock、表示時間、品質の少数回実機比較が必要。将来RAM読出しを試すなら、まず基板の双方向SDA経路と2.5 MHz以下の受信を安全に検証し、既知画像の両RAM読出しでMode-2の仮定を確かめる。今回の未解決点は物理RAM保持である。
