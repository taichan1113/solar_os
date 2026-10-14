# PaperMono SSD1677 Dirty Rectangle / HOLD 採用前の技術確認

判定: **CONDITIONAL**。Host試作はコマンド列と仮定したRAM保持状態の下で整合するが、実機でのMode-2 RAM保持・ping-pong設定は未確認。またSSD1677 Rev 1.0の書込みSPI上限20 MHzに対し、SolarOSのfastestは実測履歴上40 MHzである。今回はHostテストだけを変更し、通常版への統合、クロック変更、実機Flash・EPD試験は行っていない。

## A. Mode-2 RAM

- SSD1677 Rev 1.0 §6.5はB/W RAMとRED RAMを区別する。コマンド表（印刷頁29）では0x24がB/W RAM、0x26がRED RAMへの書込み、0x27/0x41がRAM読出し選択である。§8.3–8.5は書込み窓・カウンターを定義する。0x22は更新シーケンス選択、0x20はその起動であり、RAM書込みとは別操作。[データシート](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf)
- 同コマンド表の0x37（印刷頁32）では、Display OptionのF[6]がMode-2 RAM ping-pongのenable/disableを指定し、Mode-1では非対応とする。Display OptionはOTPに保存可能。SolarOSの初期化は0x37を明示設定していないため、**現在の実機のF[6]値はソースから確定できない**。0x2DでOTP Display Optionを読む仕様はあるが、今回は実機通信を行わない。[データシート](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf)、`src/drivers/epd_ssd1677.c:278–315`
- SolarOS fastestは0x22=0xCC（cold）/0x0C（warm）から0x20を起動する。試作は初回に0x24/0x26へ全画面を書き、0x20後に0x26へ現在画像を全画面同期する。以降は変更窓の0x24を書き、0x20後に同窓の0x26を同期する。**0x26を「前画像」として使うのはSolarOSのLUT上の解釈**であり、ControllerのRED RAMという物理的役割を言い換えたものではない。`src/drivers/epd_ssd1677_reference.inc:163–237`
- M5GFXはMode-2 activationごとに内部のface parityを反転する（`Panel_SSD1677.inl:774–799`）。一方、Mode-2の0x24/0x26へは毎回両方の**全画面**を書き、コマンド先とfaceを別に扱っている（同`:706–773,852–864,889–905`）。全画面再送はRAM保持や内部faceによる内容変化を前提とせずに済む保守的な実装だと*推論*できる。face parityの反転だけから、物理RAMの内容が入れ替わる、または変わらないとは断定できない。[M5GFX SSD1677実装](https://github.com/m5stack/M5GFX/blob/master/src/lgfx/v1/panel/Panel_SSD1677.inl)
- データシートは0x20後の0x24/0x26の内容保持・物理bank入替え・コマンド番地のalias切替えを明記していない。よって「変更窓以外の両RAM内容が保持され、0x26同期後に次回の旧画像となる」は**Hostモデルの仮定**であり、実Controllerで保証された性質ではない。ping-pong無効時も、0x20後の保持挙動を仕様から確証できない。

Hostでは0x24/0x26のコマンド番地別データ、論理表示画像、M5GFX型の論理face parityを別々に追跡する。全画面初期化→狭い変更→0x26後同期→同領域再更新→離れた領域更新→Reset/Mode切替/Sleep後の全画面fallbackを通し、10回の0x20が両face 5回ずつとなることを確認した。これは**仮定したRAM保持モデルに対するPASS**であり、F[6]や物理RAM保持の実証ではない。`tests/host/epd_ssd1677_dirty_hold_test.c:12–138,175–225,261–309`

## B. SPIクロック

- SSD1677 Rev 1.0 §12.1 Table 12-1（印刷頁44）はVDDIO 2.2–3.7 V、25°C、CL=30 pFでSPI **書込みfSCL最大20 MHz**、読出し最大2.5 MHz。実装が40 MHzで動いた事例はこの定格を変更しない。[データシート](https://files.waveshare.com/upload/2/2a/SSD1677_1.0.pdf)
- SolarOSのboard bindingは10 MHz（`boards/manifests/m5paper_mono.toml:87–89`）で、Service経由の初期値も20 MHz以下に制限される（`src/services/solar_os_ssd1677.c:318–319,482`）。しかしfastest Driverは40 MHzへ切り替える（`src/drivers/epd_ssd1677.c:830–834`、`src/drivers/epd_ssd1677_reference.inc:163`）。[ESP-IDF SPI Master文書](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/peripherals/spi_master.rst)では要求周波数がハードウェアの分周値へ丸められるため、SolarOSは`spi_device_get_actual_freq`をログ化する（`src/drivers/epd_ssd1677.c:86–101`）。既存実機記録ではfastestの**actual=40 MHz**だった（`doc/m5paper-mono-fastest-quality.md:411,455,493`）。今回は実機測定をしていないため現在の個体の実効値は未確認。
- M5Stack OTP DemoはPaperMonoのSPIを20 MHzに設定する（`EDP_SPI.cpp:30,215–218`）。一方、M5GFXのPaperMono設定は40 MHz（`M5GFX.cpp:1988–2033`）。両者は実装上の設定であり、後者はRev 1.0の20 MHz定格を上書きする根拠にはならない。[M5Stack OTP Demo](https://github.com/m5stack/M5PaperMono-OTP-Demo/blob/main/components/EDP_OTP_LUT_demo/src/EDP_SPI.cpp)、[M5GFX PaperMono設定](https://github.com/m5stack/M5GFX/blob/master/src/M5GFX.cpp)
- PaperMono用Demoは`DEPG0397BBS770F3HP-XM`/SSD1677を対象とする。SolarOSの個体のSilicon/仕様revisionを識別する読み取り結果はなく、Rev 1.0以外の上限値は**未確認**。[M5Stack OTP Demo](https://github.com/m5stack/M5PaperMono-OTP-Demo)

推奨設定は、仕様適合を優先して**書込み20 MHz以下**とすること。速度理論値は96,000 Bの両面全画面転送が40 MHzで19.2 ms、20 MHzで38.4 ms（差19.2 ms、コマンド・待機・変換は除外）。今回の10更新Hostトレースでは従来961,571 Bが40/20 MHzで192.3/384.6 ms、試作191,360 Bが38.3/76.5 msの純転送下限となる。Dirty Rectangleは20 MHzに下げても同条件の全画面転送より少ないが、実表示所要時間は未測定。

## C. 採用判断

**CONDITIONAL**。Hostモデルと確認できたコマンド仕様に直接の矛盾は見つからず、SPI削減も有望。ただし0x20後のRAM保持/face動作と実機F[6]は未確認で、試作の次回Dirty更新が依存する条件である。さらに40 MHzは確認したRev 1.0の保証値を超える。通常版への統合判断には、対象個体のDisplay Option・RAM保持挙動を安全な手順で確認し、20 MHz以下での性能/表示品質を別途評価する必要がある。Host PASSを光学品質・黒濃度・実Controllerの成立保証として扱わない。

## D. 変更と検証

変更は`tests/host/epd_ssd1677_dirty_hold_test.c`のみ。0x22値と論理faceを追跡し、物理RAM内容と混同しないようにした。`make -C tests/host -B epd_ssd1677_dirty_hold_test`、`./tests/host/epd_ssd1677_dirty_hold_test`でPASS。Driver、LUT、SPI設定、通常版、実機は変更していない。
