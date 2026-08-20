// ============================================================
// 调试版 app.c —— 关 BLE / 不休眠 / 上电刷屏 / GPIO 翻转
// 用途：判断 TLSR8359 固件是否真正跑起来
// 判断方法：
//   电流持续 3~8mA 不下降 → 固件在跑 ✅
//   电流掉到微安 → 固件没跑起来 ❌
//   屏幕刷出黑白格 → 屏驱正常 ✅
// ============================================================

#include "tl_common.h"
#include "app.h"
#include "main.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "epd.h"
#include "epd_spi.h"
#include "epd_bw_213.h"
#include "battery.h"
#include "led.h"
#include "uart.h"

// ===== 调试用：翻转 GPIO（示波器/万用表可测） =====
#define DEBUG_PIN GPIO_PB2  // PB2 通常是空闲脚，方便测

// ===== 屏驱选择：强制 BW213（2.13" 250x122） =====
extern void EPD_BW_213_Display(unsigned char *image, int size, uint8_t full_or_partial);
extern uint8_t EPD_BW_213_read_temp(void);

// 画一个棋盘格测试图（黑白交替），证明屏驱在干活
void draw_test_pattern(uint8_t *buf, int size)
{
    int i;
    for (i = 0; i < size; i++) {
        // 棋盘格：0xAA = 10101010，每字节8个像素交替黑白
        buf[i] = 0xAA;
    }
}

// 画一个"半屏黑半屏白"的简单测试图
void draw_half_pattern(uint8_t *buf, int size)
{
    int half = size / 2;
    int i;
    for (i = 0; i < half; i++) buf[i] = 0x00;  // 全黑
    for (i = half; i < size; i++) buf[i] = 0xFF; // 全白
}

// ===== 上电初始化（只跑一次） =====
_attribute_ram_code_ void user_init_normal(void)
{
    // 1. 初始化调试 GPIO
    gpio_set_func(DEBUG_PIN, AS_GPIO);
    gpio_set_output_en(DEBUG_PIN, 1);
    gpio_setup_up_down_resistor(DEBUG_PIN, PM_PIN_PULLUP_1M);
    gpio_write(DEBUG_PIN, 0);

    // 2. 初始化 LED（如果有）
    init_led();

    // 3. 初始化 UART（方便看串口输出）
    init_uart();
    printf("\n\n==== Debug Firmware Start ====\n");
    printf("BLE: OFF | DeepSleep: OFF\n");
    printf("EPD: Forced BW213 (2.13\" 250x122)\n\n");

    // 4. 初始化墨水屏
    printf("Init EPD...\n");
    EPD_init();

    // 5. 上电复位 EPD 驱动 IC
    gpio_write(EPD_RESET, 0);
    WaitMs(50);
    gpio_write(EPD_RESET, 1);
    WaitMs(50);

    // 6. 开屏电源
    EPD_POWER_ON();
    WaitMs(20);

    // 7. 第一次刷屏：棋盘格
    printf("Drawing test pattern 1 (checkerboard)...\n");
    uint8_t *test_buf = (uint8_t *)0x840000; // TLSR8359 SRAM 地址
    draw_test_pattern(test_buf, 250 * 122 / 8);
    EPD_BW_213_Display(test_buf, 250 * 122 / 8, 1); // 1 = full refresh
    printf("Full refresh done.\n");

    WaitMs(3000); // 等 3 秒看清第一屏

    // 8. 第二次刷屏：半黑半白
    printf("Drawing test pattern 2 (half black/white)...\n");
    draw_half_pattern(test_buf, 250 * 122 / 8);
    EPD_BW_213_Display(test_buf, 250 * 122 / 8, 1);
    printf("Second refresh done.\n");

    WaitMs(2000);

    // 9. 关屏电源（省电，但 CPU 不睡）
    EPD_POWER_OFF();

    printf("Init complete. Entering main loop.\n");
    printf("If you see 'loop XXX' every 2s, firmware is ALIVE.\n\n");
}

// ===== 从 Deep Sleep 唤醒后的初始化（我们不用休眠，留空） =====
_attribute_ram_code_ void user_init_deepRetn(void)
{
    // 不启用 deep sleep，这个函数不会被调用
    // 留空以防万一
}

// ===== 主循环（死循环，不休眠） =====
_attribute_ram_code_ void main_loop(void)
{
    static uint32_t loop_count = 0;
    static unsigned long last_tick = 0;

    // 翻转调试脚（示波器测 PB2 应有方波）
    gpio_toggle(DEBUG_PIN);

    // 每 2 秒打印一次（不用 RTC，用 clock_time）
    if (clock_time() - last_tick > 2000000)  // ~2s @ 24MHz 粗略
    {
        last_tick = clock_time();
        loop_count++;
        printf("loop %lu | ALIVE | BLE=OFF | Sleep=OFF\n", loop_count);

        // LED 闪烁：绿灯亮 100ms
        set_led_color(2);  // 绿色
        WaitMs(100);
        set_led_color(0);  // 全灭
    }

    // 每 10 秒刷一次屏（证明屏驱持续工作）
    if (loop_count % 5 == 0 && (loop_count > 0))
    {
        // 只第一次和第五次刷，避免太频繁伤屏
        static uint8_t first_five_done = 0;
        if (!first_five_done) {
            first_five_done = 1;
            printf("Periodic refresh...\n");
            EPD_POWER_ON();
            WaitMs(10);
            gpio_write(EPD_RESET, 0);
            WaitMs(10);
            gpio_write(EPD_RESET, 1);
            WaitMs(10);

            uint8_t *buf = (uint8_t *)0x840000;
            draw_test_pattern(buf, 250 * 122 / 8);
            EPD_BW_213_Display(buf, 250 * 122 / 8, 0); // partial
            EPD_POWER_OFF();
        }
    }

    // 不进 deep sleep！不调用 blt_pm_proc()！
    // 不调用 cpu_sleep_wakeup()！
    // 故意让 CPU 空转，电流保持在几 mA
}
