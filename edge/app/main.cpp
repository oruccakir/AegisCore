#include <array>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "button_classifier.hpp"
#include "platform_io.hpp"
#include "simulation_engine.hpp"
#include "state_machine.hpp"
#include "stm32f4xx_hal.h"

namespace {

using aegis::edge::ButtonClassifier;
using aegis::edge::RawButtonEdge;
using aegis::edge::SimulationEngine;
using aegis::edge::StateMachine;

constexpr std::uint32_t kAppTaskPriority = 4U;
constexpr std::uint32_t kAppTaskStackWords = 512U;
constexpr std::uint32_t kButtonQueueLength = 8U;
constexpr std::uint32_t kSimulationSeed = 0x12345678U;
constexpr TickType_t kSimulationPeriodTicks = pdMS_TO_TICKS(100U);

StaticTask_t gStateMachineTaskControlBlock;
StackType_t gStateMachineTaskStack[kAppTaskStackWords];
StaticQueue_t gButtonQueueControlBlock;
std::array<RawButtonEdge, kButtonQueueLength> gButtonQueueStorage = {};

QueueHandle_t gButtonQueue = nullptr;

void ApplyCurrentOutputs(const StateMachine& state_machine)
{
    aegis::edge::ApplyLedOutputs(
        state_machine.GetLedOutputs(aegis::edge::MillisecondsSinceBoot()));
}

void DispatchButtonEvents(StateMachine& state_machine,
                          ButtonClassifier& classifier)
{
    RawButtonEdge edge = {};

    while (xQueueReceive(gButtonQueue, &edge, 0U) == pdPASS)
    {
        const auto event = classifier.OnEdge(edge);
        if (event.has_value())
        {
            (void)state_machine.Dispatch(*event);
        }
    }
}

void RunSimulationStep(StateMachine& state_machine,
                       SimulationEngine& simulation_engine,
                       const std::uint32_t timestamp_ms)
{
    const auto event = simulation_engine.Tick100ms(state_machine.state(), timestamp_ms);
    if (event.has_value())
    {
        (void)state_machine.Dispatch(*event);
    }
}

void StateMachineTask(void* /*context*/)
{
    ButtonClassifier classifier;
    StateMachine state_machine{aegis::edge::MillisecondsSinceBoot()};
    SimulationEngine simulation_engine{kSimulationSeed};

    TickType_t next_simulation_tick = xTaskGetTickCount() + kSimulationPeriodTicks;

    ApplyCurrentOutputs(state_machine);

    for (;;)
    {
        const TickType_t now_ticks = xTaskGetTickCount();
        const TickType_t wait_ticks =
            (now_ticks < next_simulation_tick) ? (next_simulation_tick - now_ticks) : 0U;

        RawButtonEdge edge = {};
        if (xQueueReceive(gButtonQueue, &edge, wait_ticks) == pdPASS)
        {
            const auto event = classifier.OnEdge(edge);
            if (event.has_value())
            {
                (void)state_machine.Dispatch(*event);
            }

            DispatchButtonEvents(state_machine, classifier);
        }

        TickType_t current_ticks = xTaskGetTickCount();
        while (current_ticks >= next_simulation_tick)
        {
            const auto timestamp_ms =
                static_cast<std::uint32_t>(next_simulation_tick * portTICK_PERIOD_MS);
            RunSimulationStep(state_machine, simulation_engine, timestamp_ms);
            next_simulation_tick += kSimulationPeriodTicks;
            current_ticks = xTaskGetTickCount();
        }

        ApplyCurrentOutputs(state_machine);
    }
}

} // namespace

extern "C" void HAL_GPIO_EXTI_Callback(const uint16_t gpio_pin)
{
    if ((gpio_pin != GPIO_PIN_0) || (gButtonQueue == nullptr))
    {
        return;
    }

    const RawButtonEdge edge = {
        aegis::edge::ReadButtonPressed() ? aegis::edge::RawButtonEdgeType::Pressed
                                         : aegis::edge::RawButtonEdgeType::Released,
        aegis::edge::MillisecondsSinceBoot()};

    BaseType_t higher_priority_task_woken = pdFALSE;
    (void)xQueueSendFromISR(gButtonQueue, &edge, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

extern "C" int main()
{
    aegis::edge::InitializePlatform();

    gButtonQueue = xQueueCreateStatic(
        kButtonQueueLength,
        sizeof(RawButtonEdge),
        reinterpret_cast<std::uint8_t*>(gButtonQueueStorage.data()),
        &gButtonQueueControlBlock);
    configASSERT(gButtonQueue != nullptr);

    TaskHandle_t const task_handle = xTaskCreateStatic(
        StateMachineTask,
        "StateCore",
        kAppTaskStackWords,
        nullptr,
        kAppTaskPriority,
        gStateMachineTaskStack,
        &gStateMachineTaskControlBlock);
    configASSERT(task_handle != nullptr);

    vTaskStartScheduler();

    __disable_irq();
    while (true)
    {
    }
}
