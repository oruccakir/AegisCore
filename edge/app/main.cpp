#include <array>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "stm32f4xx_hal.h"

#include "ac2_framer.hpp"
#include "button_classifier.hpp"
#include "fail_safe_supervisor.hpp"
#include "hmac_sha256.hpp"
#include "lcd_driver.hpp"
#include "mpu_config.hpp"
#include "platform_io.hpp"
#include "post.hpp"
#include "rate_limiter.hpp"
#include "replay_guard.hpp"
#include "simulation_engine.hpp"
#include "state_machine.hpp"
#include "telemetry.hpp"
#include "uart_driver.hpp"
#include "version.hpp"
#include "watchdog.hpp"

namespace {

using namespace aegis::edge;

// ---- Task priorities / stack sizes (ARCH-03) --------------------------------
constexpr UBaseType_t    kPrioUartRx      = 5U;
constexpr UBaseType_t    kPrioStateMachine = 4U;
constexpr UBaseType_t    kPrioTelemetryTx  = 3U;
constexpr UBaseType_t    kPrioHeartbeat    = 2U;
constexpr std::uint32_t  kStackUartRx      = 512U;
constexpr std::uint32_t  kStackStateMachine= 512U;
constexpr std::uint32_t  kStackTelemetryTx = 512U;
constexpr std::uint32_t  kStackHeartbeat   = 256U;

// ---- Queue depths -----------------------------------------------------------
constexpr std::uint32_t kButtonQueueLen   = 8U;
constexpr std::uint32_t kTxQueueLen       = 4U;
constexpr std::uint32_t kRemoteCmdQueueLen= 4U;

// ---- Pre-shared key — SR-01 / SR-07 placeholder ----------------------------
constexpr std::uint8_t kPsk[] = {
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF
};

constexpr std::uint32_t kSimulationSeed        = 0x12345678U;
constexpr TickType_t    kSimulationPeriodTicks  = pdMS_TO_TICKS(100U);
constexpr TickType_t    kHeartbeatPeriodTicks   = pdMS_TO_TICKS(1000U);
constexpr TickType_t    kTelemetryPeriodTicks   = pdMS_TO_TICKS(1000U);
constexpr TickType_t    kTaskListPeriodTicks    = pdMS_TO_TICKS(2000U);
constexpr TickType_t    kSystemResetDelayTicks  = pdMS_TO_TICKS(250U);
constexpr TickType_t    kLcdAlertDurationTicks  = pdMS_TO_TICKS(3000U);
constexpr std::uint8_t  kDetectionClassNone     = 0U;
constexpr std::uint8_t  kDetectionClassPerson   = 1U;
constexpr std::uint8_t  kDetectionThresholdPct  = 50U;

// ---- Queue item types -------------------------------------------------------

struct TxItem
{
    std::uint8_t cmd;
    std::uint8_t payload[kAC2MaxPayload];
    std::uint8_t len;
};

struct RemoteCmd
{
    std::uint8_t  cmd;
    std::uint32_t seq;
    std::uint8_t  payload[kAC2MaxPayload];
    std::uint8_t  payload_len;
};

// ---- User task infrastructure -----------------------------------------------

enum class UserTaskType : std::uint8_t {
    Blink = 0U,
    RangeScan = 3U,
    LcdStatus = 4U
};

struct UserTaskSlot {
    StaticTask_t  tcb;
    StackType_t   stack[384U];
    TaskHandle_t  handle;
    UserTaskType  task_type;
    std::uint8_t  param;
    bool          in_use;
};

constexpr std::uint8_t kUserTaskSlots = 4U;
constexpr UBaseType_t  kMaxTaskCount  = 9U; // Telemetry payload cap: up to 9 reported tasks.

UserTaskSlot  gUserTasks[kUserTaskSlots]       = {};
TaskStatus_t  gTaskStatusBuf[kMaxTaskCount]    = {};

static_assert(1U + kMaxTaskCount * sizeof(PackedTaskEntry) <= kAC2MaxPayload,
              "task list payload overflows AC2 max payload");

// ---- Static task allocations ------------------------------------------------
StaticTask_t  gUartRxTCB, gSMTaskTCB, gTxTaskTCB, gHbTaskTCB;
TaskHandle_t  gUartRxHandle    = nullptr;
TaskHandle_t  gSMHandle        = nullptr;
TaskHandle_t  gTxHandle        = nullptr;
TaskHandle_t  gHbHandle        = nullptr;
StackType_t  gUartRxStack[kStackUartRx];
StackType_t  gSMStack[kStackStateMachine];
StackType_t  gTxStack[kStackTelemetryTx];
StackType_t  gHbStack[kStackHeartbeat];

// ---- Static queue storage ---------------------------------------------------
StaticQueue_t                               gButtonQueueCB;
StaticQueue_t                               gTxQueueCB;
StaticQueue_t                               gRemoteCmdQueueCB;
std::array<RawButtonEdge, kButtonQueueLen>  gButtonQueueStorage  = {};
std::array<TxItem,        kTxQueueLen>      gTxQueueStorage      = {};
std::array<RemoteCmd,     kRemoteCmdQueueLen> gRemoteCmdQueueStorage = {};

QueueHandle_t gButtonQueue    = nullptr;
QueueHandle_t gTxQueue        = nullptr;
QueueHandle_t gRemoteCmdQueue = nullptr;

// ---- Driver / security state ------------------------------------------------
UartDriver  gUart;
AC2Parser   gParser;
ReplayGuard gReplay;
RateLimiter gRateLimiter;

// TX sequence number — only TelemetryTxTask writes this.
std::uint32_t gTxSeq = 0U;

// Blue LED off time — set when GetVersion arrives, cleared by StateMachineTask.
std::uint32_t gVersionLedOffMs = 0U;

// Blue LED off time for detection pulse (300 ms).
std::uint32_t gDetectionLedOffMs = 0U;
std::uint32_t gGreenPulseOffMs   = 0U;
std::uint32_t gYellowPulseOffMs  = 0U;
std::uint32_t gRedPulseOffMs     = 0U;

// Last detection result received from gateway.
struct LastDetection {
    std::uint8_t  class_id       = 0U;
    std::uint8_t  confidence_pct = 0U;
    bool          valid          = false;
};
LastDetection gLastDetection = {};

volatile std::uint8_t  gLcdState       = 0U;
volatile std::uint16_t gLcdCpuLoadX10  = 0U;
volatile std::uint8_t  gLcdHbMissCount = 0U;
volatile std::uint8_t  gLcdTaskCount   = 0U;
volatile std::uint32_t gLcdUptimeMs    = 0U;
volatile std::uint32_t gLcdTaskListMs  = 0U;
volatile std::uint16_t gLcdStackMin    = 0U;
volatile std::uint8_t  gLcdRangeAngle  = 0U;
volatile std::uint16_t gLcdRangeDistCm = 0U;
volatile std::uint16_t gLcdRangeThresh = 0U;
volatile bool          gLcdRangeValid  = false;
volatile bool          gLcdRangeLocked = false;
volatile bool          gLcdRangeSeen   = false;
volatile bool          gLcdRangeActive = false;
volatile std::uint8_t  gLcdDetectClass = 0U;
volatile std::uint8_t  gLcdDetectConf  = 0U;
volatile bool          gLcdDetectSeen  = false;
volatile std::uint8_t  gLcdAlertCode   = 0U;
volatile std::uint16_t gLcdAlertValue  = 0U;
volatile TickType_t    gLcdAlertUntilTick = 0U;
volatile bool          gSystemResetPending = false;
volatile TickType_t    gSystemResetDueTick = 0U;
std::uint32_t          gBootResetReasonBits = 0U;
bool                   gVisionPersonActive = false;

// Yellow LED off time — set when ManualLock arrives, cleared by StateMachineTask.
std::uint32_t gManualLockLedOffMs = 0U;

// Shared blink state written by UserBlinkTask, read by StateMachineTask.
// volatile bool is single-byte; Cortex-M4 byte stores/loads are atomic.
volatile bool gUserBlinkState = false;

// ---- Raw receive buffer for UartRxTask (avoid function-local static) --------
std::uint8_t gUartRxBuf[kAC2MaxFrame] = {};

// ---- TX helpers -------------------------------------------------------------

static void QueueTx(std::uint8_t cmd,
                    const std::uint8_t* payload,
                    std::uint8_t len) noexcept
{
    TxItem item = {};
    item.cmd = cmd;
    item.len = (len <= kAC2MaxPayload) ? len : kAC2MaxPayload;
    for (std::uint8_t i = 0U; i < item.len; ++i) { item.payload[i] = payload[i]; }
    (void)xQueueSend(gTxQueue, &item, 0U);
}

static void SendAck(std::uint32_t echoed_seq) noexcept
{
    PayloadAck p = {};
    p.echoed_seq = echoed_seq;
    QueueTx(CmdId::kAck,
            reinterpret_cast<const std::uint8_t*>(&p),
            static_cast<std::uint8_t>(sizeof(p)));
}

static void SendNack(std::uint32_t echoed_seq, std::uint8_t err) noexcept
{
    PayloadNack p = {};
    p.echoed_seq = echoed_seq;
    p.err_code   = err;
    QueueTx(CmdId::kNack,
            reinterpret_cast<const std::uint8_t*>(&p),
            static_cast<std::uint8_t>(sizeof(p)));
}

static void QueueAuditEvent(std::uint8_t event_code, std::uint16_t count) noexcept
{
    PayloadAuditEvent p = {};
    p.event_code = event_code;
    p.count      = count;
    QueueTx(CmdId::kAuditEvent,
            reinterpret_cast<const std::uint8_t*>(&p),
            static_cast<std::uint8_t>(sizeof(p)));
}

static void PulseFeedback(std::uint8_t event_code) noexcept
{
    const std::uint32_t until_ms = MillisecondsSinceBoot() + 350U;
    switch (event_code) {
        case AuditCode::kTaskCreate:
            gGreenPulseOffMs = until_ms;
            break;
        case AuditCode::kTaskDelete:
        case AuditCode::kRangeLock:
            gYellowPulseOffMs = until_ms;
            break;
        case AuditCode::kVisionHit:
            gDetectionLedOffMs = until_ms;
            break;
        case AuditCode::kSystemReset:
        case AuditCode::kFailSafe:
            gRedPulseOffMs = until_ms;
            break;
        default:
            gVersionLedOffMs = until_ms;
            break;
    }
}

static void SetLcdAlert(std::uint8_t event_code, std::uint16_t value) noexcept
{
    gLcdAlertCode      = event_code;
    gLcdAlertValue     = value;
    gLcdAlertUntilTick = xTaskGetTickCount() + kLcdAlertDurationTicks;
    PulseFeedback(event_code);
    QueueAuditEvent(event_code, value);
}

static void QueueBootReport() noexcept
{
    PayloadBootReport p = {};
    p.reset_reason_bits = gBootResetReasonBits;
    QueueTx(CmdId::kBootReport,
            reinterpret_cast<const std::uint8_t*>(&p),
            static_cast<std::uint8_t>(sizeof(p)));
}

static void QueueRangeScanReport(std::uint8_t angle_deg,
                                 std::uint16_t distance_cm,
                                 bool measurement_valid,
                                 bool locked,
                                 std::uint16_t threshold_cm) noexcept
{
    gLcdRangeAngle  = angle_deg;
    gLcdRangeDistCm = distance_cm;
    gLcdRangeThresh = threshold_cm;
    gLcdRangeValid  = measurement_valid;
    gLcdRangeLocked = locked;
    gLcdRangeSeen   = true;

    PayloadRangeScanReport p = {};
    p.angle_deg = angle_deg;
    p.distance_cm = distance_cm;
    p.flags = static_cast<std::uint8_t>((locked ? 0x01U : 0x00U) |
                                        (measurement_valid ? 0x02U : 0x00U));
    p.threshold_cm = threshold_cm;
    QueueTx(CmdId::kRangeScanReport,
            reinterpret_cast<const std::uint8_t*>(&p),
            static_cast<std::uint8_t>(sizeof(p)));
}

// ---- User task functions ----------------------------------------------------

void UserBlinkTask(void* ctx) noexcept
{
    const auto* slot = static_cast<const UserTaskSlot*>(ctx);
    // param = half-period in units of 100 ms (default 5 → 500 ms half-period = 1 Hz)
    const TickType_t half = pdMS_TO_TICKS(
        static_cast<std::uint32_t>(slot->param > 0U ? slot->param : 5U) * 100U);
    for (;;) {
        gUserBlinkState = !gUserBlinkState;
        vTaskDelay(half);
    }
}

void UserRangeScanTask(void* ctx) noexcept
{
    const auto* slot = static_cast<const UserTaskSlot*>(ctx);
    const std::uint16_t near_threshold_cm =
        static_cast<std::uint16_t>(slot->param > 0U ? slot->param : 30U);
    gLcdRangeActive = true;
    gLcdRangeSeen   = false;
    gLcdRangeValid  = false;
    gLcdRangeLocked = false;

    constexpr std::uint8_t kMinAngle = 20U;
    constexpr std::uint8_t kMaxAngle = 160U;
    constexpr std::uint8_t kStepDegrees = 2U;
    constexpr std::uint8_t kMeasureEverySteps = 3U;
    constexpr TickType_t kStepDelay = pdMS_TO_TICKS(40U);
    constexpr TickType_t kLockDelay = pdMS_TO_TICKS(80U);
    constexpr std::uint8_t kLostSamplesToRelease = 6U;

    std::uint8_t angle = 90U;
    bool increasing = true;
    bool locked = false;
    std::uint8_t locked_angle = angle;
    std::uint8_t step_count = 0U;
    std::uint8_t lost_samples = 0U;
    bool range_lock_announced = false;

    for (;;)
    {
        if (locked) {
            aegis::edge::SetServoAngleDegrees(locked_angle);

            std::uint16_t distance_cm = 0U;
            const bool valid = aegis::edge::MeasureRangeCm(distance_cm);
            QueueRangeScanReport(locked_angle, distance_cm, valid, true, near_threshold_cm);
            if (valid && distance_cm <= static_cast<std::uint16_t>(near_threshold_cm + 5U)) {
                lost_samples = 0U;
            } else {
                lost_samples = static_cast<std::uint8_t>(lost_samples + 1U);
                if (lost_samples >= kLostSamplesToRelease) {
                    locked = false;
                    range_lock_announced = false;
                    lost_samples = 0U;
                    angle = locked_angle;
                }
            }

            vTaskDelay(kLockDelay);
            continue;
        }

        aegis::edge::SetServoAngleDegrees(angle);
        vTaskDelay(kStepDelay);
        step_count = static_cast<std::uint8_t>(step_count + 1U);
        bool report_valid = false;
        std::uint16_t report_distance_cm = 0U;

        if (step_count >= kMeasureEverySteps) {
            step_count = 0U;
            std::uint16_t distance_cm = 0U;
            const bool valid = aegis::edge::MeasureRangeCm(distance_cm);
            report_valid = valid;
            report_distance_cm = distance_cm;
            if (valid && distance_cm <= near_threshold_cm) {
                locked = true;
                locked_angle = angle;
                lost_samples = 0U;
                if (!range_lock_announced) {
                    SetLcdAlert(AuditCode::kRangeLock, angle);
                    range_lock_announced = true;
                }
                QueueRangeScanReport(angle, distance_cm, true, true, near_threshold_cm);
                vTaskDelay(kLockDelay);
                continue;
            }
        }

        QueueRangeScanReport(angle, report_distance_cm, report_valid, false, near_threshold_cm);

        if (increasing) {
            if (angle >= static_cast<std::uint8_t>(kMaxAngle - kStepDegrees)) {
                angle = kMaxAngle;
                increasing = false;
            } else {
                angle = static_cast<std::uint8_t>(angle + kStepDegrees);
            }
        } else {
            if (angle <= static_cast<std::uint8_t>(kMinAngle + kStepDegrees)) {
                angle = kMinAngle;
                increasing = true;
            } else {
                angle = static_cast<std::uint8_t>(angle - kStepDegrees);
            }
        }
    }
}

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
    if ((bits & RCC_CSR_SFTRSTF) != 0U)   { return "SW RESET"; }
    if ((bits & RCC_CSR_IWDGRSTF) != 0U)  { return "IWDG"; }
    if ((bits & RCC_CSR_WWDGRSTF) != 0U)  { return "WWDG"; }
    if ((bits & RCC_CSR_PORRSTF) != 0U)   { return "POWER"; }
    if ((bits & RCC_CSR_PINRSTF) != 0U)   { return "NRST PIN"; }
    if ((bits & RCC_CSR_BORRSTF) != 0U)   { return "BROWNOUT"; }
    if ((bits & RCC_CSR_LPWRRSTF) != 0U)  { return "LOW PWR"; }
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

static std::int8_t CreateUserTask(std::uint8_t type, std::uint8_t param) noexcept
{
    for (std::uint8_t i = 0U; i < kUserTaskSlots; ++i) {
        if (gUserTasks[i].in_use) { continue; }

        TaskFunction_t fn = UserBlinkTask;
        char tname[configMAX_TASK_NAME_LEN] = {};

        switch (static_cast<UserTaskType>(type)) {
            case UserTaskType::Blink:
                fn       = UserBlinkTask;
                tname[0] = 'B'; tname[1] = 'l'; tname[2] = 'n'; tname[3] = 'k';
                tname[4] = static_cast<char>('0' + i);
                break;
            case UserTaskType::RangeScan:
                fn       = UserRangeScanTask;
                tname[0] = 'R'; tname[1] = 'n'; tname[2] = 'g'; tname[3] = 'S';
                tname[4] = static_cast<char>('0' + i);
                break;
            case UserTaskType::LcdStatus:
                fn       = UserLcdStatusTask;
                tname[0] = 'L'; tname[1] = 'C'; tname[2] = 'D';
                tname[3] = static_cast<char>('0' + i);
                break;
            default:
                return -1;
        }

        gUserTasks[i].task_type = static_cast<UserTaskType>(type);
        gUserTasks[i].param     = param;

        const std::uint32_t depth =
            static_cast<std::uint32_t>(sizeof(gUserTasks[i].stack) / sizeof(StackType_t));
        gUserTasks[i].handle = xTaskCreateStatic(
            fn, tname, depth, &gUserTasks[i], 1U,
            gUserTasks[i].stack, &gUserTasks[i].tcb);

        if (gUserTasks[i].handle == nullptr) { return -1; }
        gUserTasks[i].in_use = true;
        return static_cast<std::int8_t>(i);
    }
    return -1;
}

static bool DeleteUserTask(std::uint8_t slot_index) noexcept
{
    if (slot_index >= kUserTaskSlots)   { return false; }
    if (!gUserTasks[slot_index].in_use) { return false; }

    vTaskDelete(gUserTasks[slot_index].handle);
    gUserTasks[slot_index].handle = nullptr;
    gUserTasks[slot_index].in_use = false;

    // If no other Blink task is running, release the shared LED flag.
    if (gUserTasks[slot_index].task_type == UserTaskType::Blink) {
        bool any_blink = false;
        for (std::uint8_t s = 0U; s < kUserTaskSlots; ++s) {
            if (gUserTasks[s].in_use &&
                gUserTasks[s].task_type == UserTaskType::Blink) {
                any_blink = true;
                break;
            }
        }
        if (!any_blink) { gUserBlinkState = false; }
    }

    if (gUserTasks[slot_index].task_type == UserTaskType::RangeScan) {
        aegis::edge::SetServoAngleDegrees(90U);

        bool any_range_scan = false;
        for (std::uint8_t s = 0U; s < kUserTaskSlots; ++s) {
            if (gUserTasks[s].in_use &&
                gUserTasks[s].task_type == UserTaskType::RangeScan) {
                any_range_scan = true;
                break;
            }
        }

        if (!any_range_scan) {
            gLcdRangeActive = false;
            gLcdRangeSeen   = false;
            gLcdRangeValid  = false;
            gLcdRangeLocked = false;
            gLcdRangeAngle  = 0U;
            gLcdRangeDistCm = 0U;
            gLcdRangeThresh = 0U;
        }
    }

    if (gUserTasks[slot_index].task_type == UserTaskType::LcdStatus) {
        LcdClear();
    }

    return true;
}

// ---- AC2 parser callback (UartRxTask context) -------------------------------

static void OnAC2Frame(const AC2Frame& frame, void* /*ctx*/) noexcept
{
    const auto now_ms = MillisecondsSinceBoot();

    // Build HMAC input: CMD_ID (1 byte) + PAYLOAD (IRS §5.3).
    std::uint8_t hmac_in[1U + kAC2MaxPayload] = {};
    hmac_in[0U] = frame.cmd;
    for (std::uint8_t i = 0U; i < frame.payload_len; ++i)
    {
        hmac_in[1U + i] = frame.payload[i];
    }

    // SR-01: HMAC authentication.
    if (!HMAC_SHA256_Verify(kPsk,
                             static_cast<std::uint8_t>(sizeof(kPsk)),
                             hmac_in,
                             static_cast<std::uint16_t>(1U + frame.payload_len),
                             frame.hmac,
                             kHmacTruncLen))
    {
        FailSafeSupervisor::Instance().OnHmacFailure();
        SendNack(frame.seq, ErrCode::kAuthFail);
        return;
    }

    // SR-02: Replay protection.
    if (!gReplay.Check(frame.seq))
    {
        SendNack(frame.seq, ErrCode::kReplay);
        return;
    }

    // SAF-04: Gateway heartbeat — update supervisor, no further dispatch.
    if (frame.cmd == CmdId::kHeartbeat)
    {
        FailSafeSupervisor::Instance().OnGatewayHeartbeatReceived(now_ms);
        return;
    }

    // SAF-02: Commands are blocked in fail-safe except explicit system reset.
    if (FailSafeSupervisor::Instance().IsTriggered() && frame.cmd != CmdId::kSystemReset)
    {
        SendNack(frame.seq, ErrCode::kFailSafeLock);
        return;
    }

    // SR-05: Rate limiting.
    if (frame.cmd == CmdId::kManualLock && !gRateLimiter.Allow(frame.cmd, now_ms))
    {
        SendNack(frame.seq, ErrCode::kRateLimited);
        return;
    }

    RemoteCmd rcmd = {};
    rcmd.cmd         = frame.cmd;
    rcmd.seq         = frame.seq;
    rcmd.payload_len = frame.payload_len;
    for (std::uint8_t i = 0U; i < frame.payload_len; ++i)
    {
        rcmd.payload[i] = frame.payload[i];
    }
    (void)xQueueSend(gRemoteCmdQueue, &rcmd, 0U);
}

static void QueueTaskList() noexcept; // forward declaration — defined in telemetry helpers

// ---- Remote-command dispatcher (StateMachineTask context) -------------------

static void DispatchRemoteCmd(StateMachine& sm,
                               const RemoteCmd& rcmd) noexcept
{
    const auto now_ms = MillisecondsSinceBoot();

    switch (rcmd.cmd)
    {
    case CmdId::kManualLock:
        gManualLockLedOffMs = MillisecondsSinceBoot() + 1000U;
        sm.ForceFailSafe(now_ms);
        FailSafeSupervisor::Instance().ReportEvent(FailSafeEvent::ExternalTrigger);
        SetLcdAlert(AuditCode::kFailSafe, 0U);
        SendAck(rcmd.seq);
        break;

    case CmdId::kGetVersion:
        {
            gVersionLedOffMs = MillisecondsSinceBoot() + 1000U;

            PayloadVersionReport vr = {};
            vr.major    = kVersionMajor;
            vr.minor    = kVersionMinor;
            vr.patch    = kVersionPatch;
            vr.build_ts = kBuildTimestamp;
            for (std::uint8_t i = 0U;
                 i < sizeof(vr.git_sha) && kGitSha[i] != '\0'; ++i)
            {
                vr.git_sha[i] = static_cast<std::uint8_t>(kGitSha[i]);
            }
            QueueTx(CmdId::kVersionReport,
                    reinterpret_cast<const std::uint8_t*>(&vr),
                    static_cast<std::uint8_t>(sizeof(vr)));
        }
        break;

    case CmdId::kSystemReset:
        SendAck(rcmd.seq);
        SetLcdAlert(AuditCode::kSystemReset, 0U);
        gSystemResetDueTick = xTaskGetTickCount() + kSystemResetDelayTicks;
        gSystemResetPending = true;
        break;

    case CmdId::kDetectionResult: {
        if (rcmd.payload_len != static_cast<std::uint8_t>(sizeof(PayloadDetectionResult))) {
            SendNack(rcmd.seq, ErrCode::kInvalidPayload);
            break;
        }
        const auto* pd =
            reinterpret_cast<const PayloadDetectionResult*>(rcmd.payload);
        gLastDetection.class_id       = pd->class_id;
        gLastDetection.confidence_pct = pd->confidence_pct;
        gLastDetection.valid          = true;
        gLcdDetectClass               = pd->class_id;
        gLcdDetectConf                = pd->confidence_pct;
        gLcdDetectSeen                = true;
        gDetectionLedOffMs = MillisecondsSinceBoot() + 300U;

        if (pd->class_id == kDetectionClassPerson &&
            pd->confidence_pct >= kDetectionThresholdPct)
        {
            if (!gVisionPersonActive) {
                SetLcdAlert(AuditCode::kVisionHit, pd->confidence_pct);
                gVisionPersonActive = true;
            }
            sm.ForceState(SystemState::Track, now_ms);
        }
        else if (pd->class_id == kDetectionClassNone ||
                 pd->confidence_pct < kDetectionThresholdPct)
        {
            gVisionPersonActive = false;
            sm.ForceState(SystemState::Idle, now_ms);
        }
        else
        {
            SendNack(rcmd.seq, ErrCode::kInvalidPayload);
            break;
        }

        SendAck(rcmd.seq);
        break;
    }

    case CmdId::kCreateTask: {
        if (rcmd.payload_len < static_cast<std::uint8_t>(sizeof(PayloadCreateTask))) {
            SendNack(rcmd.seq, ErrCode::kInvalidPayload);
            break;
        }
        const std::int8_t slot =
            CreateUserTask(rcmd.payload[0U], rcmd.payload[1U]);
        if (slot < 0) { SendNack(rcmd.seq, ErrCode::kBusy); break; }
        SendAck(rcmd.seq);
        SetLcdAlert(AuditCode::kTaskCreate, static_cast<std::uint16_t>(slot));
        QueueTaskList();
        break;
    }

    case CmdId::kDeleteTask: {
        if (rcmd.payload_len < static_cast<std::uint8_t>(sizeof(PayloadDeleteTask))) {
            SendNack(rcmd.seq, ErrCode::kInvalidPayload);
            break;
        }
        if (!DeleteUserTask(rcmd.payload[0U])) {
            SendNack(rcmd.seq, ErrCode::kInvalidCmd);
            break;
        }
        SendAck(rcmd.seq);
        SetLcdAlert(AuditCode::kTaskDelete, rcmd.payload[0U]);
        QueueTaskList();
        break;
    }

    default:
        SendNack(rcmd.seq, ErrCode::kInvalidCmd);
        break;
    }
}

// ---- Telemetry helpers ------------------------------------------------------

static void QueueTaskList() noexcept
{
    std::uint32_t total_rt = 0U;
    const UBaseType_t n = uxTaskGetSystemState(gTaskStatusBuf, kMaxTaskCount, &total_rt);
    const std::uint8_t count =
        static_cast<std::uint8_t>(n > kMaxTaskCount ? kMaxTaskCount : n);
    gLcdTaskCount = count;
    gLcdTaskListMs = MillisecondsSinceBoot();

    constexpr std::uint8_t kEntry =
        static_cast<std::uint8_t>(sizeof(PackedTaskEntry));
    std::uint8_t payload[1U + kMaxTaskCount * sizeof(PackedTaskEntry)] = {};
    payload[0] = count;

    for (std::uint8_t i = 0U; i < count; ++i) {
        const TaskStatus_t& src = gTaskStatusBuf[i];
        PackedTaskEntry entry   = {};

        for (std::uint8_t c = 0U; c < 7U && src.pcTaskName[c] != '\0'; ++c) {
            entry.name[c] = src.pcTaskName[c];
        }
        entry.state           = static_cast<std::uint8_t>(src.eCurrentState);
        entry.priority        = static_cast<std::uint8_t>(src.uxCurrentPriority);
        entry.stack_watermark = static_cast<std::uint16_t>(src.usStackHighWaterMark);
        if (total_rt > 100U) {
            const std::uint32_t pct = src.ulRunTimeCounter / (total_rt / 100U);
            entry.cpu_load = static_cast<std::uint8_t>(pct > 100U ? 100U : pct);
        }
        for (std::uint8_t s = 0U; s < kUserTaskSlots; ++s) {
            if (gUserTasks[s].in_use && gUserTasks[s].handle == src.xHandle) {
                entry.task_id = static_cast<std::uint8_t>(0x80U | s);
                break;
            }
        }

        const auto* eb = reinterpret_cast<const std::uint8_t*>(&entry);
        const std::uint8_t off = static_cast<std::uint8_t>(1U + i * kEntry);
        for (std::uint8_t b = 0U; b < kEntry; ++b) { payload[off + b] = eb[b]; }
    }

    const std::uint8_t plen = static_cast<std::uint8_t>(1U + count * kEntry);
    QueueTx(CmdId::kTaskList, payload, plen);
}

// Previous-sample accumulators for delta CPU-load calculation.
static std::uint32_t sPrevTotalRt = 0U;
static std::uint32_t sPrevIdleRt  = 0U;

static void QueueTelemetryTick(const StateMachine& sm) noexcept
{
    // CPU load: delta(used) / delta(total) over the last telemetry period.
    // Cumulative totals grow without bound → always use delta between two calls.
    std::uint32_t total_rt = 0U;
    const UBaseType_t n = uxTaskGetSystemState(gTaskStatusBuf, kMaxTaskCount, &total_rt);

    std::uint32_t idle_rt = 0U;
    for (UBaseType_t i = 0U; i < n; ++i) {
        const char* nm = gTaskStatusBuf[i].pcTaskName;
        if (nm[0]=='I' && nm[1]=='D' && nm[2]=='L' && nm[3]=='E' && nm[4]=='\0') {
            idle_rt = gTaskStatusBuf[i].ulRunTimeCounter;
            break;
        }
    }

    const std::uint32_t delta_total = total_rt - sPrevTotalRt;
    const std::uint32_t delta_idle  = idle_rt  - sPrevIdleRt;
    sPrevTotalRt = total_rt;
    sPrevIdleRt  = idle_rt;

    std::uint16_t cpu_load_x10 = 0U;
    if (delta_total > 100U) {
        const std::uint32_t idle_pct =
            (delta_idle >= delta_total) ? 100U : (delta_idle / (delta_total / 100U));
        const std::uint32_t used_pct = (idle_pct >= 100U) ? 0U : (100U - idle_pct);
        cpu_load_x10 = static_cast<std::uint16_t>(used_pct * 10U);
    }

    PayloadTelemetryTick p = {};
    p.state             = static_cast<std::uint8_t>(sm.state());
    p.cpu_load_x10      = cpu_load_x10;
    p.stack_uart_rx     = static_cast<std::uint16_t>(uxTaskGetStackHighWaterMark(gUartRxHandle));
    p.stack_state_core  = static_cast<std::uint16_t>(uxTaskGetStackHighWaterMark(gSMHandle));
    p.stack_tel_tx      = static_cast<std::uint16_t>(uxTaskGetStackHighWaterMark(gTxHandle));
    p.stack_heartbeat   = static_cast<std::uint16_t>(uxTaskGetStackHighWaterMark(gHbHandle));
    p.hb_miss_count     = FailSafeSupervisor::Instance().HeartbeatMissCount();
    gLcdState           = p.state;
    gLcdCpuLoadX10      = p.cpu_load_x10;
    gLcdHbMissCount     = p.hb_miss_count;
    gLcdUptimeMs        = MillisecondsSinceBoot();
    gLcdStackMin        = p.stack_uart_rx;
    if (p.stack_state_core < gLcdStackMin) { gLcdStackMin = p.stack_state_core; }
    if (p.stack_tel_tx < gLcdStackMin)     { gLcdStackMin = p.stack_tel_tx; }
    if (p.stack_heartbeat < gLcdStackMin)  { gLcdStackMin = p.stack_heartbeat; }
    QueueTx(CmdId::kTelemetryTick,
            reinterpret_cast<const std::uint8_t*>(&p),
            static_cast<std::uint8_t>(sizeof(p)));
}

// ---- Button edge callback (EXTI ISR context) --------------------------------

static void OnButtonEdge(void* ctx) noexcept
{
    const RawButtonEdge edge = {
        ReadButtonPressed() ? RawButtonEdgeType::Pressed
                            : RawButtonEdgeType::Released,
        MillisecondsSinceBoot()
    };
    BaseType_t woken = pdFALSE;
    (void)xQueueSendFromISR(static_cast<QueueHandle_t>(ctx), &edge, &woken);
    portYIELD_FROM_ISR(woken);
}

// ---- Tasks ------------------------------------------------------------------

void UartRxTask(void* /*ctx*/)
{
    std::uint32_t last_crc_errors = 0U;

    for (;;)
    {
        gUart.WaitForData();
        const std::uint8_t n = gUart.Read(
            gUartRxBuf, static_cast<std::uint8_t>(sizeof(gUartRxBuf)));
        for (std::uint8_t i = 0U; i < n; ++i) { gParser.Feed(gUartRxBuf[i]); }

        const std::uint32_t crc_now = gParser.CrcErrorCount();
        while (last_crc_errors < crc_now)
        {
            FailSafeSupervisor::Instance().OnCrcError();
            last_crc_errors = last_crc_errors + 1U;
        }
    }
}

void StateMachineTask(void* /*ctx*/)
{
    ButtonClassifier classifier;
    StateMachine     sm{MillisecondsSinceBoot()};
    SimulationEngine sim{kSimulationSeed};

    TickType_t next_sim_tick       = xTaskGetTickCount() + kSimulationPeriodTicks;
    TickType_t next_telemetry_tick = xTaskGetTickCount() + kTelemetryPeriodTicks;
    TickType_t next_tasklist_tick  = xTaskGetTickCount() + kTaskListPeriodTicks;
    bool fail_safe_announced = false;

    ApplyLedOutputs(sm.GetLedOutputs(MillisecondsSinceBoot()));
    QueueBootReport();
    SetLcdAlert(AuditCode::kBoot, 0U);

    for (;;)
    {
        Watchdog::Feed();

        if (FailSafeSupervisor::Instance().IsTriggered())
        {
            sm.ForceFailSafe(MillisecondsSinceBoot());
            if (!fail_safe_announced) {
                SetLcdAlert(AuditCode::kFailSafe, 0U);
                fail_safe_announced = true;
            }
        }
        else
        {
            fail_safe_announced = false;
        }

        RemoteCmd rcmd = {};
        while (xQueueReceive(gRemoteCmdQueue, &rcmd, 0U) == pdPASS)
        {
            DispatchRemoteCmd(sm, rcmd);
        }

        const TickType_t now_ticks = xTaskGetTickCount();
        const TickType_t wait =
            (now_ticks < next_sim_tick) ? (next_sim_tick - now_ticks) : 0U;

        RawButtonEdge edge = {};
        if (xQueueReceive(gButtonQueue, &edge, wait) == pdPASS)
        {
            const auto ev = classifier.OnEdge(edge);
            if (ev.has_value()) { (void)sm.Dispatch(*ev); }

            while (xQueueReceive(gButtonQueue, &edge, 0U) == pdPASS)
            {
                const auto ev2 = classifier.OnEdge(edge);
                if (ev2.has_value()) { (void)sm.Dispatch(*ev2); }
            }
        }

        TickType_t curr = xTaskGetTickCount();
        while (curr >= next_sim_tick)
        {
            const auto ts_ms = static_cast<std::uint32_t>(
                next_sim_tick * portTICK_PERIOD_MS);
            const auto ev = sim.Tick100ms(sm.state(), ts_ms);
            if (ev.has_value()) { (void)sm.Dispatch(*ev); }
            next_sim_tick = next_sim_tick + kSimulationPeriodTicks;
            curr = xTaskGetTickCount();
        }

        if (xTaskGetTickCount() >= next_telemetry_tick)
        {
            next_telemetry_tick = next_telemetry_tick + kTelemetryPeriodTicks;
            QueueTelemetryTick(sm);
        }

        if (xTaskGetTickCount() >= next_tasklist_tick)
        {
            next_tasklist_tick = next_tasklist_tick + kTaskListPeriodTicks;
            QueueTaskList();
        }

        const std::uint32_t now_led = MillisecondsSinceBoot();
        LedOutputs leds = sm.GetLedOutputs(now_led);
        leds.green_on  = leds.green_on
                         || (gGreenPulseOffMs != 0U && now_led < gGreenPulseOffMs);
        leds.blue_on   = (gVersionLedOffMs != 0U && now_led < gVersionLedOffMs)
                         || (gDetectionLedOffMs != 0U && now_led < gDetectionLedOffMs);
        leds.red_on    = leds.red_on
                         || (gRedPulseOffMs != 0U && now_led < gRedPulseOffMs);
        leds.yellow_on = (gManualLockLedOffMs != 0U && now_led < gManualLockLedOffMs)
                         || (gYellowPulseOffMs != 0U && now_led < gYellowPulseOffMs)
                         || static_cast<bool>(gUserBlinkState);
        ApplyLedOutputs(leds);

        if (gSystemResetPending && xTaskGetTickCount() >= gSystemResetDueTick)
        {
            NVIC_SystemReset();
        }
    }
}

void TelemetryTxTask(void* /*ctx*/)
{
    TxItem item = {};
    std::uint8_t encoded[kAC2MaxFrame] = {};

    for (;;)
    {
        if (xQueueReceive(gTxQueue, &item, portMAX_DELAY) == pdPASS)
        {
            const std::uint8_t flen =
                AC2Framer::EncodeTelemetry(item.cmd,
                                           item.payload,
                                           item.len,
                                           gTxSeq,
                                           encoded);
            gTxSeq = gTxSeq + 1U;
            (void)gUart.Write(encoded, flen);
        }
    }
}

void HeartbeatTask(void* /*ctx*/)
{
    TickType_t last_wake = xTaskGetTickCount();

    for (;;)
    {
        vTaskDelayUntil(&last_wake, kHeartbeatPeriodTicks);

        const auto now_ms = MillisecondsSinceBoot();
        FailSafeSupervisor::Instance().CheckHeartbeatTimeout(now_ms);

        PayloadHeartbeatTx hb = {};
        hb.uptime_ms = now_ms;
        QueueTx(CmdId::kHeartbeat,
                reinterpret_cast<const std::uint8_t*>(&hb),
                static_cast<std::uint8_t>(sizeof(hb)));
    }
}

} // namespace

extern "C" int main()
{
    gBootResetReasonBits = RCC->CSR;
    __HAL_RCC_CLEAR_RESET_FLAGS();

    aegis::edge::MPU_Init();
    aegis::edge::InitializePlatform();

    (void)aegis::edge::FailSafeSupervisor::Instance();

    aegis::edge::Watchdog::Init();

    if (!aegis::edge::POST_Run())
    {
        aegis::edge::FailSafeSupervisor::Instance().ReportEvent(
            aegis::edge::FailSafeEvent::PostFailure);
        while (true) { }
    }

    if (!gUart.Init()) { while (true) { } }
    gParser.SetCallback(OnAC2Frame, nullptr);

    if (!aegis::edge::InitializeRangeSensor()) { while (true) { } }
    if (!aegis::edge::InitializeServoPwm()) { while (true) { } }

    gButtonQueue = xQueueCreateStatic(
        kButtonQueueLen,
        sizeof(aegis::edge::RawButtonEdge),
        reinterpret_cast<std::uint8_t*>(gButtonQueueStorage.data()),
        &gButtonQueueCB);
    configASSERT(gButtonQueue != nullptr);

    gTxQueue = xQueueCreateStatic(
        kTxQueueLen,
        sizeof(TxItem),
        reinterpret_cast<std::uint8_t*>(gTxQueueStorage.data()),
        &gTxQueueCB);
    configASSERT(gTxQueue != nullptr);

    gRemoteCmdQueue = xQueueCreateStatic(
        kRemoteCmdQueueLen,
        sizeof(RemoteCmd),
        reinterpret_cast<std::uint8_t*>(gRemoteCmdQueueStorage.data()),
        &gRemoteCmdQueueCB);
    configASSERT(gRemoteCmdQueue != nullptr);

    aegis::edge::SetButtonEdgeCallback(OnButtonEdge, gButtonQueue);

    gUartRxHandle = xTaskCreateStatic(UartRxTask, "UartRx",
        kStackUartRx, nullptr, kPrioUartRx, gUartRxStack, &gUartRxTCB);
    configASSERT(gUartRxHandle != nullptr);

    gSMHandle = xTaskCreateStatic(StateMachineTask, "StateCore",
        kStackStateMachine, nullptr, kPrioStateMachine, gSMStack, &gSMTaskTCB);
    configASSERT(gSMHandle != nullptr);

    gTxHandle = xTaskCreateStatic(TelemetryTxTask, "TelTx",
        kStackTelemetryTx, nullptr, kPrioTelemetryTx, gTxStack, &gTxTaskTCB);
    configASSERT(gTxHandle != nullptr);

    gHbHandle = xTaskCreateStatic(HeartbeatTask, "Heartbeat",
        kStackHeartbeat, nullptr, kPrioHeartbeat, gHbStack, &gHbTaskTCB);
    configASSERT(gHbHandle != nullptr);

    vTaskStartScheduler();

    taskDISABLE_INTERRUPTS();
    while (true) { }
}
