#include "uart_driver.hpp"

#include "FreeRTOS.h"
#include "semphr.h"
#include "stm32f4xx_hal.h"

// File-scope HAL handles and semaphore — not exposed in the header (ARCH-05).
static UART_HandleTypeDef  g_huart2   = {};
static DMA_HandleTypeDef   g_hdma_rx  = {};
static StaticSemaphore_t   g_rx_sem_buf = {};
static SemaphoreHandle_t   g_rx_sem   = nullptr;

// Back-pointer for ISR dispatch (set in Init, never null after init).
static aegis::edge::UartDriver* g_driver = nullptr;

// HAL DMA RX complete callback — called from DMA IRQ context.
extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart)
{
    if (g_driver != nullptr && huart->Instance == USART2)
    {
        g_driver->OnRxCpltCallback();
    }
}

// C wrappers called from stm32f4xx_it.c.
extern "C" void UartDriver_OnUsart2Irq(void)
{
    if (g_driver != nullptr) { g_driver->OnUsart2Irq(); }
}

extern "C" void UartDriver_OnDma1Stream5Irq(void)
{
    if (g_driver != nullptr) { g_driver->OnDma1Stream5Irq(); }
}

namespace aegis::edge {

bool UartDriver::Init() noexcept
{
    g_driver = this;

    g_rx_sem = xSemaphoreCreateBinaryStatic(&g_rx_sem_buf);
    if (g_rx_sem == nullptr) { return false; }

    // Enable clocks.
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    // PA2 = TX, PA3 = RX, AF7.
    GPIO_InitTypeDef gpio = {};
    gpio.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);

    // USART2 @ 115200 8N1.
    g_huart2.Instance          = USART2;
    g_huart2.Init.BaudRate     = 115200U;
    g_huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    g_huart2.Init.StopBits     = UART_STOPBITS_1;
    g_huart2.Init.Parity       = UART_PARITY_NONE;
    g_huart2.Init.Mode         = UART_MODE_TX_RX;
    g_huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    g_huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&g_huart2) != HAL_OK) { return false; }

    // DMA1 Stream5 Ch4 → USART2_RX.
    g_hdma_rx.Instance                 = DMA1_Stream5;
    g_hdma_rx.Init.Channel             = DMA_CHANNEL_4;
    g_hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    g_hdma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    g_hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;
    g_hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    g_hdma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    g_hdma_rx.Init.Mode                = DMA_NORMAL;
    g_hdma_rx.Init.Priority            = DMA_PRIORITY_LOW;
    g_hdma_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&g_hdma_rx) != HAL_OK) { return false; }
    __HAL_LINKDMA(&g_huart2, hdmarx, g_hdma_rx);

    // NVIC for DMA and UART (priority 6, below FreeRTOS MAX_SYSCALL = 5<<4 = 80).
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);

    HAL_NVIC_SetPriority(USART2_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    // Enable IDLE line interrupt.
    __HAL_UART_ENABLE_IT(&g_huart2, UART_IT_IDLE);

    // Start DMA receive into circular buffer.
    (void)HAL_UART_Receive_DMA(&g_huart2, rx_dma_buf_, kRxBufSize);

    return true;
}

bool UartDriver::Write(const std::uint8_t* data, std::uint8_t len) noexcept
{
    // Polling TX — blocks for ~(len * 87µs) at 115200. Acceptable for task context.
    return HAL_UART_Transmit(&g_huart2, const_cast<std::uint8_t*>(data),
                             len, 10U) == HAL_OK;
}

std::uint8_t UartDriver::Read(std::uint8_t* dst, std::uint8_t max_len) noexcept
{
    const std::uint8_t n = (rx_ready_len_ < max_len) ? rx_ready_len_ : max_len;
    for (std::uint8_t i = 0U; i < n; ++i) { dst[i] = rx_dma_buf_[i]; }
    rx_ready_len_ = 0U;
    (void)HAL_UART_Receive_DMA(&g_huart2, rx_dma_buf_, kRxBufSize);
    return n;
}

void UartDriver::WaitForData() noexcept
{
    (void)xSemaphoreTake(g_rx_sem, portMAX_DELAY);
}

void UartDriver::SignalRxData() noexcept
{
    const auto ndtr = static_cast<std::uint16_t>(g_hdma_rx.Instance->NDTR);
    const auto received = static_cast<std::uint8_t>(kRxBufSize - ndtr);
    if (received == 0U) { return; }

    rx_ready_len_ = received;
    (void)HAL_UART_AbortReceive(&g_huart2);

    BaseType_t woken = pdFALSE;
    (void)xSemaphoreGiveFromISR(g_rx_sem, &woken);
    portYIELD_FROM_ISR(woken);
}

void UartDriver::OnUsart2Irq() noexcept
{
    if (__HAL_UART_GET_FLAG(&g_huart2, UART_FLAG_IDLE) != RESET)
    {
        __HAL_UART_CLEAR_IDLEFLAG(&g_huart2);
        SignalRxData();
    }
    HAL_UART_IRQHandler(&g_huart2);
}

void UartDriver::OnDma1Stream5Irq() noexcept
{
    HAL_DMA_IRQHandler(&g_hdma_rx);
}

void UartDriver::OnRxCpltCallback() noexcept
{
    SignalRxData();
}

} // namespace aegis::edge
