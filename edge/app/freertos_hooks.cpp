#include <cstring>

#include "FreeRTOS.h"
#include "task.h"

#include "fail_safe_supervisor.hpp"
#include "panic.hpp"

extern "C" void vApplicationGetIdleTaskMemory(StaticTask_t** ppxIdleTaskTCBBuffer,
                                              StackType_t** ppxIdleTaskStackBuffer,
                                              uint32_t* pulIdleTaskStackSize)
{
    static StaticTask_t idle_task_tcb;
    static StackType_t  idle_task_stack[configMINIMAL_STACK_SIZE];

    *ppxIdleTaskTCBBuffer  = &idle_task_tcb;
    *ppxIdleTaskStackBuffer = idle_task_stack;
    *pulIdleTaskStackSize  = configMINIMAL_STACK_SIZE;
}

// SAF-05: stack overflow detection.
extern "C" void vApplicationStackOverflowHook(TaskHandle_t /*xTask*/,
                                              char* pcTaskName)
{
    auto* pb = aegis::edge::GetPanicBlock();
    if (pb != nullptr)
    {
        (void)std::strncpy(pb->task_name, pcTaskName,
                           sizeof(pb->task_name) - 1U);
        pb->task_name[sizeof(pb->task_name) - 1U] = '\0';
    }

    aegis::edge::FailSafeSupervisor::Instance().ReportEvent(
        aegis::edge::FailSafeEvent::StackOverflow);

    taskDISABLE_INTERRUPTS();
    while (true) { }
}

extern "C" void vAssertCalled(const char* /*file*/, int /*line*/)
{
    taskDISABLE_INTERRUPTS();
    while (true) { }
}
