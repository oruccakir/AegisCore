#ifndef AEGIS_EDGE_UART_DRIVER_HPP
#define AEGIS_EDGE_UART_DRIVER_HPP

#include <cstdint>

#include "i_serial.hpp"

namespace aegis::edge {

// USART2 driver (PA2 TX / PA3 RX, 115200 8N1, DMA RX + IDLE detection).
// All HAL state lives in uart_driver.cpp — this header stays HAL-free.
// Implements ISerial for use by TelemetryTxTask and UartRxTask.
class UartDriver : public ISerial
{
public:
    // Configure USART2, GPIO, DMA, binary semaphore.
    // Must be called once before tasks start.
    bool Init() noexcept;

    // Blocking transmit (polling mode). Returns true on success.
    bool Write(const std::uint8_t* data, std::uint8_t len) noexcept override;

    // Copy at most max_len bytes from the DMA receive buffer.
    // Returns byte count copied. Restarts DMA after copy.
    std::uint8_t Read(std::uint8_t* dst, std::uint8_t max_len) noexcept override;

    // Block the calling task until UART data is available (IDLE line or DMA TC).
    void WaitForData() noexcept;

    // Called from ISR — not for application code.
    void OnUsart2Irq() noexcept;
    void OnDma1Stream5Irq() noexcept;
    void OnRxCpltCallback() noexcept;

private:
    void SignalRxData() noexcept;

    // SYNC(1)+VER(1)+SEQ(4)+LEN(1)+CMD(1)+PAYLOAD(48)+HMAC(8)+CRC(2) = 66
    static constexpr std::uint8_t kRxBufSize = 66U;

    std::uint8_t          rx_dma_buf_[kRxBufSize] = {};
    volatile std::uint8_t rx_ready_len_            = 0U;
};

} // namespace aegis::edge

#endif
