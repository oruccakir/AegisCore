#include "runtime/edge_runtime.hpp"

#include "fail_safe_supervisor.hpp"

namespace aegis::edge {

void UartRxTask(void* /*ctx*/)
{
    std::uint32_t last_crc_errors = 0U;

    for (;;) {
        gUart.WaitForData();
        const std::uint8_t n = gUart.Read(
            gUartRxBuf, static_cast<std::uint8_t>(sizeof(gUartRxBuf)));
        for (std::uint8_t i = 0U; i < n; ++i) {
            gParser.Feed(gUartRxBuf[i]);
        }

        const std::uint32_t crc_now = gParser.CrcErrorCount();
        while (last_crc_errors < crc_now) {
            FailSafeSupervisor::Instance().OnCrcError();
            last_crc_errors = last_crc_errors + 1U;
        }
    }
}

} // namespace aegis::edge
