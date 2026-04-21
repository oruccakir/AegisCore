/*
 * AegisCore Edge Node — Phase 1a: bare-metal LED blink.
 *
 * Target : STM32F407G-DISC1 (STM32F407VGTx, Cortex-M4F @ 168 MHz)
 * Output : Green LED on PD12 toggles every 500 ms.
 *
 * This is the smallest runnable firmware that exercises:
 *   - HAL initialization
 *   - Clock tree configuration (HSE -> PLL -> 168 MHz SYSCLK)
 *   - GPIO driver
 *   - SysTick-based HAL_Delay
 *
 * No RTOS, no state machine, no UART yet — those arrive in later phases.
 */

#include "stm32f4xx_hal.h"

namespace {

constexpr uint32_t kBlinkPeriodMs = 500U;
constexpr uint16_t kLedGreenPin   = GPIO_PIN_12;  /* STM32F407G-DISC1: PD12 */
GPIO_TypeDef* const kLedGreenPort = GPIOD;

void SystemClock_Config();
void GpioInit();
[[noreturn]] void ErrorHandler();

} // namespace

extern "C" int main()
{
    /* Reset all peripherals, initialize Flash prefetch, configure SysTick. */
    HAL_Init();

    /* Crank the clocks to 168 MHz. */
    SystemClock_Config();

    /* Bring up PD12 as push-pull output. */
    GpioInit();

    while (true)
    {
        HAL_GPIO_TogglePin(kLedGreenPort, kLedGreenPin);
        HAL_Delay(kBlinkPeriodMs);
    }
}

namespace {

/**
 * Configure the system clock to 168 MHz using the 8 MHz HSE on the DISC1 board.
 *
 * Clock tree:
 *   HSE (8 MHz)  -> PLLM=8  -> 1 MHz VCO input
 *                -> PLLN=336-> 336 MHz VCO
 *                -> PLLP=2  -> 168 MHz SYSCLK
 *                -> PLLQ=7  -> 48 MHz  (USB/SDIO/RNG)
 *
 * AHB=168, APB1=42 (max), APB2=84 (max). Flash latency 5 WS at 168 MHz / 3.3 V.
 */
void SystemClock_Config()
{
    RCC_OscInitTypeDef  osc  = {};
    RCC_ClkInitTypeDef  clk  = {};

    /* Voltage scaling 1 required for 168 MHz. */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType       = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState             = RCC_HSE_ON;
    osc.PLL.PLLState         = RCC_PLL_ON;
    osc.PLL.PLLSource        = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM             = 8;
    osc.PLL.PLLN             = 336;
    osc.PLL.PLLP             = RCC_PLLP_DIV2;
    osc.PLL.PLLQ             = 7;

    if (HAL_RCC_OscConfig(&osc) != HAL_OK) { ErrorHandler(); }

    clk.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK
                       | RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;   /* 168 MHz */
    clk.APB1CLKDivider = RCC_HCLK_DIV4;     /*  42 MHz (max) */
    clk.APB2CLKDivider = RCC_HCLK_DIV2;     /*  84 MHz (max) */

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) { ErrorHandler(); }
}

void GpioInit()
{
    __HAL_RCC_GPIOD_CLK_ENABLE();

    GPIO_InitTypeDef pin = {};
    pin.Pin   = kLedGreenPin;
    pin.Mode  = GPIO_MODE_OUTPUT_PP;
    pin.Pull  = GPIO_NOPULL;
    pin.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(kLedGreenPort, &pin);

    HAL_GPIO_WritePin(kLedGreenPort, kLedGreenPin, GPIO_PIN_RESET);
}

[[noreturn]] void ErrorHandler()
{
    __disable_irq();
    while (true) { }
}

} // namespace

/* ------------------------------------------------------------------------- */
/* HAL assertion hook — enabled by USE_FULL_ASSERT in stm32f4xx_hal_conf.h.  */
/* ------------------------------------------------------------------------- */
#ifdef USE_FULL_ASSERT
extern "C" void assert_failed(uint8_t* /*file*/, uint32_t /*line*/)
{
    while (true) { }
}
#endif
