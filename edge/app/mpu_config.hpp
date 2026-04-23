#ifndef AEGIS_EDGE_MPU_CONFIG_HPP
#define AEGIS_EDGE_MPU_CONFIG_HPP

namespace aegis::edge {

// Configures Cortex-M4 MPU with four regions:
//   0 — null-guard (0x00000000, 256 B, no access)
//   1 — Flash       (0x08000000, 1 MB,  RO + exec)
//   2 — SRAM        (0x20000000, 128 KB, RW + no-exec)
//   3 — CCMRAM      (0x10000000,  64 KB, RW + no-exec)
// Called once during InitializePlatform(), before tasks start.
void MPU_Init() noexcept;

} // namespace aegis::edge

#endif
