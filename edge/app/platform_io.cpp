#include "platform_io.hpp"

#include "stm32f4xx_hal.h"

namespace {

using aegis::edge::LedOutputs;

constexpr std::uint32_t kYellowLedPin = GPIO_PIN_13;
constexpr std::uint16_t kGreenLedPin  = GPIO_PIN_12;
constexpr std::uint16_t kBlueLedPin   = GPIO_PIN_15;
constexpr std::uint16_t kRedLedPin    = GPIO_PIN_14;
constexpr std::uint16_t kUserButtonPin = GPIO_PIN_0;
constexpr std::uint32_t kButtonIrqPriority = 6U;

GPIO_TypeDef* const kLedPort = GPIOD;
GPIO_TypeDef* const kButtonPort = GPIOA;

void ErrorHandler()
{
    __disable_irq();
    while (true)
    {
    }
}

void SystemClock_Config()
{
    RCC_OscInitTypeDef oscillator = {};
    RCC_ClkInitTypeDef clock = {};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator.HSEState = RCC_HSE_ON;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator.PLL.PLLM = 8;
    oscillator.PLL.PLLN = 336;
    oscillator.PLL.PLLP = RCC_PLLP_DIV2;
    oscillator.PLL.PLLQ = 7;

    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
    {
        ErrorHandler();
    }

    clock.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK
                    | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock.APB1CLKDivider = RCC_HCLK_DIV4;
    clock.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_5) != HAL_OK)
    {
        ErrorHandler();
    }
}

void GpioInit()
{
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    gpio.Pin = kGreenLedPin | kBlueLedPin | kRedLedPin | kYellowLedPin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(kLedPort, &gpio);

    HAL_GPIO_WritePin(kLedPort, kGreenLedPin | kBlueLedPin | kRedLedPin | kYellowLedPin, GPIO_PIN_RESET);

    gpio = {};
    gpio.Pin = kUserButtonPin;
    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(kButtonPort, &gpio);

    HAL_NVIC_SetPriority(EXTI0_IRQn, kButtonIrqPriority, 0U);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}

aegis::edge::ButtonEdgeCallback g_button_cb  = nullptr;
void*                           g_button_ctx = nullptr;

} // namespace

namespace aegis::edge {

void ApplyLedOutputs(const LedOutputs& outputs)
{
    HAL_GPIO_WritePin(kLedPort, kGreenLedPin,
                      outputs.green_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(kLedPort, kRedLedPin,
                      outputs.red_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(kLedPort, kBlueLedPin,
                      outputs.blue_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(kLedPort, kYellowLedPin,
                      outputs.yellow_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void InitializePlatform()
{
    HAL_Init();
    SystemClock_Config();
    GpioInit();
}

std::uint32_t MillisecondsSinceBoot()
{
    return HAL_GetTick();
}

bool ReadButtonPressed()
{
    return HAL_GPIO_ReadPin(kButtonPort, kUserButtonPin) == GPIO_PIN_SET;
}

void DelayMs(std::uint32_t ms)
{
    HAL_Delay(ms);
}

bool IsHseClockReady() noexcept
{
    return (RCC->CR & RCC_CR_HSERDY_Msk) != 0U;
}

void SetButtonEdgeCallback(ButtonEdgeCallback cb, void* ctx) noexcept
{
    g_button_cb  = cb;
    g_button_ctx = ctx;
}

} // namespace aegis::edge

extern "C" void Aegis_HandleExti0Irq(void)
{
    HAL_GPIO_EXTI_IRQHandler(kUserButtonPin);
}

extern "C" void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin)
{
    if (gpio_pin == kUserButtonPin && g_button_cb != nullptr)
    {
        g_button_cb(g_button_ctx);
    }
}
