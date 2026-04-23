#include "watchdog.hpp"

#include "stm32f4xx_hal.h"

// LSI ~32 kHz, prescaler /32, reload 1023 → timeout ≈ 32*(1023+1)/32000 = 1.024 s.
static IWDG_HandleTypeDef g_hiwdg;

namespace aegis::edge {

void Watchdog::Init() noexcept
{
    g_hiwdg.Instance       = IWDG;
    g_hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    g_hiwdg.Init.Reload    = 1023U;
    (void)HAL_IWDG_Init(&g_hiwdg);
}

void Watchdog::Feed() noexcept
{
    (void)HAL_IWDG_Refresh(&g_hiwdg);
}

} // namespace aegis::edge
