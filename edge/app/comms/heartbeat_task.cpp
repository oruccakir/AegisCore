#include "runtime/edge_runtime.hpp"

#include "fail_safe_supervisor.hpp"
#include "platform_io.hpp"

namespace aegis::edge {

void HeartbeatTask(void* /*ctx*/)
{
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last_wake, kHeartbeatPeriodTicks);

        const auto now_ms = MillisecondsSinceBoot();
        FailSafeSupervisor::Instance().CheckHeartbeatTimeout(now_ms);

        PayloadHeartbeatTx hb = {};
        hb.uptime_ms = now_ms;
        QueueTx(CmdId::kHeartbeat,
                reinterpret_cast<const std::uint8_t*>(&hb),
                static_cast<std::uint8_t>(sizeof(hb)));
    }
}

} // namespace aegis::edge
