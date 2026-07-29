# BLE-Daikin

通过 ESP32-S3 的 BLE 控制大金 VRV 中央空调多台室内机的开关。

## 功能

- ✅ AP 热点配网（首次上电开热点 "BLE-Daikin"，无密码）
- ✅ BLE 扫描 + 手动选择室外机
- ✅ GATT 服务自动发现（不依赖固定 handle）
- ✅ 多台室内机独立开关（动态发现）
- ✅ 断线自动重连已保存设备
- ✅ 分机改名（存 NVS，重启不丢失）
- ✅ 定时开关（存 NVS，支持每台独立设置）
- ✅ Web 控制界面（手机/电脑浏览器）
- ✅ 页面显示 IP 地址
- ✅ 16MB Flash / 8MB PSRAM 优化

## 硬件要求

- ESP32-S3 N16R8（16MB Flash / 8MB Octal PSRAM）
- USB 供电
- 需要在大金室外机 BLE 信号范围内（约 10-20 米）

## 快速开始

### 1. 编译

```bash
cd ESP
idf.py build
```

### 2. 合并为单个文件

```bash
esptool.py --chip esp32s3 merge_bin --output BLE-Daikin-all.bin \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/BLE-Daikin.bin
```

### 3. 烧录

```bash
esptool.py --chip esp32s3 --port <PORT> write_flash 0x0 BLE-Daikin-all.bin
```

将 `<PORT>` 替换为实际端口：`COM3`（Windows）、`/dev/ttyACM0`（Linux）或 `/dev/cu.usbserial-xxxx`（macOS）。

### 4. 使用

| 步骤 | 操作 |
|---|---|
| **首次配网** | 上电 → 手机连热点 "BLE-Daikin" → 浏览器打开 `http://192.168.4.1` → 选你家 WiFi 输密码 |
| **日常使用** | 浏览器打开 `http://<ESP32-IP>`（页面顶部会显示 IP） |
| **连接空调** | 点 "Scan BLE Devices" → 在列表中选中室外机 → 点 "Connect" |
| **开关控制** | 连接后自动显示各室内机，点 ON/OFF 按钮 |
| **改名** | 点击分机名称，输入新名字 |
| **定时** | 设置 ON/OFF 时间，到点自动执行 |

## 项目结构

```
BLE-Daikin/
└── ESP/
    ├── CMakeLists.txt              # ESP-IDF 项目入口
    ├── sdkconfig.defaults          # N16R8 + NimBLE 配置
    ├── partitions_16m.csv          # 16MB 分区表（OTA 双 7MB）
    ├── main/
    │   ├── main.c                  # 入口：WiFi + AP + SNTP + 任务
    │   └── CMakeLists.txt
    └── components/
        └── ble-daikin/
            ├── CMakeLists.txt
            ├── include/
            │   ├── ble_daikin.h     # BLE API 声明
            │   └── daikin_web.h     # Web API 声明
            ├── ble_daikin.c         # NimBLE GATT 客户端
            └── daikin_web.c         # HTTP 服务器 + Web UI
```

## 技术细节

| 模块 | 说明 |
|---|---|
| **BLE 协议栈** | Apache NimBLE（ESP-IDF v5.4 默认） |
| **控制指令** | Write Request，24 字节二进制，handle 由 GATT 服务发现自动获取 |
| **分机识别** | 从 handle `0x6528` 的通知中解析分机 ID 和 ON/OFF 状态 |
| **数据持久化** | NVS flash 存储 WiFi 凭证、BLE 设备地址、分机名称、定时设置 |
| **时间同步** | SNTP（联网后自动获取 NTP 时间，用于定时功能） |

## 许可证

本项目基于 ESP32-Faikout 的思路，但代码完全重写。
