# Joystick SW Interrupt Wiring

This setup uses only the joystick switch (`SW`) as a digital EXTI interrupt
source. The analog axes (`VRx`, `VRy`) are intentionally left disconnected until
ADC sampling is added.

## Connections

| Joystick module pin | STM32F407G-DISC1 pin | Notes |
| --- | --- | --- |
| `GND` | `GND` | Common ground. |
| `5V` / `VCC` | `3V3` | Use 3.3 V supply, not 5 V. |
| `SW` | `PB1` | EXTI1 falling-edge interrupt, internal pull-up enabled. |
| `VRx` | disconnected | Future ADC input. |
| `VRy` | disconnected | Future ADC input. |

## Firmware Behavior

- `PB1` is configured as `GPIO_MODE_IT_FALLING` with internal pull-up.
- `EXTI1_IRQHandler` calls the platform glue, which posts a timestamp into a
  FreeRTOS queue from ISR context.
- `StateMachineTask` drains the queue, debounces presses, emits
  `AUDIT_JOYSTICK_PRESS`, pulses board LEDs, and shows `JOY PRESS` on the LCD
  status overlay when the LCD task is active.

Do not power the joystick module from 5 V while `VRx` or `VRy` are connected to
STM32 pins. The analog outputs can then exceed the STM32 3.3 V input range.
