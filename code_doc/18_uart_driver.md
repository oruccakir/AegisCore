# UartDriver — USART2 + DMA Serial Driver

**Files:** `edge/bsp/uart_driver.hpp`, `edge/bsp/uart_driver.cpp`

---

## What this module is

`UartDriver` is the hardware driver for USART2 — the serial port that connects the STM32 to the Arduino USB-UART bridge. It implements the `ISerial` abstract interface so application code never touches HAL directly.

Key features:
- **DMA receive** — the DMA controller moves bytes from USART2 into a buffer automatically, with no CPU involvement per byte
- **IDLE line interrupt** — triggers when the transmitter goes silent (end of frame), even if the DMA buffer isn't full
- **Polling transmit** — simple blocking TX (the short frames don't justify DMA TX complexity)
- **Binary semaphore** — the task blocks until data arrives (no busy-waiting)

---

## Hardware wiring

```
STM32 PA2 (AF7) = USART2 TX  →  Arduino RX (Serial1)
STM32 PA3 (AF7) = USART2 RX  ←  Arduino TX (Serial1)
```

Baud rate: 115200, 8 data bits, no parity, 1 stop bit (8N1). No hardware flow control.

---

## File-scope HAL state (hidden from header)

```cpp
static UART_HandleTypeDef  g_huart2   = {};   // USART2 HAL handle
static DMA_HandleTypeDef   g_hdma_rx  = {};   // DMA1 Stream5 handle
static StaticSemaphore_t   g_rx_sem_buf = {};  // storage for binary semaphore
static SemaphoreHandle_t   g_rx_sem   = nullptr;
static aegis::edge::UartDriver* g_driver = nullptr; // back-pointer for ISR dispatch
```

All HAL state is file-scope (not in the class or the header). This enforces the ARCH-05 rule: HAL types never appear in application-layer headers. The header `uart_driver.hpp` includes only `i_serial.hpp` and `<cstdint>`.

---

## ISR dispatch hooks

```cpp
extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart)
{
    if (g_driver != nullptr && huart->Instance == USART2)
    {
        g_driver->OnRxCpltCallback();
    }
}
```

HAL calls this weak symbol when a DMA receive completes. We check that it's USART2 and forward to the driver object via the `g_driver` back-pointer.

```cpp
extern "C" void UartDriver_OnUsart2Irq(void) { ... }
extern "C" void UartDriver_OnDma1Stream5Irq(void) { ... }
```

These C-linkage wrappers are called from `stm32f4xx_it.c`. They forward to the driver object using the same `g_driver` back-pointer pattern.

---

## `Init()` → `bool`

```cpp
bool UartDriver::Init() noexcept
```

**Purpose:** Configure all hardware. Called once from `main()` before tasks start.

**Step by step:**

```cpp
g_driver = this;
g_rx_sem = xSemaphoreCreateBinaryStatic(&g_rx_sem_buf);
```
Store `this` as the global back-pointer. Create the binary semaphore using static storage (no heap allocation).

```cpp
__HAL_RCC_GPIOA_CLK_ENABLE();
__HAL_RCC_USART2_CLK_ENABLE();
__HAL_RCC_DMA1_CLK_ENABLE();
```
Enable the bus clocks for the peripherals we need. Without enabling the clock, any read/write to the peripheral's registers has no effect.

```cpp
gpio.Pin       = GPIO_PIN_2 | GPIO_PIN_3;   // PA2 = TX, PA3 = RX
gpio.Mode      = GPIO_MODE_AF_PP;            // alternate function push-pull
gpio.Alternate = GPIO_AF7_USART2;            // route to USART2
HAL_GPIO_Init(GPIOA, &gpio);
```
Configure PA2 and PA3 as USART2 pins. AF7 is the Alternate Function number that routes the pins to USART2. This is chip-specific — the STM32F407 datasheet defines which AF number maps to which peripheral for each pin.

```cpp
g_huart2.Init.BaudRate     = 115200U;
g_huart2.Init.WordLength   = UART_WORDLENGTH_8B;
g_huart2.Init.StopBits     = UART_STOPBITS_1;
g_huart2.Init.Parity       = UART_PARITY_NONE;
HAL_UART_Init(&g_huart2)
```
Configure USART2 at 115200 8N1 and initialise the peripheral.

```cpp
g_hdma_rx.Instance                 = DMA1_Stream5;
g_hdma_rx.Init.Channel             = DMA_CHANNEL_4;
g_hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
g_hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;
g_hdma_rx.Init.Mode                = DMA_NORMAL;
HAL_DMA_Init(&g_hdma_rx);
__HAL_LINKDMA(&g_huart2, hdmarx, g_hdma_rx);
```
Configure DMA1 Stream5 Channel4 to receive from USART2's data register and write to memory (`rx_dma_buf_`). `DMA_MINC_ENABLE` means the memory pointer advances after each byte (otherwise it overwrites the same byte). `DMA_NORMAL` mode completes and stops — not circular.

```cpp
HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 6U, 0U);
HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
HAL_NVIC_SetPriority(USART2_IRQn, 6U, 0U);
HAL_NVIC_EnableIRQ(USART2_IRQn);
```
Enable both interrupts at priority 6. FreeRTOS requires that any ISR using `FromISR` APIs has numeric priority > `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` (5). Priority 6 × 16 = 96 > 80 — safe.

```cpp
__HAL_UART_ENABLE_IT(&g_huart2, UART_IT_IDLE);
```
Enable the IDLE line interrupt. This fires when the RX line goes idle after a burst of data — perfect for detecting end-of-AC2-frame.

```cpp
HAL_UART_Receive_DMA(&g_huart2, rx_dma_buf_, kRxBufSize);
```
Start the first DMA receive. DMA will fill `rx_dma_buf_` with up to 66 bytes from USART2.

---

## `Write(const std::uint8_t* data, std::uint8_t len)` → `bool`

```cpp
return HAL_UART_Transmit(&g_huart2, const_cast<std::uint8_t*>(data), len, 10U) == HAL_OK;
```

**Blocking polling transmit.** The CPU waits while bytes are shifted out. At 115200 baud, each byte takes ~87 µs. A 66-byte frame takes ~5.7 ms.

`const_cast` is needed because `HAL_UART_Transmit` takes a `uint8_t*` (non-const) for historical reasons, but doesn't actually write to it.

Timeout = 10 ms. If transmission takes longer than 10 ms, it returns `HAL_TIMEOUT` → `false`.

---

## `Read(std::uint8_t* dst, std::uint8_t max_len)` → `std::uint8_t`

```cpp
const std::uint8_t n = (rx_ready_len_ < max_len) ? rx_ready_len_ : max_len;
for (std::uint8_t i = 0U; i < n; ++i) { dst[i] = rx_dma_buf_[i]; }
rx_ready_len_ = 0U;
HAL_UART_Receive_DMA(&g_huart2, rx_dma_buf_, kRxBufSize);
return n;
```

Copies bytes from the DMA buffer to `dst`. `rx_ready_len_` was set by the ISR before `WaitForData()` returned.

After copying, `rx_ready_len_` is cleared and DMA receive is restarted for the next frame.

---

## `WaitForData()`

```cpp
void UartDriver::WaitForData() noexcept
{
    (void)xSemaphoreTake(g_rx_sem, portMAX_DELAY);
}
```

The `UartRxTask` calls this before `Read()`. It blocks indefinitely until either the IDLE interrupt or the DMA complete interrupt fires and calls `SignalRxData()`. This is efficient — the task consumes no CPU while waiting.

---

## `SignalRxData()` — called from ISR

```cpp
void UartDriver::SignalRxData() noexcept
{
    const auto ndtr    = g_hdma_rx.Instance->NDTR;        // bytes remaining in DMA
    const auto received = kRxBufSize - ndtr;               // bytes already written
    if (received == 0U) { return; }

    rx_ready_len_ = received;
    HAL_UART_AbortReceive(&g_huart2);                      // stop DMA

    xSemaphoreGiveFromISR(g_rx_sem, &woken);               // wake UartRxTask
    portYIELD_FROM_ISR(woken);                             // context switch if needed
}
```

**NDTR** (Number of Data to Receive) is the DMA's countdown register. It starts at `kRxBufSize` (66) and decrements for each byte received. If DMA is partway through, `NDTR = 10` means 56 bytes have been received.

`HAL_UART_AbortReceive` stops the DMA transfer cleanly so `Read()` can restart it.

`xSemaphoreGiveFromISR` is the ISR-safe version of semaphore give. `portYIELD_FROM_ISR(woken)` causes an immediate context switch if `UartRxTask` has higher priority than the interrupted task.

---

## `OnUsart2Irq()` — USART2 IRQ handler

```cpp
void UartDriver::OnUsart2Irq() noexcept
{
    if (__HAL_UART_GET_FLAG(&g_huart2, UART_FLAG_IDLE) != RESET)
    {
        __HAL_UART_CLEAR_IDLEFLAG(&g_huart2);
        SignalRxData();
    }
    HAL_UART_IRQHandler(&g_huart2);
}
```

The IDLE flag is set when the RX line goes idle (no byte for one frame period). We clear it manually (the HAL doesn't do this automatically), then call `SignalRxData()` to notify the task.

`HAL_UART_IRQHandler` handles other UART interrupts (errors, etc.) and calls the HAL callbacks.

---

## `OnDma1Stream5Irq()` and `OnRxCpltCallback()`

```cpp
void UartDriver::OnDma1Stream5Irq() noexcept
{
    HAL_DMA_IRQHandler(&g_hdma_rx);
}

void UartDriver::OnRxCpltCallback() noexcept
{
    SignalRxData();
}
```

When the DMA transfer completes (all 66 bytes received), the DMA TC interrupt fires. `HAL_DMA_IRQHandler` processes it and calls `HAL_UART_RxCpltCallback`, which calls `OnRxCpltCallback()`, which calls `SignalRxData()`.

This handles the case where a full 66-byte frame arrives without triggering the IDLE interrupt first.
