# platform_io — Hardware Abstraction for GPIO and Clock

**Files:** `edge/app/platform_io.hpp`, `edge/app/platform_io.cpp`

---

## What this module is

`platform_io` is the single place where the application code talks to the STM32 hardware. It handles:
- Configuring the system clock to 168 MHz
- Initialising the LED GPIO pins (PD12 green, PD14 red)
- Initialising the User Button pin (PA0) with interrupt
- Providing functions to read/write those pins
- Providing the millisecond uptime counter

All other application code calls functions from this module instead of calling HAL directly. This is the "HAL wrapper" principle from ARCH-05.

---

## Hardware mapping

| Signal | STM32 Pin | GPIO Port | GPIO Pin |
|--------|-----------|-----------|----------|
| Green LED | PD12 | GPIOD | GPIO_PIN_12 |
| Red LED | PD14 | GPIOD | GPIO_PIN_14 |
| User Button | PA0 | GPIOA | GPIO_PIN_0 |

The User Button on STM32F407G-DISC1 is active-high (pressed = GPIO_PIN_SET).

---

## Internal helpers (anonymous namespace — not visible outside this file)

### `ErrorHandler()`

```cpp
void ErrorHandler()
{
    __disable_irq();
    while (true) { }
}
```

Called if any HAL initialisation step fails. Disables all interrupts and spins. The watchdog (started just before `InitializePlatform` in the full system) would reset the MCU. In the current code, watchdog is started after platform init, so this is a permanent hang on early failures.

### `SystemClock_Config()`

```cpp
void SystemClock_Config()
```

Configures the clock tree from 8 MHz HSE (external crystal) to 168 MHz system clock.

**Line by line:**

```cpp
__HAL_RCC_PWR_CLK_ENABLE();
__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
```
Enable the power controller clock and set voltage scale 1 — required for 168 MHz operation.

```cpp
oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
oscillator.HSEState = RCC_HSE_ON;
```
Use the external 8 MHz crystal (HSE = High-Speed External oscillator).

```cpp
oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
oscillator.PLL.PLLM = 8;    // 8 MHz / 8 = 1 MHz VCO input
oscillator.PLL.PLLN = 336;  // 1 MHz × 336 = 336 MHz VCO
oscillator.PLL.PLLP = RCC_PLLP_DIV2;  // 336 MHz / 2 = 168 MHz system clock
oscillator.PLL.PLLQ = 7;   // 336 MHz / 7 = 48 MHz for USB (not used here)
```

PLL (Phase-Locked Loop) multiplies the HSE frequency. The formula:
`SYSCLK = HSE × (PLLN / PLLM) / PLLP = 8 × (336/8) / 2 = 168 MHz`

```cpp
clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;    // use PLL output
clock.AHBCLKDivider = RCC_SYSCLK_DIV1;           // AHB = 168 MHz
clock.APB1CLKDivider = RCC_HCLK_DIV4;            // APB1 = 42 MHz (peripheral bus 1)
clock.APB2CLKDivider = RCC_HCLK_DIV2;            // APB2 = 84 MHz (peripheral bus 2)
```

APB1 runs timers, USART2, I2C. APB2 runs USART1, SPI, ADC. The /4 divider for APB1 keeps it within its 42 MHz maximum.

```cpp
HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_5)
```

At 168 MHz, the Flash memory cannot respond in one CPU cycle. `FLASH_LATENCY_5` tells the Flash controller to insert 5 wait states (the CPU waits 5 extra cycles per read from Flash). This is mandatory for correct operation at 168 MHz.

### `GpioInit()`

```cpp
void GpioInit()
```

**Line by line:**

```cpp
__HAL_RCC_GPIOD_CLK_ENABLE();
__HAL_RCC_GPIOA_CLK_ENABLE();
```
GPIOs need their bus clock enabled before they can be used. GPIOD for LEDs, GPIOA for button.

```cpp
gpio.Pin = kGreenLedPin | kRedLedPin;
gpio.Mode = GPIO_MODE_OUTPUT_PP;
```
Configure PD12 and PD14 as push-pull outputs. Both pins configured simultaneously with the OR'd pin mask.

```cpp
gpio.Pull = GPIO_NOPULL;
gpio.Speed = GPIO_SPEED_FREQ_LOW;
```
No pull resistor needed (we drive the pin). Low speed because LEDs don't need fast switching.

```cpp
HAL_GPIO_WritePin(kLedPort, kGreenLedPin | kRedLedPin, GPIO_PIN_RESET);
```
Explicitly turn both LEDs off after init (default might be undefined after reset).

```cpp
gpio.Pin = kUserButtonPin;
gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
gpio.Pull = GPIO_NOPULL;
```
Configure PA0 as interrupt input, triggered on both rising (press) and falling (release) edges.

```cpp
HAL_NVIC_SetPriority(EXTI0_IRQn, kButtonIrqPriority, 0U);
HAL_NVIC_EnableIRQ(EXTI0_IRQn);
```
Enable the EXTI0 interrupt (PA0 is connected to EXTI line 0). Priority 6 — must be higher (lower number) than FreeRTOS task priority threshold (5 × 16 = 80), but we use 6 × 16 = 96 > 80, so `xQueueSendFromISR` is safe to call.

---

## Public functions

### `ApplyLedOutputs(const LedOutputs& outputs)`

```cpp
void ApplyLedOutputs(const LedOutputs& outputs)
{
    HAL_GPIO_WritePin(kLedPort, kGreenLedPin,
                      outputs.green_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(kLedPort, kRedLedPin,
                      outputs.red_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
```

Writes both LEDs in one call, based on the `LedOutputs` struct from the state machine.

### `InitializePlatform()`

```cpp
void InitializePlatform()
{
    HAL_Init();           // initialise HAL, sets SysTick to 1 ms tick
    SystemClock_Config(); // configure 168 MHz PLL
    GpioInit();           // configure LED and button pins
}
```

Called once from `main()` before the scheduler starts.

### `MillisecondsSinceBoot()`

```cpp
std::uint32_t MillisecondsSinceBoot()
{
    return HAL_GetTick();
}
```

`HAL_GetTick()` returns the value of a counter that increments every 1 ms in the SysTick interrupt. On the STM32F407 at 168 MHz with SysTick configured at 1 ms, this counter wraps at ~49.7 days.

### `ReadButtonPressed()`

```cpp
bool ReadButtonPressed()
{
    return HAL_GPIO_ReadPin(kButtonPort, kUserButtonPin) == GPIO_PIN_SET;
}
```

Reads the physical pin state. Called from the EXTI callback to determine the direction of the edge.

### `DelayMs(std::uint32_t ms)`

```cpp
void DelayMs(std::uint32_t ms)
{
    HAL_Delay(ms);
}
```

Blocking millisecond delay using HAL's SysTick-based delay. Used only in `POST_Run()` for the LED self-test. Not safe to call from FreeRTOS tasks after the scheduler starts (use `vTaskDelay` instead).

### `IsHseClockReady()`

```cpp
bool IsHseClockReady() noexcept
{
    return (RCC->CR & RCC_CR_HSERDY_Msk) != 0U;
}
```

Reads the RCC (Reset and Clock Control) CR register bit `HSERDY` — set by hardware when the external crystal has stabilised. Used by `POST_Run()` to verify the clock is healthy.

### `SetButtonEdgeCallback(ButtonEdgeCallback cb, void* ctx)`

```cpp
void SetButtonEdgeCallback(ButtonEdgeCallback cb, void* ctx) noexcept
{
    g_button_cb  = cb;
    g_button_ctx = ctx;
}
```

Stores a function pointer and context pointer. Called from `main()` with `OnButtonEdge` and the button queue handle. Must be called before the scheduler starts, since the ISR may fire immediately.

---

## ISR and HAL callback

### `Aegis_HandleExti0Irq()`

```cpp
extern "C" void Aegis_HandleExti0Irq(void)
{
    HAL_GPIO_EXTI_IRQHandler(kUserButtonPin);
}
```

Called from `stm32f4xx_it.c`'s `EXTI0_IRQHandler`. Clears the EXTI pending flag and calls the HAL EXTI callback.

### `HAL_GPIO_EXTI_Callback(uint16_t gpio_pin)`

```cpp
extern "C" void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin)
{
    if (gpio_pin == kUserButtonPin && g_button_cb != nullptr)
    {
        g_button_cb(g_button_ctx);
    }
}
```

Called by `HAL_GPIO_EXTI_IRQHandler`. Checks that this is the right pin and invokes the registered callback (`OnButtonEdge` in `main.cpp`). The `extern "C"` linkage is required because the HAL was compiled as C and calls this weak symbol by name.
