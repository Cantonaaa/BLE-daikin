/*
 * 语音控制模块
 * 通过 I2S 麦克风采集音频 → ESP-SR 语音识别 → 调用主模块开关空调
 *
 * 流程：
 *   INMP441 → I2S → AFE(降噪) → WakeNet(唤醒词"你好小智") → MultiNet(命令识别) → 回调
 *
 * 依赖：
 *   - 硬件：INMP441 I2S MEMS 麦克风（BCLK=GPIO6, WS=GPIO7, DIN=GPIO5）
 *   - 软件：ESP-SR v2.4（需额外 clone 到 components/esp-sr）
 */

#include "voice_control.h"
#include <stdio.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"

static const char *TAG = "VOICE_CTRL";

/* I2S 麦克风引脚配置（INMP441） */
#define I2S_BCLK_GPIO 6
#define I2S_WS_GPIO   7
#define I2S_DIN_GPIO  5
#define SAMPLE_RATE   16000  /* ESP-SR 要求的音频采样率 */

/* I2S 和语音引擎的句柄 */
static i2s_chan_handle_t i2s_rx_handle = NULL;
static const esp_afe_sr_iface_t *afe_handle = NULL;  /* 前端处理（AFE）接口 */
static esp_afe_sr_data_t *afe_data = NULL;           /* AFE 实例数据 */
static esp_mn_iface_t *multinet = NULL;              /* MultiNet 命令识别接口 */
static model_iface_data_t *mn_model_data = NULL;     /* 命令识别模型数据 */
static volatile bool voice_active = false;            /* 语音任务运行标志 */
static volatile bool rename_pending = false;           /* 需要同步命令列表 */

/* 回调函数指针，由 main 注册 */
static voice_power_fn power_cb = NULL;      /* 开关空调：power_cb(unit_id, on) */
static voice_get_units_fn get_units_cb = NULL; /* 获取分机列表 */

void voice_control_register_power_cb(voice_power_fn cb) { power_cb = cb; }
void voice_control_register_units_cb(voice_get_units_fn cb) { get_units_cb = cb; }

/*
 * 语音命令执行回调
 * 根据 command_id 执行对应操作：
 *   0xFFFF = 全部开机
 *   0xFFFE = 全部关机
 *   0x00~0xFF = 单台开机（低字节为分机 ID）
 *   0x100~0x1FF = 单台关机（低字节为分机 ID）
 */
static void voice_command_callback(int command_id)
{
    // 全部开机
    if (command_id == 0xFFFF) {
        uint8_t ids[VOICE_MAX_UNITS];
        char names[VOICE_MAX_UNITS][32];
        int cnt = get_units_cb ? get_units_cb(ids, names, VOICE_MAX_UNITS) : 0;
        for (int i = 0; i < cnt; i++) {
            if (power_cb) power_cb(ids[i], true);
        }
        ESP_LOGI(TAG, "ALL UNITS ON");
        return;
    }
    // 全部关机
    if (command_id == 0xFFFE) {
        uint8_t ids[VOICE_MAX_UNITS];
        char names[VOICE_MAX_UNITS][32];
        int cnt = get_units_cb ? get_units_cb(ids, names, VOICE_MAX_UNITS) : 0;
        for (int i = 0; i < cnt; i++) {
            if (power_cb) power_cb(ids[i], false);
        }
        ESP_LOGI(TAG, "ALL UNITS OFF");
        return;
    }
    // 单台开关（command_id < 256 表示开，>= 256 表示关）
    bool on = (command_id < 256);
    uint8_t unit_id = on ? (uint8_t)command_id : (uint8_t)(command_id & 0xFF);
    if (power_cb) power_cb(unit_id, on);
    ESP_LOGI(TAG, "Unit 0x%02x %s", unit_id, on ? "ON" : "OFF");
}

/*
 * 同步语音命令列表
 * 每次分机改名后调用，重新生成语音命令注册表：
 *   - "打开空调" → 0xFFFF（全部开）
 *   - "关闭空调" → 0xFFFE（全部关）
 *   - "打开[分机名]" → 分机 ID（开）
 *   - "关掉[分机名]" → 分机 ID | 0x100（关）
 */
void voice_control_sync_commands(void)
{
    if (!multinet || !mn_model_data) return;

    int cnt = 0;
    uint8_t ids[VOICE_MAX_UNITS];
    char names[VOICE_MAX_UNITS][32];
    if (get_units_cb) cnt = get_units_cb(ids, names, VOICE_MAX_UNITS);

    // 清空并注册全局命令
    esp_mn_commands_clear();
    esp_mn_commands_add(0xFFFF, "打开空调");
    esp_mn_commands_add(0xFFFE, "关闭空调");

    // 为每个分机注册"打开[名称]"和"关掉[名称]"命令
    for (int i = 0; i < cnt; i++) {
        if (names[i][0] == 0) continue;  // 跳过未命名的分机
        char cmd_on[64];
        char cmd_off[64];
        snprintf(cmd_on, sizeof(cmd_on), "打开%s", names[i]);
        snprintf(cmd_off, sizeof(cmd_off), "关掉%s", names[i]);
        esp_mn_commands_add(ids[i], cmd_on);          // 开：command_id = 分机 ID
        esp_mn_commands_add(ids[i] | 0x100, cmd_off); // 关：command_id = 0x100 | 分机 ID
    }

    // 提交更新到识别引擎
    esp_mn_error_t *err = esp_mn_commands_update();
    if (err == NULL) {
        ESP_LOGI(TAG, "Voice commands synced (%d units)", cnt);
    } else {
        ESP_LOGW(TAG, "Sync errors: %d", err->num);
    }
    esp_mn_active_commands_print();
}

/*
 * 改名通知回调（由 daikin_web.c 的 rename_handler 触发）
 * 标记需要同步命令列表，detect_task 会在下一轮循环中执行同步
 */
void voice_control_notify_rename(void)
{
    rename_pending = true;
}

/*
 * 音频采集任务（Core 0）
 * 从 I2S 读取麦克风数据，送入 AFE（前端处理引擎）
 * I2S 数据格式：16kHz, 16bit, 单声道
 */
static void feed_task(void *arg)
{
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int nch = afe_handle->get_feed_channel_num(afe_data);
    size_t buf_size = audio_chunksize * sizeof(int16_t) * nch;

    // 优先从 PSRAM 分配缓冲区，失败则使用内部 RAM
    int16_t *i2s_buff = NULL;
    while (voice_active && !i2s_buff) {
        i2s_buff = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL);
        if (!i2s_buff) {
            ESP_LOGW(TAG, "feed buf alloc failed, retrying...");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    if (!i2s_buff) { vTaskDelete(NULL); return; }

    // 持续采集音频并送入 AFE
    while (voice_active) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(i2s_rx_handle, i2s_buff, buf_size, &bytes_read, portMAX_DELAY);
        if (ret != ESP_OK) continue;
        if (bytes_read == buf_size) {
            afe_handle->feed(afe_data, i2s_buff);
        }
    }

    free(i2s_buff);
    vTaskDelete(NULL);
}

/*
 * 语音检测任务（Core 0）
 * 从 AFE 取处理后的音频数据，依次经过：
 *   1. 唤醒词检测（WakeNet："你好小智"）
 *   2. 命令识别（MultiNet）
 *   3. 回调执行
 * 同时处理改名同步请求
 */
static void detect_task(void *arg)
{
    int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data);

    while (voice_active) {
        // 检测到改名事件时同步命令列表
        if (rename_pending) {
            rename_pending = false;
            voice_control_sync_commands();
        }

        // 从 AFE 取一帧处理后的数据
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 检测到唤醒词
        if (res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "WAKE WORD DETECTED");
            multinet->clean(mn_model_data);  // 清空命令识别器，准备接收指令
        }

        // 唤醒后进入命令识别
        if (res->wakeup_state == WAKENET_DETECTED && res->raw_data_channels == 1) {
            esp_mn_state_t mn_state = multinet->detect(mn_model_data, res->data);
            if (mn_state == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *result = multinet->get_results(mn_model_data);
                if (result->num > 0) {
                    int cmd_id = result->command_id[0];
                    ESP_LOGI(TAG, "COMMAND: %s (id=%d)", result->string, cmd_id);
                    voice_command_callback(cmd_id);
                }
            }
        }
    }
    vTaskDelete(NULL);
}

/*
 * 初始化语音控制模块
 * 步骤：
 *   1. 初始化 I2S 接收通道（INMP441 麦克风）
 *   2. 加载语音模型（esp_srmodel_init）
 *   3. 创建 AFE 前端处理实例
 *   4. 创建 MultiNet 命令识别实例
 *   5. 注册语音命令
 *   6. 启动采集任务和检测任务
 */
esp_err_t voice_control_init(void)
{
    // ==== 1. I2S 初始化 ====
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &i2s_rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .bclk = I2S_BCLK_GPIO,
            .ws = I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_DIN_GPIO,
            .invert_flags = {
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_rx_handle));
    ESP_LOGI(TAG, "I2S initialized (BCLK=GPIO%d, WS=GPIO%d, DIN=GPIO%d)",
             I2S_BCLK_GPIO, I2S_WS_GPIO, I2S_DIN_GPIO);

    // ==== 2. 加载语音模型 ====
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models) {
        ESP_LOGE(TAG, "Model init failed");
        return ESP_FAIL;
    }

    // ==== 3. 创建 AFE 前端处理（降噪、语音增强） ====
    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (!afe_config) {
        ESP_LOGE(TAG, "AFE config init failed");
        return ESP_FAIL;
    }
    afe_handle = esp_afe_handle_from_config(afe_config);
    afe_data = afe_handle->create_from_config(afe_config);
    afe_config_free(afe_config);
    if (!afe_data) {
        ESP_LOGE(TAG, "AFE create failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "AFE initialized");

    // ==== 4. 创建命令识别（MultiNet6 中文） ====
    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_CHINESE);
    if (!mn_name) {
        ESP_LOGE(TAG, "No Chinese model found");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "MultiNet model: %s", mn_name);
    multinet = (esp_mn_iface_t *)esp_mn_handle_from_name(mn_name);
    if (!multinet) {
        ESP_LOGE(TAG, "MultiNet handle failed");
        return ESP_FAIL;
    }
    mn_model_data = multinet->create(mn_name, 6000);
    if (!mn_model_data) {
        ESP_LOGE(TAG, "MultiNet create failed");
        return ESP_FAIL;
    }

    // ==== 5. 注册语音命令 ====
    esp_mn_commands_alloc(multinet, mn_model_data);
    voice_control_sync_commands();

    // ==== 6. 启动任务 ====
    voice_active = true;
    xTaskCreatePinnedToCore(feed_task, "vc_feed", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(detect_task, "vc_detect", 8192, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "Voice control initialized (wake word: 你好小智)");
    return ESP_OK;
}
