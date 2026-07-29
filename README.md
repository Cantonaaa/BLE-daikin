# BLE-Daikin

通过 ESP32-S3 的 BLE 控制大金 VRV 中央空调多台室内机的开关。

## 功能

### 核心功能（所有版本）

- ✅ AP 热点配网（首次上电开热点 "BLE-Daikin"，无密码）
- ✅ DNS 劫持（连上热点后手机自动弹出配网页）
- ✅ BLE 扫描 + 手动选择室外机
- ✅ GATT 服务自动发现（不依赖固定 handle）
- ✅ 多台室内机独立开关（动态发现）
- ✅ 断线自动重连已保存设备
- ✅ 分机改名（存 NVS，重启不丢失）
- ✅ 定时开关（存 NVS，支持每台独立设置）
- ✅ Web 控制界面（手机/电脑浏览器）
- ✅ 页面显示 IP 地址
- ✅ WiFi 连接失败 5 次后自动回退 AP 模式

### 语音功能（语音版额外支持）

- 🎤 **离线语音控制** — 说"你好小智"唤醒，6 秒内说指令，全程不联网
- 🎤 **分机名动态同步** — Web 改名后自动注册为语音命令，改名即能喊
- 🎤 **中文命令词** — 支持"打开空调""关闭空调""打开客厅""关掉卧室"等

## 两个版本

| 版本 | 适用硬件 | 固件下载 |
|---|---|---|
| **核心版** | 任何 ESP32-S3 N16R8 | `BLE-Daikin-N16R8-all.bin`（v1.1.2） |
| **语音版** | 需外接 I2S 麦克风 | `BLE-Daikin-VoiceControl-all.bin`（v1.1.2vc） |

语音版在核心版基础上增加了离线语音识别，使用方法不变。

## 硬件要求

### 通用要求

- ESP32-S3（推荐 N16R8，16MB Flash / 8MB Octal PSRAM）
- USB 供电
- 需要在大金室外机 BLE 信号范围内（约 10-20 米）

### 硬件适配

本固件默认配置为 **ESP32-S3 N16R8**。其他型号需修改 `ESP/sdkconfig.defaults`：

| 型号 | Flash | PSRAM | 修改方式 |
|---|---|---|---|
| **N16R8** | 16MB | 8MB Octal | ✅ 当前默认配置 |
| **N8R2** | 8MB | 2MB Quad | `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y`<br>`# CONFIG_SPIRAM_MODE_OCT is not set`<br>`CONFIG_SPIRAM_MODE_QUAD=y` |
| **N4R2** | 4MB | 2MB Quad | `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y`<br>`# CONFIG_SPIRAM_MODE_OCT is not set`<br>`CONFIG_SPIRAM_MODE_QUAD=y` |
| **无 PSRAM** | 任意 | 无 | 删除所有 `CONFIG_SPIRAM*` 行 |

### INMP441 麦克风接线（语音版）

| INMP441 | ESP32-S3 |
|---------|----------|
| VDD     | 3.3V     |
| GND     | GND      |
| SCK     | GPIO 6   |
| WS      | GPIO 7   |
| SD      | GPIO 5   |
| L/R     | GND      |

## 语音指令

说 **"你好小智"** 唤醒 → 6 秒内说指令：

| 指令 | 效果 |
|---|---|
| **"打开空调"** | 所有室内机开机 |
| **"关闭空调"** | 所有室内机关机 |
| **"打开[分机名]"** | 指定分机开机 |
| **"关掉[分机名]"** | 指定分机关机 |

分机名以 Web 页面显示的为准。例如改名为"客厅""卧室""书房"后，可以说：
> "打开客厅" → 客厅开机  
> "关掉卧室" → 卧室关机  
> "关闭空调" → 全部关机

## 快速开始

### 编译

```bash
cd ESP
idf.py build
```

编译语音版需额外 clone ESP-SR：
```bash
cd ESP
git clone --depth 1 https://github.com/espressif/esp-sr.git components/esp-sr
idf.py set-target esp32s3
idf.py build
```

### 合并为单个文件

```bash
esptool.py --chip esp32s3 merge_bin --output BLE-Daikin-all.bin \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/BLE-Daikin.bin
```

### 烧录

```bash
esptool.py --chip esp32s3 --port <PORT> write_flash 0x0 BLE-Daikin-all.bin
```

将 `<PORT>` 替换为实际端口：`COM3`（Windows）、`/dev/ttyACM0`（Linux）或 `/dev/cu.usbserial-xxxx`（macOS）。

### 使用

| 步骤 | 操作 |
|---|---|
| **首次配网** | 上电 → 手机连热点 "BLE-Daikin" → 手机自动弹出配网页 → 选你家 WiFi 输密码 |
| **日常使用** | 浏览器打开 `http://<ESP32-IP>`（页面顶部会显示 IP） |
| **连接空调** | 点 "扫描蓝牙设备" → 在列表中选中室外机 → 点"连接" |
| **开关控制** | 连接后自动显示各室内机，点"开"/"关"按钮 |
| **改名** | 点击分机名称，输入新名字 |
| **定时** | 设置开/关时间，到点自动执行 |
| **语音控制**（语音版） | 说"你好小智"唤醒 → 说指令 |

## 项目结构

```
BLE-Daikin/
└── ESP/
    ├── CMakeLists.txt              # ESP-IDF 项目入口
    ├── sdkconfig.defaults          # N16R8 + NimBLE 配置
    ├── partitions_16m.csv          # 16MB 分区表
    ├── main/
    │   ├── main.c                  # 入口：WiFi + AP + SNTP + 任务
    │   └── CMakeLists.txt
    └── components/
        ├── ble-daikin/
        │   ├── CMakeLists.txt
        │   ├── include/
        │   │   ├── ble_daikin.h     # BLE API 声明
        │   │   └── daikin_web.h     # Web API 声明
        │   ├── ble_daikin.c         # NimBLE GATT 客户端
        │   └── daikin_web.c         # HTTP 服务器 + Web UI
        └── voice-control/           # 语音版独有
            ├── CMakeLists.txt
            ├── include/
            │   └── voice_control.h
            └── voice_control.c       # 语音控制（INMP441 + ESP-SR）
```

## 技术细节

| 模块 | 说明 |
|---|---|
| **BLE 协议栈** | Apache NimBLE（ESP-IDF v5.4 默认） |
| **控制指令** | Write Request，24 字节二进制，handle 由 GATT 服务发现自动获取 |
| **分机识别** | 从 handle `0x6528` 的通知中解析分机 ID 和 ON/OFF 状态 |
| **数据持久化** | NVS flash 存储 WiFi 凭证、BLE 设备地址、分机名称、定时设置 |
| **时间同步** | SNTP（联网后自动获取 NTP 时间，用于定时功能） |
| **语音引擎**（语音版） | ESP-SR — WakeNet9（唤醒）+ MultiNet6（命令识别） |
| **麦克风**（语音版） | INMP441（I2S, 16kHz, 16bit） |

## Release 版本说明

| 版本 | 说明 |
|---|---|
| **v1.1.2** | 核心版，可直接烧录 |
| **v1.1.2vc** | 语音版，需 INMP441 麦克风 |

完整 Release 列表见 https://github.com/Cantonaaa/BLE-daikin/releases

## 许可证

本项目基于 ESP32-Faikout 的思路，但代码完全重写。
