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
constexpr std::uint32_t kServoPin = GPIO_PIN_7;
constexpr std::uint32_t kServoTimerChannel = TIM_CHANNEL_2;
constexpr std::uint32_t kServoMinPulseUs = 1000U;
constexpr std::uint32_t kServoMaxPulseUs = 2000U;
constexpr std::uint32_t kServoCenterPulseUs = 1500U;
constexpr std::uint32_t kRangeTrigPin = GPIO_PIN_5;
constexpr std::uint32_t kRangeEchoPin = GPIO_PIN_8;
constexpr std::uint32_t kRangeEchoTimeoutUs = 30000U;

GPIO_TypeDef* const kLedPort = GPIOD;
GPIO_TypeDef* const kButtonPort = GPIOA;
GPIO_TypeDef* const kServoPort = GPIOB;
GPIO_TypeDef* const kRangePort = GPIOB;

TIM_HandleTypeDef gServoTimer = {};
TIM_HandleTypeDef gRangeTimer = {};

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

bool InitializeRangeSensor() noexcept
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM5_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    gpio.Pin = kRangeTrigPin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(kRangePort, &gpio);
    HAL_GPIO_WritePin(kRangePort, kRangeTrigPin, GPIO_PIN_RESET);

    gpio = {};
    gpio.Pin = kRangeEchoPin;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(kRangePort, &gpio);

    gRangeTimer = {};
    gRangeTimer.Instance = TIM5;
    gRangeTimer.Init.Prescaler = 84U - 1U; // 84 MHz APB1 timer clock -> 1 us ticks.
    gRangeTimer.Init.CounterMode = TIM_COUNTERMODE_UP;
    gRangeTimer.Init.Period = 0xFFFFFFFFU;
    gRangeTimer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    gRangeTimer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&gRangeTimer) != HAL_OK) {
        return false;
    }

    if (HAL_TIM_Base_Start(&gRangeTimer) != HAL_OK) {
        return false;
    }

    return true;
}

bool InitializeServoPwm() noexcept
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    gpio.Pin = kServoPin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF2_TIM4;
    HAL_GPIO_Init(kServoPort, &gpio);

    gServoTimer = {};
    gServoTimer.Instance = TIM4;
    gServoTimer.Init.Prescaler = 84U - 1U;      // 84 MHz APB1 timer clock -> 1 MHz.
    gServoTimer.Init.CounterMode = TIM_COUNTERMODE_UP;
    gServoTimer.Init.Period = 20000U - 1U;      // 20 ms period -> 50 Hz servo PWM.
    gServoTimer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    gServoTimer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&gServoTimer) != HAL_OK) {
        return false;
    }

    TIM_OC_InitTypeDef channel = {};
    channel.OCMode = TIM_OCMODE_PWM1;
    channel.Pulse = kServoCenterPulseUs;
    channel.OCPolarity = TIM_OCPOLARITY_HIGH;
    channel.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&gServoTimer, &channel, kServoTimerChannel) != HAL_OK) {
        return false;
    }

    if (HAL_TIM_PWM_Start(&gServoTimer, kServoTimerChannel) != HAL_OK) {
        return false;
    }

    return true;
}

std::uint32_t RangeMicros() noexcept
{
    return __HAL_TIM_GET_COUNTER(&gRangeTimer);
}

void DelayUs(std::uint32_t us) noexcept
{
    const std::uint32_t start = RangeMicros();
    while ((RangeMicros() - start) < us) {
    }
}

bool WaitEchoState(GPIO_PinState state, std::uint32_t timeout_us) noexcept
{
    const std::uint32_t start = RangeMicros();
    while (HAL_GPIO_ReadPin(kRangePort, kRangeEchoPin) != state) {
        if ((RangeMicros() - start) >= timeout_us) {
            return false;
        }
    }
    return true;
}

bool MeasureRangeCm(std::uint16_t& distance_cm) noexcept
{
    HAL_GPIO_WritePin(kRangePort, kRangeTrigPin, GPIO_PIN_RESET);
    DelayUs(2U);
    HAL_GPIO_WritePin(kRangePort, kRangeTrigPin, GPIO_PIN_SET);
    DelayUs(10U);
    HAL_GPIO_WritePin(kRangePort, kRangeTrigPin, GPIO_PIN_RESET);

    if (!WaitEchoState(GPIO_PIN_SET, kRangeEchoTimeoutUs)) {
        return false;
    }

    const std::uint32_t pulse_start = RangeMicros();
    if (!WaitEchoState(GPIO_PIN_RESET, kRangeEchoTimeoutUs)) {
        return false;
    }

    const std::uint32_t pulse_us = RangeMicros() - pulse_start;
    distance_cm = static_cast<std::uint16_t>(pulse_us / 58U);
    return true;
}

std::uint32_t MillisecondsSinceBoot()
{
    return HAL_GetTick();
}

bool ReadButtonPressed()
{
    return HAL_GPIO_ReadPin(kButtonPort, kUserButtonPin) == GPIO_PIN_SET;
}

void SetServoAngleDegrees(std::uint8_t angle_degrees) noexcept
{
    const std::uint32_t clamped = (angle_degrees > 180U) ? 180U : angle_degrees;
    const std::uint32_t pulse =
        kServoMinPulseUs +
        ((kServoMaxPulseUs - kServoMinPulseUs) * clamped) / 180U;
    __HAL_TIM_SET_COMPARE(&gServoTimer, kServoTimerChannel, pulse);
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
