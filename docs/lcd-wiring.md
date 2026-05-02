# QAPASS 16-Pin LCD Wiring

This project uses the 16-pin QAPASS/HD44780-compatible LCD in 4-bit write-only
mode. The Arduino Uno is used only as a 5 V power source. STM32 and Arduino
ground must be common.

## Power and Ground

| LCD pin | LCD signal | Connection |
| --- | --- | --- |
| 1 | VSS | Common GND |
| 2 | VDD | Arduino 5V |
| 5 | RW | Common GND |
| 16 | K | Common GND |

Common ground connections:

```text
Arduino GND -> STM32 GND
Arduino GND -> LCD pin 1 / VSS
LCD pin 5 / RW -> GND
LCD pin 16 / K -> GND
```

## Contrast

Use a 10k potentiometer:

```text
pot outer leg  -> Arduino 5V
pot outer leg  -> Common GND
pot middle leg -> LCD pin 3 / VO
```

If the two outer legs are swapped, only the turn direction changes. The middle
leg must go to LCD `VO`.

## Backlight

```text
LCD pin 15 / A -> Arduino 5V through 220 ohm resistor
LCD pin 16 / K -> Common GND
```

## STM32 Data and Control Lines

The LCD is driven in 4-bit mode. LCD pins `D0` through `D3` are unused.

| LCD pin | LCD signal | STM32F407G-DISC1 pin |
| --- | --- | --- |
| 4 | RS | PC0 |
| 6 | E | PC1 |
| 11 | D4 | PC2 |
| 12 | D5 | PC3 |
| 13 | D6 | PC4 |
| 14 | D7 | PC5 |

Unused LCD pins:

```text
LCD pin 7  / D0 -> not connected
LCD pin 8  / D1 -> not connected
LCD pin 9  / D2 -> not connected
LCD pin 10 / D3 -> not connected
```

## Full Wiring Summary

```text
LCD 1  VSS -> Common GND
LCD 2  VDD -> Arduino 5V
LCD 3  VO  -> 10k pot middle leg
LCD 4  RS  -> STM32 PC0
LCD 5  RW  -> Common GND
LCD 6  E   -> STM32 PC1
LCD 7  D0  -> not connected
LCD 8  D1  -> not connected
LCD 9  D2  -> not connected
LCD 10 D3  -> not connected
LCD 11 D4  -> STM32 PC2
LCD 12 D5  -> STM32 PC3
LCD 13 D6  -> STM32 PC4
LCD 14 D7  -> STM32 PC5
LCD 15 A   -> Arduino 5V through 220 ohm resistor
LCD 16 K   -> Common GND
```

## Firmware Mapping

The firmware LCD driver uses these GPIO assignments:

```text
RS -> PC0
E  -> PC1
D4 -> PC2
D5 -> PC3
D6 -> PC4
D7 -> PC5
RW -> GND, write-only mode
```

The UI creates the LCD status task through:

```text
cmd.create_task
task_type = 4
param = refresh period in 250 ms units
```

Default UI value `4` means a 1 second refresh period.
