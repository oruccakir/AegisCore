#include "runtime/edge_runtime.hpp"

#include "lcd_driver.hpp"
#include "platform_io.hpp"

#include "stm32f4xx_hal.h"

namespace aegis::edge {
namespace {

const char* StateShortName(std::uint8_t state) noexcept
{
    switch (state) {
        case 0U: return "IDLE";
        case 1U: return "SRCH";
        case 2U: return "TRCK";
        case 3U: return "SAFE";
        default: return "UNKN";
    }
}

void FillLine(char* line) noexcept
{
    for (std::uint8_t i = 0U; i < 16U; ++i) {
        line[i] = ' ';
    }
    line[16U] = '\0';
}

void PutText(char* line, std::uint8_t pos, const char* text) noexcept
{
    while (pos < 16U && *text != '\0') {
        line[pos] = *text;
        ++pos;
        ++text;
    }
}

void Put2(char* line, std::uint8_t pos, std::uint32_t value) noexcept
{
    const std::uint32_t clamped = (value > 99U) ? 99U : value;
    line[pos] = static_cast<char>('0' + (clamped / 10U));
    line[static_cast<std::uint8_t>(pos + 1U)] =
        static_cast<char>('0' + (clamped % 10U));
}

void Put3(char* line, std::uint8_t pos, std::uint32_t value) noexcept
{
    const std::uint32_t clamped = (value > 999U) ? 999U : value;
    line[pos] = static_cast<char>('0' + (clamped / 100U));
    line[static_cast<std::uint8_t>(pos + 1U)] =
        static_cast<char>('0' + ((clamped / 10U) % 10U));
    line[static_cast<std::uint8_t>(pos + 2U)] =
        static_cast<char>('0' + (clamped % 10U));
}

void Put4(char* line, std::uint8_t pos, std::uint32_t value) noexcept
{
    const std::uint32_t clamped = (value > 9999U) ? 9999U : value;
    line[pos] = static_cast<char>('0' + (clamped / 1000U));
    line[static_cast<std::uint8_t>(pos + 1U)] =
        static_cast<char>('0' + ((clamped / 100U) % 10U));
    line[static_cast<std::uint8_t>(pos + 2U)] =
        static_cast<char>('0' + ((clamped / 10U) % 10U));
    line[static_cast<std::uint8_t>(pos + 3U)] =
        static_cast<char>('0' + (clamped % 10U));
}

char HexNibble(std::uint8_t value) noexcept
{
    const std::uint8_t nibble = static_cast<std::uint8_t>(value & 0x0FU);
    return static_cast<char>(nibble < 10U ? ('0' + nibble) : ('A' + (nibble - 10U)));
}

void PutHex4(char* line, std::uint8_t pos, std::uint32_t value) noexcept
{
    const std::uint16_t clamped = static_cast<std::uint16_t>(value & 0xFFFFU);
    line[pos] = HexNibble(static_cast<std::uint8_t>(clamped >> 12U));
    line[static_cast<std::uint8_t>(pos + 1U)] = HexNibble(static_cast<std::uint8_t>(clamped >> 8U));
    line[static_cast<std::uint8_t>(pos + 2U)] = HexNibble(static_cast<std::uint8_t>(clamped >> 4U));
    line[static_cast<std::uint8_t>(pos + 3U)] = HexNibble(static_cast<std::uint8_t>(clamped));
}

void PutCpu(char* line, std::uint8_t pos, std::uint16_t cpu_x10) noexcept
{
    const std::uint16_t clamped = (cpu_x10 > 999U) ? 999U : cpu_x10;
    const std::uint16_t whole = static_cast<std::uint16_t>(clamped / 10U);
    line[pos] = (whole >= 10U) ? static_cast<char>('0' + (whole / 10U)) : ' ';
    line[static_cast<std::uint8_t>(pos + 1U)] = static_cast<char>('0' + (whole % 10U));
    line[static_cast<std::uint8_t>(pos + 2U)] = '.';
    line[static_cast<std::uint8_t>(pos + 3U)] = static_cast<char>('0' + (clamped % 10U));
}

const char* DetectionShortName(std::uint8_t class_id) noexcept
{
    return class_id == kDetectionClassPerson ? "PERSON" : "NONE";
}

const char* ResetReasonShortName(std::uint32_t bits) noexcept
{
    if ((bits & RCC_CSR_SFTRSTF) != 0U) { return "SW RESET"; }
    if ((bits & RCC_CSR_IWDGRSTF) != 0U) { return "IWDG"; }
    if ((bits & RCC_CSR_WWDGRSTF) != 0U) { return "WWDG"; }
    if ((bits & RCC_CSR_PORRSTF) != 0U) { return "POWER"; }
    if ((bits & RCC_CSR_PINRSTF) != 0U) { return "NRST PIN"; }
    if ((bits & RCC_CSR_BORRSTF) != 0U) { return "BROWNOUT"; }
    if ((bits & RCC_CSR_LPWRRSTF) != 0U) { return "LOW PWR"; }
    return "UNKNOWN";
}

void RenderAlertPage(char* line0, char* line1, std::uint8_t code, std::uint16_t value) noexcept
{
    PutText(line0, 0U, "EVENT");
    switch (code) {
        case AuditCode::kBoot:
            PutText(line0, 6U, "BOOT");
            PutText(line1, 0U, ResetReasonShortName(gBootResetReasonBits));
            break;
        case AuditCode::kTaskCreate:
            PutText(line0, 6U, "TASK ADD");
            PutText(line1, 0U, "SLOT");
            Put2(line1, 5U, value);
            break;
        case AuditCode::kTaskDelete:
            PutText(line0, 6U, "TASK DEL");
            PutText(line1, 0U, "SLOT");
            Put2(line1, 5U, value);
            break;
        case AuditCode::kRangeLock:
            PutText(line0, 6U, "RANGE LOCK");
            PutText(line1, 0U, "ANGLE");
            Put3(line1, 6U, value);
            break;
        case AuditCode::kVisionHit:
            PutText(line0, 6U, "VISION HIT");
            PutText(line1, 0U, "CONF");
            Put3(line1, 5U, value);
            line1[8U] = '%';
            break;
        case AuditCode::kSystemReset:
            PutText(line0, 6U, "RESET");
            PutText(line1, 0U, "REBOOTING");
            break;
        case AuditCode::kFailSafe:
            PutText(line0, 6U, "FAIL SAFE");
            PutText(line1, 0U, "LOCK ACTIVE");
            break;
        case AuditCode::kJoystickPress:
            PutText(line0, 6U, "JOY PRESS");
            PutText(line1, 0U, "COUNT");
            Put3(line1, 6U, value);
            break;
        default:
            PutText(line0, 6U, "UNKNOWN");
            PutText(line1, 0U, "CODE");
            Put3(line1, 5U, code);
            break;
    }
}

void RenderSystemPage(char* line0, char* line1) noexcept
{
    const std::uint32_t uptime_s = gLcdUptimeMs / 1000U;
    const std::uint32_t uptime_m = (uptime_s / 60U) % 100U;
    const std::uint32_t uptime_r = uptime_s % 60U;

    PutText(line0, 0U, "AC2");
    PutText(line0, 4U, StateShortName(gLcdState));
    Put2(line0, 10U, uptime_m);
    line0[12U] = ':';
    Put2(line0, 13U, uptime_r);

    PutText(line1, 0U, "CPU");
    PutCpu(line1, 4U, gLcdCpuLoadX10);
    PutText(line1, 9U, "HB");
    line1[11U] = static_cast<char>('0' + ((gLcdHbMissCount > 9U) ? 9U : gLcdHbMissCount));
    PutText(line1, 13U, "T");
    Put2(line1, 14U, gLcdTaskCount);
}

void RenderTaskPage(char* line0, char* line1) noexcept
{
    PutText(line0, 0U, "TASKS");
    Put2(line0, 6U, gLcdTaskCount);
    PutText(line0, 10U, "LIVE");

    PutText(line1, 0U, "MIN");
    Put4(line1, 4U, gLcdStackMin);
    PutText(line1, 9U, "WORDS");
    const std::uint32_t age_s = (MillisecondsSinceBoot() - gLcdTaskListMs) / 1000U;
    Put2(line1, 14U, age_s);
}

void RenderBootPage(char* line0, char* line1) noexcept
{
    PutText(line0, 0U, "BOOT");
    PutText(line0, 5U, ResetReasonShortName(gBootResetReasonBits));
    PutText(line1, 0U, "RCC");
    PutHex4(line1, 4U, (gBootResetReasonBits >> 16U) & 0xFFFFU);
    PutHex4(line1, 9U, gBootResetReasonBits & 0xFFFFU);
}

void RenderRangePage(char* line0, char* line1) noexcept
{
    PutText(line0, 0U, "RANGE");
    if (!gLcdRangeActive) {
        PutText(line0, 6U, "IDLE");
        PutText(line1, 0U, "NO ACTIVE TASK");
        return;
    }

    if (!gLcdRangeSeen) {
        PutText(line1, 0U, "NO SAMPLE");
        return;
    }

    PutText(line0, 6U, "A");
    Put3(line0, 7U, gLcdRangeAngle);
    PutText(line0, 11U, gLcdRangeLocked ? "LOCK" : "SCAN");

    PutText(line1, 0U, gLcdRangeValid ? "D" : "D---");
    if (gLcdRangeValid) {
        Put3(line1, 1U, gLcdRangeDistCm);
    }
    PutText(line1, 5U, "TH");
    Put3(line1, 7U, gLcdRangeThresh);
    PutText(line1, 12U, gLcdRangeLocked ? "LOCK" : "OPEN");
}

void RenderVisionPage(char* line0, char* line1) noexcept
{
    PutText(line0, 0U, "VISION");
    if (!gLcdDetectSeen) {
        PutText(line1, 0U, "NO SAMPLE");
        return;
    }

    PutText(line0, 7U, DetectionShortName(gLcdDetectClass));
    PutText(line1, 0U, "CONF");
    Put3(line1, 5U, gLcdDetectConf);
    line1[8U] = '%';
    PutText(line1, 10U, gLcdDetectClass == kDetectionClassPerson ? "TRACK" : "CLEAR");
}

} // namespace

void UserLcdStatusTask(void* ctx) noexcept
{
    const auto* slot = static_cast<const UserTaskSlot*>(ctx);
    const TickType_t refresh_ticks = pdMS_TO_TICKS(
        static_cast<std::uint32_t>(slot->param > 0U ? slot->param : 4U) * 250U);

    if (!InitializeLcd()) {
        vTaskDelete(nullptr);
    }

    LcdWriteLines("AEGIS CORE", "LCD ONLINE");
    vTaskDelay(pdMS_TO_TICKS(1500U));

    std::uint8_t page = 0U;
    std::uint8_t ticks_on_page = 0U;

    for (;;) {
        char line0[17] = {};
        char line1[17] = {};
        FillLine(line0);
        FillLine(line1);

        const TickType_t now_ticks = xTaskGetTickCount();
        const std::uint8_t alert_code = gLcdAlertCode;
        if (alert_code != 0U && now_ticks < gLcdAlertUntilTick) {
            RenderAlertPage(line0, line1, alert_code, gLcdAlertValue);
        } else {
            if (alert_code != 0U) {
                gLcdAlertCode = 0U;
            }

            switch (page) {
                case 0U:
                    RenderSystemPage(line0, line1);
                    break;
                case 1U:
                    RenderTaskPage(line0, line1);
                    break;
                case 2U:
                    RenderRangePage(line0, line1);
                    break;
                case 3U:
                    RenderVisionPage(line0, line1);
                    break;
                default:
                    RenderBootPage(line0, line1);
                    break;
            }
        }

        LcdWriteLines(line0, line1);
        vTaskDelay(refresh_ticks);

        ticks_on_page = static_cast<std::uint8_t>(ticks_on_page + 1U);
        if (ticks_on_page >= 3U) {
            ticks_on_page = 0U;
            page = static_cast<std::uint8_t>((page + 1U) % 5U);
            LcdClear();
        }
    }
}

} // namespace aegis::edge
