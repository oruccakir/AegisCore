# MPU Config — Memory Protection Unit

**Files:** `edge/app/mpu_config.hpp`, `edge/bsp/mpu_config.cpp`

---

## What this module is

The **MPU (Memory Protection Unit)** is a hardware component in the Cortex-M4 that allows the CPU to enforce access rules on memory regions. If code tries to access memory in a way that violates the rules (e.g., writing to Flash, executing from RAM, reading from address 0), the CPU triggers a MemManage fault instead of silently corrupting memory.

AegisCore configures four MPU regions to:
1. Catch null pointer dereferences (address 0 is illegal)
2. Prevent accidental writes to Flash (firmware integrity)
3. Prevent executing code injected into RAM (security)

---

## Critical prerequisite: VTOR must be set first

```cpp
void MPU_Init() noexcept
{
    SCB->VTOR = FLASH_BASE;  // ← must be first
    HAL_MPU_Disable();
    ...
}
```

**Why this line is here:** The VTOR (Vector Table Offset Register) tells the CPU where to find the interrupt vector table. On the STM32F407, after reset, VTOR defaults to 0x00000000. The vector table is actually in Flash at 0x08000000 — but because VTOR points to 0, the CPU reads from address 0.

After enabling Region 0 (null-guard, no-access), address 0 becomes illegal. When FreeRTOS starts its first task (`prvPortStartFirstTask`), it reads the initial stack pointer from `[VTOR + 0]` — i.e., from address 0. This causes a MemManage fault.

Setting `SCB->VTOR = FLASH_BASE` (0x08000000) before enabling the MPU ensures the CPU reads the vector table from Flash, not from the no-access null region.

This bug was discovered during development when the system repeatedly entered a watchdog-reset loop (green/red LED flash repeating). Reading the panic block confirmed CFSR=0x82 (DACCVIOL + MMARVALID), MMFAR=0x00000000.

---

## Region setup pattern

All regions share these default settings:

```cpp
MPU_Region_InitTypeDef region = {};
region.Enable           = MPU_REGION_ENABLE;
region.SubRegionDisable = 0x00U;        // all 8 sub-regions active
region.TypeExtField     = MPU_TEX_LEVEL0;
region.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
region.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
region.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
```

TEX/S/C/B bits control cache and bus behaviour. For a Cortex-M4 without an external bus or L2 cache (which STM32F407 doesn't have), these are set to defaults.

---

## Region 0: Null-guard

```cpp
region.Number           = MPU_REGION_NUMBER0;
region.BaseAddress      = 0x00000000U;
region.Size             = MPU_REGION_SIZE_256B;
region.AccessPermission = MPU_REGION_NO_ACCESS;
region.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
```

- **Base:** 0x00000000 (address zero)
- **Size:** 256 bytes
- **Access:** NO_ACCESS — any read, write, or execute raises MemManage fault
- **Effect:** Catches null pointer dereferences. `int* p = nullptr; *p = 5;` → MemManage fault → panic block saved → reset

---

## Region 1: Flash

```cpp
region.Number           = MPU_REGION_NUMBER1;
region.BaseAddress      = 0x08000000U;
region.Size             = MPU_REGION_SIZE_1MB;
region.AccessPermission = MPU_REGION_PRIV_RO_URO;
region.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
```

- **Base:** 0x08000000 (start of Flash on STM32F407)
- **Size:** 1 MB (the full Flash of this chip)
- **Access:** PRIV_RO_URO — privileged and unprivileged read-only
- **Execute:** ENABLE — code in Flash can be executed

**Effect:** The firmware cannot write to its own Flash at runtime (any write attempt → fault). This protects the firmware from accidental corruption.

---

## Region 2: SRAM

```cpp
region.Number           = MPU_REGION_NUMBER2;
region.BaseAddress      = 0x20000000U;
region.Size             = MPU_REGION_SIZE_128KB;
region.AccessPermission = MPU_REGION_FULL_ACCESS;
region.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
```

- **Base:** 0x20000000 (SRAM1 start)
- **Size:** 128 KB (SRAM1 = 112 KB + SRAM2 = 16 KB, together = 128 KB)
- **Access:** FULL_ACCESS — read and write allowed
- **Execute:** DISABLE — executing code from RAM is not allowed

**Effect:** Prevents code injection attacks where an attacker writes shellcode into RAM and jumps to it. Any attempt to execute from RAM → fault.

---

## Region 3: CCMRAM

```cpp
region.Number           = MPU_REGION_NUMBER3;
region.BaseAddress      = 0x10000000U;
region.Size             = MPU_REGION_SIZE_64KB;
region.AccessPermission = MPU_REGION_FULL_ACCESS;
region.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
```

- **Base:** 0x10000000 (CCM = Core-Coupled Memory, 64 KB)
- **Access:** FULL_ACCESS
- **Execute:** DISABLE

CCM is fast zero-wait-state RAM directly connected to the CPU (no bus arbitration). It is used for stack and time-critical data. Same protection as SRAM — no code execution from here.

---

## Enabling the MPU

```cpp
HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
```

`MPU_PRIVILEGED_DEFAULT` means: for any access that is not covered by a configured region, use the default memory map — privileged code can still access everything. This prevents the MPU from blocking accesses to hardware peripherals (which are at addresses like 0x40000000) that we haven't explicitly configured.
