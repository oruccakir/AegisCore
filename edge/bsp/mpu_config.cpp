#include "mpu_config.hpp"

#include "stm32f4xx_hal.h"

namespace aegis::edge {

void MPU_Init() noexcept
{
    // VTOR must point to Flash before the null-guard region is active,
    // otherwise prvPortStartFirstTask faults reading initial MSP from 0x00000000.
    SCB->VTOR = FLASH_BASE;

    HAL_MPU_Disable();

    MPU_Region_InitTypeDef region = {};
    region.Enable           = MPU_REGION_ENABLE;
    region.SubRegionDisable = 0x00U;
    region.TypeExtField     = MPU_TEX_LEVEL0;
    region.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    region.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    region.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;

    // Region 0: null-guard — catch NULL dereferences.
    region.Number           = MPU_REGION_NUMBER0;
    region.BaseAddress      = 0x00000000U;
    region.Size             = MPU_REGION_SIZE_256B;
    region.AccessPermission = MPU_REGION_NO_ACCESS;
    region.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&region);

    // Region 1: Flash — read-only + execute.
    region.Number           = MPU_REGION_NUMBER1;
    region.BaseAddress      = 0x08000000U;
    region.Size             = MPU_REGION_SIZE_1MB;
    region.AccessPermission = MPU_REGION_PRIV_RO_URO;
    region.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
    HAL_MPU_ConfigRegion(&region);

    // Region 2: SRAM1+SRAM2 — read/write, no execute.
    region.Number           = MPU_REGION_NUMBER2;
    region.BaseAddress      = 0x20000000U;
    region.Size             = MPU_REGION_SIZE_128KB;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&region);

    // Region 3: CCMRAM — read/write, no execute.
    region.Number           = MPU_REGION_NUMBER3;
    region.BaseAddress      = 0x10000000U;
    region.Size             = MPU_REGION_SIZE_64KB;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&region);

    // Enable MPU; use default map for privileged accesses not covered above.
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

} // namespace aegis::edge
