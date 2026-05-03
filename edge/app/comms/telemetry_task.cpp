#include "runtime/edge_runtime.hpp"

#include "fail_safe_supervisor.hpp"
#include "platform_io.hpp"
#include "state_machine.hpp"

namespace aegis::edge {
namespace {

std::uint32_t gPrevTotalRt = 0U;
std::uint32_t gPrevIdleRt = 0U;

} // namespace

void QueueTaskList() noexcept
{
    std::uint32_t total_rt = 0U;
    const UBaseType_t n = uxTaskGetSystemState(gTaskStatusBuf, kMaxTaskCount, &total_rt);
    const std::uint8_t count =
        static_cast<std::uint8_t>(n > kMaxTaskCount ? kMaxTaskCount : n);
    gLcdTaskCount = count;
    gLcdTaskListMs = MillisecondsSinceBoot();

    constexpr std::uint8_t kEntry =
        static_cast<std::uint8_t>(sizeof(PackedTaskEntry));
    std::uint8_t payload[1U + kMaxTaskCount * sizeof(PackedTaskEntry)] = {};
    payload[0] = count;

    for (std::uint8_t i = 0U; i < count; ++i) {
        const TaskStatus_t& src = gTaskStatusBuf[i];
        PackedTaskEntry entry = {};

        for (std::uint8_t c = 0U; c < 7U && src.pcTaskName[c] != '\0'; ++c) {
            entry.name[c] = src.pcTaskName[c];
        }
        entry.state = static_cast<std::uint8_t>(src.eCurrentState);
        entry.priority = static_cast<std::uint8_t>(src.uxCurrentPriority);
        entry.stack_watermark = static_cast<std::uint16_t>(src.usStackHighWaterMark);
        if (total_rt > 100U) {
            const std::uint32_t pct = src.ulRunTimeCounter / (total_rt / 100U);
            entry.cpu_load = static_cast<std::uint8_t>(pct > 100U ? 100U : pct);
        }
        for (std::uint8_t s = 0U; s < kUserTaskSlots; ++s) {
            if (gUserTasks[s].in_use && gUserTasks[s].handle == src.xHandle) {
                entry.task_id = static_cast<std::uint8_t>(0x80U | s);
                break;
            }
        }

        const auto* eb = reinterpret_cast<const std::uint8_t*>(&entry);
        const std::uint8_t off = static_cast<std::uint8_t>(1U + i * kEntry);
        for (std::uint8_t b = 0U; b < kEntry; ++b) {
            payload[off + b] = eb[b];
        }
    }

    const std::uint8_t plen = static_cast<std::uint8_t>(1U + count * kEntry);
    QueueTx(CmdId::kTaskList, payload, plen);
}

void QueueTelemetryTick(const StateMachine& sm) noexcept
{
    std::uint32_t total_rt = 0U;
    const UBaseType_t n = uxTaskGetSystemState(gTaskStatusBuf, kMaxTaskCount, &total_rt);

    std::uint32_t idle_rt = 0U;
    for (UBaseType_t i = 0U; i < n; ++i) {
        const char* nm = gTaskStatusBuf[i].pcTaskName;
        if (nm[0] == 'I' && nm[1] == 'D' && nm[2] == 'L' && nm[3] == 'E' && nm[4] == '\0') {
            idle_rt = gTaskStatusBuf[i].ulRunTimeCounter;
            break;
        }
    }

    const std::uint32_t delta_total = total_rt - gPrevTotalRt;
    const std::uint32_t delta_idle = idle_rt - gPrevIdleRt;
    gPrevTotalRt = total_rt;
    gPrevIdleRt = idle_rt;

    std::uint16_t cpu_load_x10 = 0U;
    if (delta_total > 100U) {
        const std::uint32_t idle_pct =
            (delta_idle >= delta_total) ? 100U : (delta_idle / (delta_total / 100U));
        const std::uint32_t used_pct = (idle_pct >= 100U) ? 0U : (100U - idle_pct);
        cpu_load_x10 = static_cast<std::uint16_t>(used_pct * 10U);
    }

    PayloadTelemetryTick p = {};
    p.state = static_cast<std::uint8_t>(sm.state());
    p.cpu_load_x10 = cpu_load_x10;
    p.stack_uart_rx = static_cast<std::uint16_t>(uxTaskGetStackHighWaterMark(gUartRxHandle));
    p.stack_state_core = static_cast<std::uint16_t>(uxTaskGetStackHighWaterMark(gSMHandle));
    p.stack_tel_tx = static_cast<std::uint16_t>(uxTaskGetStackHighWaterMark(gTxHandle));
    p.stack_heartbeat = static_cast<std::uint16_t>(uxTaskGetStackHighWaterMark(gHbHandle));
    p.hb_miss_count = FailSafeSupervisor::Instance().HeartbeatMissCount();

    gLcdState = p.state;
    gLcdCpuLoadX10 = p.cpu_load_x10;
    gLcdHbMissCount = p.hb_miss_count;
    gLcdUptimeMs = MillisecondsSinceBoot();
    gLcdStackMin = p.stack_uart_rx;
    if (p.stack_state_core < gLcdStackMin) {
        gLcdStackMin = p.stack_state_core;
    }
    if (p.stack_tel_tx < gLcdStackMin) {
        gLcdStackMin = p.stack_tel_tx;
    }
    if (p.stack_heartbeat < gLcdStackMin) {
        gLcdStackMin = p.stack_heartbeat;
    }

    QueueTx(CmdId::kTelemetryTick,
            reinterpret_cast<const std::uint8_t*>(&p),
            static_cast<std::uint8_t>(sizeof(p)));
}

void TelemetryTxTask(void* /*ctx*/)
{
    TxItem item = {};
    std::uint8_t encoded[kAC2MaxFrame] = {};

    for (;;) {
        if (xQueueReceive(gTxQueue, &item, portMAX_DELAY) == pdPASS) {
            const std::uint8_t flen =
                AC2Framer::EncodeTelemetry(item.cmd, item.payload, item.len, gTxSeq, encoded);
            gTxSeq = gTxSeq + 1U;
            (void)gUart.Write(encoded, flen);
        }
    }
}

} // namespace aegis::edge
