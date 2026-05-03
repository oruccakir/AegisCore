#ifndef AEGIS_EDGE_PLATFORM_IO_HPP
#define AEGIS_EDGE_PLATFORM_IO_HPP

#include <cstdint>

#include "domain.hpp"

namespace aegis::edge {

using ButtonEdgeCallback = void (*)(void* ctx);
using JoystickPressCallback = void (*)(void* ctx);

void ApplyLedOutputs(const LedOutputs& outputs);
void DelayMs(std::uint32_t ms);
void InitializePlatform();
[[nodiscard]] bool InitializeRangeSensor() noexcept;
[[nodiscard]] bool InitializeServoPwm() noexcept;
[[nodiscard]] bool IsHseClockReady() noexcept;
[[nodiscard]] bool MeasureRangeCm(std::uint16_t& distance_cm) noexcept;
[[nodiscard]] std::uint32_t MillisecondsSinceBoot();
[[nodiscard]] bool ReadButtonPressed();
[[nodiscard]] bool ReadJoystickPressed();
void SetServoAngleDegrees(std::uint8_t angle_degrees) noexcept;
// Register a callback fired from EXTI0 ISR context on each button edge.
// Must be called before the scheduler starts.
void SetButtonEdgeCallback(ButtonEdgeCallback cb, void* ctx) noexcept;
// Register a callback fired from EXTI1 ISR context on joystick SW press.
// Must be called before the scheduler starts.
void SetJoystickPressCallback(JoystickPressCallback cb, void* ctx) noexcept;

} // namespace aegis::edge

extern "C" void Aegis_HandleExti0Irq(void);
extern "C" void Aegis_HandleExti1Irq(void);

#endif
