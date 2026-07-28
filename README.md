# BLE-Daikin

ESP32-S3 通过 BLE 控制大金 VRV 中央空调的开关。

## 功能

- 多台室内机独立开关
- Web 控制界面（手机/电脑浏览器）
- MQTT / HomeAssistant 支持（TODO）

## 硬件

- ESP32-S3 N16R8
- USB 供电
- 需要在大金室外机 BLE 信号范围内

## 烧录

从 GitHub Actions 下载 `BLE-Daikin-N16R8.bin` 合并固件：

```bash
pip install esptool
esptool.py --chip esp32s3 --port <PORT> write_flash 0x0 BLE-Daikin-N16R8.bin
```

配网：连接 Faikout-xxxx 热点 → 浏览器自动弹出配置页面。
