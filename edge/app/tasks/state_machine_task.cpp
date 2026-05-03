#include "runtime/edge_runtime.hpp"

#include "fail_safe_supervisor.hpp"
#include "platform_io.hpp"
#include "simulation_engine.hpp"
#include "state_machine.hpp"
#include "watchdog.hpp"

#include "stm32f4xx_hal.h"

namespace aegis::edge {

void StateMachineTask(void* /*ctx*/)
{
    ButtonClassifier classifier;
    StateMachine sm{MillisecondsSinceBoot()};
    SimulationEngine sim{kSimulationSeed};

    TickType_t next_sim_tick = xTaskGetTickCount() + kSimulationPeriodTicks;
    TickType_t next_telemetry_tick = xTaskGetTickCount() + kTelemetryPeriodTicks;
    TickType_t next_tasklist_tick = xTaskGetTickCount() + kTaskListPeriodTicks;
    bool fail_safe_announced = false;
    std::uint32_t last_joystick_press_ms = 0U;
    std::uint16_t joystick_press_count = 0U;

    ApplyLedOutputs(sm.GetLedOutputs(MillisecondsSinceBoot()));
    QueueBootReport();
    SetLcdAlert(AuditCode::kBoot, 0U);

    for (;;) {
        Watchdog::Feed();

        if (FailSafeSupervisor::Instance().IsTriggered()) {
            sm.ForceFailSafe(MillisecondsSinceBoot());
            if (!fail_safe_announced) {
                SetLcdAlert(AuditCode::kFailSafe, 0U);
                fail_safe_announced = true;
            }
        } else {
            fail_safe_announced = false;
        }

        RemoteCmd rcmd = {};
        while (xQueueReceive(gRemoteCmdQueue, &rcmd, 0U) == pdPASS) {
            DispatchRemoteCmd(sm, rcmd);
        }

        DrainJoystickEvents(last_joystick_press_ms, joystick_press_count);

        const TickType_t now_ticks = xTaskGetTickCount();
        const TickType_t wait =
            (now_ticks < next_sim_tick) ? (next_sim_tick - now_ticks) : 0U;

        RawButtonEdge edge = {};
        if (xQueueReceive(gButtonQueue, &edge, wait) == pdPASS) {
            const auto ev = classifier.OnEdge(edge);
            if (ev.has_value()) {
                (void)sm.Dispatch(*ev);
            }

            while (xQueueReceive(gButtonQueue, &edge, 0U) == pdPASS) {
                const auto ev2 = classifier.OnEdge(edge);
                if (ev2.has_value()) {
                    (void)sm.Dispatch(*ev2);
                }
            }
        }

        TickType_t curr = xTaskGetTickCount();
        while (curr >= next_sim_tick) {
            const auto ts_ms = static_cast<std::uint32_t>(next_sim_tick * portTICK_PERIOD_MS);
            const auto ev = sim.Tick100ms(sm.state(), ts_ms);
            if (ev.has_value()) {
                (void)sm.Dispatch(*ev);
            }
            next_sim_tick = next_sim_tick + kSimulationPeriodTicks;
            curr = xTaskGetTickCount();
        }

        if (xTaskGetTickCount() >= next_telemetry_tick) {
            next_telemetry_tick = next_telemetry_tick + kTelemetryPeriodTicks;
            QueueTelemetryTick(sm);
        }

        if (xTaskGetTickCount() >= next_tasklist_tick) {
            next_tasklist_tick = next_tasklist_tick + kTaskListPeriodTicks;
            QueueTaskList();
        }

        const std::uint32_t now_led = MillisecondsSinceBoot();
        LedOutputs leds = sm.GetLedOutputs(now_led);
        leds.green_on = leds.green_on ||
                        (gGreenPulseOffMs != 0U && now_led < gGreenPulseOffMs);
        leds.blue_on = (gVersionLedOffMs != 0U && now_led < gVersionLedOffMs) ||
                       (gDetectionLedOffMs != 0U && now_led < gDetectionLedOffMs);
        leds.red_on = leds.red_on ||
                      (gRedPulseOffMs != 0U && now_led < gRedPulseOffMs);
        leds.yellow_on = (gManualLockLedOffMs != 0U && now_led < gManualLockLedOffMs) ||
                         (gYellowPulseOffMs != 0U && now_led < gYellowPulseOffMs) ||
                         static_cast<bool>(gUserBlinkState);
        ApplyLedOutputs(leds);

        if (gSystemResetPending && xTaskGetTickCount() >= gSystemResetDueTick) {
            NVIC_SystemReset();
        }
    }
}

} // namespace aegis::edge
