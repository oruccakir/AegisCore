# Panic — Hard Fault Post-Mortem and Crash Recording

**Files:** `edge/app/panic.hpp`, `edge/bsp/panic.cpp`, `edge/bsp/fault_stubs.c`

---

## What this module is

When the CPU encounters a catastrophic error (null pointer dereference, illegal instruction, stack corruption, etc.), it triggers a **HardFault** exception. Normally this would be an infinite loop with no information. This module instead:

1. Captures the CPU register state at the moment of the fault
2. Saves it to a special memory location that survives a watchdog reset
3. Resets the MCU
4. On the next boot, the saved data can be read and sent to the gateway as a fault report

This is like a "black box" recorder for the firmware.

---

## `PanicBlock` — the crash record

```cpp
struct PanicBlock
{
    std::uint32_t magic;          // 0xDEADBEEF when populated
    std::uint32_t r0;             // CPU register R0 at fault
    std::uint32_t r1;             // CPU register R1 at fault
    std::uint32_t r2;             // CPU register R2 at fault
    std::uint32_t r3;             // CPU register R3 at fault
    std::uint32_t r12;            // CPU register R12 at fault
    std::uint32_t lr;             // Link Register (return address)
    std::uint32_t pc;             // Program Counter (instruction that faulted)
    std::uint32_t xpsr;           // CPU status register
    std::uint32_t cfsr;           // Configurable Fault Status Register
    std::uint32_t hfsr;           // HardFault Status Register
    std::uint32_t mmfar;          // MemManage Fault Address Register
    std::uint32_t bfar;           // BusFault Address Register
    std::uint32_t reset_reason;   // RCC_CSR bits (why did we reset?)
    char task_name[16];           // task name if stack overflow caused this
};
```

### Key fields explained

- **`magic`** — set to `0xDEADBEEF` when the block is populated. On boot, if this is `0xDEADBEEF`, we know the previous run crashed. If it's anything else (e.g., `0x00000000`), this is a normal boot.

- **`pc`** (Program Counter) — the address of the instruction that caused the fault. Using `arm-none-eabi-addr2line` you can map this to an exact source file and line number.

- **`cfsr`** (Configurable Fault Status Register) — a bitmask that describes *what kind* of fault occurred:
  - Bit 0 (IACCVIOL): Instruction access violation
  - Bit 1 (DACCVIOL): Data access violation (e.g., null pointer dereference)
  - Bit 7 (MMARVALID): `mmfar` is valid (contains the faulting address)
  - Many more bits for BusFault and UsageFault

- **`mmfar`** (MemManage Fault Address) — if `cfsr.MMARVALID` is set, this register holds the exact address that was illegally accessed (e.g., `0x00000000` for a null pointer).

### `.noinit` section

```cpp
__attribute__((section(".noinit")))
static aegis::edge::PanicBlock g_panic_block;
```

The `.noinit` section is NOT zeroed by the startup code (`startup_stm32f407xx.s`). All other RAM is zeroed at reset. By placing `g_panic_block` in `.noinit`, it retains its value across a watchdog-triggered reset. This is how the crash data survives the reboot.

The linker script (`linker/STM32F407VGTx_FLASH.ld`) must define a `.noinit` section for this to work.

---

## `GetPanicBlock()` → `PanicBlock*`

```cpp
PanicBlock* GetPanicBlock() noexcept
{
    return &g_panic_block;
}
```

Returns a pointer to the panic block. Used by the stack overflow hook (`freertos_hooks.cpp`) to write the task name before triggering the fail-safe.

---

## `Panic_HardFaultImpl(std::uint32_t* fault_frame)`

```cpp
extern "C" void Panic_HardFaultImpl(std::uint32_t* fault_frame) noexcept
```

Called from the naked assembly stubs in `fault_stubs.c`. The `fault_frame` pointer points to the exception stack frame pushed by the CPU.

**What the CPU pushes automatically during a fault:**

When a fault occurs, the Cortex-M4 automatically pushes 8 registers onto the active stack (MSP or PSP depending on context):

```
[sp+0]  = R0
[sp+4]  = R1
[sp+8]  = R2
[sp+12] = R3
[sp+16] = R12
[sp+20] = LR (link register)
[sp+24] = PC (program counter of the faulting instruction)
[sp+28] = xPSR (processor status)
```

**Line by line:**

```cpp
g_panic_block.magic = aegis::edge::kPanicMagic;  // mark block as valid
g_panic_block.r0    = fault_frame[0U];             // R0
g_panic_block.r1    = fault_frame[1U];             // R1
...
g_panic_block.pc    = fault_frame[6U];             // PC — the faulting instruction
g_panic_block.xpsr  = fault_frame[7U];             // CPU status flags
```

Then the fault status registers are read directly:

```cpp
g_panic_block.cfsr  = SCB->CFSR;   // what type of fault
g_panic_block.hfsr  = SCB->HFSR;   // hard fault cause (escalation from other fault?)
g_panic_block.mmfar = SCB->MMFAR;  // faulting memory address (if MMARVALID)
g_panic_block.bfar  = SCB->BFAR;   // bus fault address (if BFARVALID)
g_panic_block.reset_reason = RCC->CSR;  // why did we reset before this run?
```

Finally:
```cpp
NVIC_SystemReset();
```
Trigger a software reset. The watchdog is also running so it would reset eventually anyway, but `NVIC_SystemReset()` resets immediately.

---

## `fault_stubs.c` — the naked assembly trampolines

```c
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4         \n\t"  // test bit 2 of LR (EXC_RETURN)
        "ite eq             \n\t"  // if-then-else
        "mrseq r0, msp      \n\t"  // if equal: r0 = Main Stack Pointer
        "mrsne r0, psp      \n\t"  // if not equal: r0 = Process Stack Pointer
        "b Panic_HardFaultImpl \n\t"  // branch to the C handler with r0 as arg
    );
}
```

**Why `naked`?** A normal C function pushes a stack frame (saves registers, sets up locals) before executing. A `naked` function has no prologue or epilogue — the assembly runs exactly as written. This is essential here because the fault frame on the stack would be corrupted by a normal function prologue.

**The LR trick explained:**

When a fault occurs, the CPU automatically loads LR with a special value called `EXC_RETURN`. Bit 2 of `EXC_RETURN` indicates which stack was active:
- Bit 2 = 0 → the fault interrupted code using the MSP (Main Stack Pointer) — typical for ISR context
- Bit 2 = 1 → the fault interrupted code using the PSP (Process Stack Pointer) — typical for FreeRTOS tasks

`tst lr, #4` — test bit 2 (value 4) of LR, sets flags  
`ite eq` — if the test was equal (bit was 0): use MSP; else (bit was 1): use PSP  
`mrseq r0, msp` — if LR bit 2 = 0: load MSP into R0  
`mrsne r0, psp` — if LR bit 2 = 1: load PSP into R0  

After this, R0 contains the address of the CPU's exception stack frame — exactly what `Panic_HardFaultImpl` expects as its argument.

**The same assembly is replicated for `MemManage_Handler`, `BusFault_Handler`, and `UsageFault_Handler`** — all catastrophic CPU exceptions use the same recovery path.

---

## How to use the panic block after a crash

1. Boot the system
2. In `main.cpp`, check `GetPanicBlock()->magic == kPanicMagic`
3. If true, queue a fault report frame to the gateway
4. Clear the magic number after reporting

This infrastructure is set up but the `main.cpp` check step is on the roadmap (Phase E).
