// ============================================================
// 调试版 main.c —— 最小初始化，不跑 BLE 协议栈
// ============================================================

#include "tl_common.h"
#include "drivers.h"
#include "main.h"
#include "app.h"
#include "led.h"
#include "uart.h"
#include "epd.h"

_attribute_ram_code_ __attribute__((optimize("-Os"))) void irq_handler(void)
{
    // 不调用 irq_blt_sdk_handler，因为我们不用 BLE
    // 只处理 UART 中断（printf 用）
    irq_handler_uart();
}

_attribute_ram_code_ int main(void)
{
    // 1. 选内部 32k RC（不依赖外部 32.768k 晶振！）
    blc_pm_select_internal_32k_crystal();

    // 2. CPU 唤醒初始化
    cpu_wakeup_init();

    // 3. 判断是否为 Deep Retention 唤醒（我们不用休眠，正常走 else）
    int deepRetWakeUp = pm_is_MCU_deepRetentionWakeup();

    // 4. 时钟初始化（24MHz 外部晶振）
    clock_init(SYS_CLK_24M_Crystal);

    // 5. GPIO 初始化
    gpio_init(!deepRetWakeUp);

    // 6. 打印启动信息
    init_uart();
    printf("\n\n==== TLSR8359 Debug FW ====\n");
    printf("No BLE | No DeepSleep\n");
    printf("Just EPD + GPIO toggle\n");
    printf("If you see this, UART works!\n\n");

    // 7. 调用应用初始化
    if (deepRetWakeUp) {
        printf("(deep retention wakeup - unexpected)\n");
        user_init_deepRetn();
    } else {
        printf("(normal power up)\n");
        user_init_normal();
    }

    // 8. 开中断
    irq_enable();

    // 9. 死循环跑 main_loop（不休眠）
    while (1) {
        main_loop();
    }
}
