#include "runtime/edge_runtime.hpp"

#include "platform_io.hpp"

namespace aegis::edge {
namespace {

void OnButtonEdge(void* ctx) noexcept
{
    const RawButtonEdge edge = {
        ReadButtonPressed() ? RawButtonEdgeType::Pressed : RawButtonEdgeType::Released,
        MillisecondsSinceBoot()
    };
    BaseType_t woken = pdFALSE;
    (void)xQueueSendFromISR(static_cast<QueueHandle_t>(ctx), &edge, &woken);
    portYIELD_FROM_ISR(woken);
}

void OnJoystickPress(void* ctx) noexcept
{
    const std::uint32_t timestamp_ms = MillisecondsSinceBoot();
    BaseType_t woken = pdFALSE;
    (void)xQueueSendFromISR(static_cast<QueueHandle_t>(ctx), &timestamp_ms, &woken);
    portYIELD_FROM_ISR(woken);
}

} // namespace

void RegisterInputCallbacks() noexcept
{
    SetButtonEdgeCallback(OnButtonEdge, gButtonQueue);
    SetJoystickPressCallback(OnJoystickPress, gJoystickQueue);
}

void DrainJoystickEvents(std::uint32_t& last_press_ms,
                         std::uint16_t& press_count) noexcept
{
    constexpr std::uint32_t kDebounceMs = 150U;

    std::uint32_t press_ms = 0U;
    while (xQueueReceive(gJoystickQueue, &press_ms, 0U) == pdPASS) {
        if (last_press_ms != 0U && (press_ms - last_press_ms) < kDebounceMs) {
            continue;
        }

        last_press_ms = press_ms;
        press_count = static_cast<std::uint16_t>(press_count + 1U);
        SetLcdAlert(AuditCode::kJoystickPress, press_count);
    }
}

} // namespace aegis::edge
