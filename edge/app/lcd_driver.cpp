#include "lcd_driver.hpp"

#include "platform_io.hpp"
#include "stm32f4xx_hal.h"

namespace {

constexpr std::uint16_t kRsPin = GPIO_PIN_0; // LCD pin 4  RS -> PC0
constexpr std::uint16_t kEnPin = GPIO_PIN_1; // LCD pin 6  E  -> PC1
constexpr std::uint16_t kD4Pin = GPIO_PIN_2; // LCD pin 11 D4 -> PC2
constexpr std::uint16_t kD5Pin = GPIO_PIN_3; // LCD pin 12 D5 -> PC3
constexpr std::uint16_t kD6Pin = GPIO_PIN_4; // LCD pin 13 D6 -> PC4
constexpr std::uint16_t kD7Pin = GPIO_PIN_5; // LCD pin 14 D7 -> PC5
constexpr std::uint16_t kAllPins = kRsPin | kEnPin | kD4Pin | kD5Pin | kD6Pin | kD7Pin;
constexpr std::uint8_t kLcdColumns = 16U;

GPIO_TypeDef* const kLcdPort = GPIOC;

void ShortDelay() noexcept
{
    for (std::uint32_t i = 0U; i < 1200U; ++i) {
        __asm volatile ("nop");
    }
}

void SetPin(std::uint16_t pin, bool high) noexcept
{
    HAL_GPIO_WritePin(kLcdPort, pin, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void PulseEnable() noexcept
{
    SetPin(kEnPin, true);
    ShortDelay();
    SetPin(kEnPin, false);
    ShortDelay();
}

void WriteNibble(std::uint8_t nibble) noexcept
{
    SetPin(kD4Pin, (nibble & 0x01U) != 0U);
    SetPin(kD5Pin, (nibble & 0x02U) != 0U);
    SetPin(kD6Pin, (nibble & 0x04U) != 0U);
    SetPin(kD7Pin, (nibble & 0x08U) != 0U);
    PulseEnable();
}

void WriteByte(std::uint8_t value, bool data) noexcept
{
    SetPin(kRsPin, data);
    WriteNibble(static_cast<std::uint8_t>((value >> 4U) & 0x0FU));
    WriteNibble(static_cast<std::uint8_t>(value & 0x0FU));
    ShortDelay();
}

void Command(std::uint8_t value) noexcept
{
    WriteByte(value, false);
}

void Data(std::uint8_t value) noexcept
{
    WriteByte(value, true);
}

void WritePaddedLine(const char* text) noexcept
{
    std::uint8_t col = 0U;
    while (col < kLcdColumns && text[col] != '\0') {
        Data(static_cast<std::uint8_t>(text[col]));
        ++col;
    }
    while (col < kLcdColumns) {
        Data(static_cast<std::uint8_t>(' '));
        ++col;
    }
}

} // namespace

namespace aegis::edge {

bool InitializeLcd() noexcept
{
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    gpio.Pin = kAllPins;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(kLcdPort, &gpio);
    HAL_GPIO_WritePin(kLcdPort, kAllPins, GPIO_PIN_RESET);

    DelayMs(50U);

    SetPin(kRsPin, false);
    WriteNibble(0x03U);
    DelayMs(5U);
    WriteNibble(0x03U);
    DelayMs(5U);
    WriteNibble(0x03U);
    DelayMs(1U);
    WriteNibble(0x02U);
    DelayMs(1U);

    Command(0x28U); // 4-bit, 2 lines, 5x8 font.
    Command(0x08U); // Display off.
    Command(0x01U); // Clear display.
    DelayMs(2U);
    Command(0x06U); // Entry mode: increment, no shift.
    Command(0x0CU); // Display on, cursor off.

    return true;
}

void LcdClear() noexcept
{
    Command(0x01U);
    DelayMs(2U);
}

void LcdWriteLines(const char* line0, const char* line1) noexcept
{
    Command(0x80U);
    WritePaddedLine(line0);
    Command(0xC0U);
    WritePaddedLine(line1);
}

} // namespace aegis::edge
