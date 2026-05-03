#include "runtime/edge_runtime.hpp"

#include "FreeRTOS.h"
#include "task.h"

#include "fail_safe_supervisor.hpp"
#include "hmac_sha256.hpp"
#include "mpu_config.hpp"
#include "platform_io.hpp"
#include "post.hpp"
#include "watchdog.hpp"

#include "stm32f4xx_hal.h"

namespace aegis::edge {
namespace {

constexpr UBaseType_t kPrioUartRx       = 5U;
constexpr UBaseType_t kPrioStateMachine = 4U;
constexpr UBaseType_t kPrioTelemetryTx  = 3U;
constexpr UBaseType_t kPrioHeartbeat    = 2U;

constexpr std::uint32_t kStackUartRx       = 512U;
constexpr std::uint32_t kStackStateMachine = 512U;
constexpr std::uint32_t kStackTelemetryTx  = 512U;
constexpr std::uint32_t kStackHeartbeat    = 256U;

constexpr std::uint8_t kPsk[] = {
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF
};

StaticTask_t gUartRxTCB;
StaticTask_t gSMTaskTCB;
StaticTask_t gTxTaskTCB;
StaticTask_t gHbTaskTCB;

StackType_t gUartRxStack[kStackUartRx];
StackType_t gSMStack[kStackStateMachine];
StackType_t gTxStack[kStackTelemetryTx];
StackType_t gHbStack[kStackHeartbeat];

StaticQueue_t gButtonQueueCB;
StaticQueue_t gJoystickQueueCB;
StaticQueue_t gTxQueueCB;
StaticQueue_t gRemoteCmdQueueCB;

std::array<RawButtonEdge, kButtonQueueLen> gButtonQueueStorage = {};
std::array<std::uint32_t, kJoystickQueueLen> gJoystickQueueStorage = {};
std::array<TxItem, kTxQueueLen> gTxQueueStorage = {};
std::array<RemoteCmd, kRemoteCmdQueueLen> gRemoteCmdQueueStorage = {};

void QueueAuditEvent(std::uint8_t event_code, std::uint16_t count) noexcept
{
    PayloadAuditEvent p = {};
    p.event_code = event_code;
    p.count = count;
    QueueTx(CmdId::kAuditEvent,
            reinterpret_cast<const std::uint8_t*>(&p),
            static_cast<std::uint8_t>(sizeof(p)));
}

void PulseFeedback(std::uint8_t event_code) noexcept
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
        case AuditCode::kJoystickPress:
            gGreenPulseOffMs = until_ms;
            gYellowPulseOffMs = until_ms;
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

void OnAC2Frame(const AC2Frame& frame, void* /*ctx*/) noexcept
{
    const auto now_ms = MillisecondsSinceBoot();

    std::uint8_t hmac_in[1U + kAC2MaxPayload] = {};
    hmac_in[0U] = frame.cmd;
    for (std::uint8_t i = 0U; i < frame.payload_len; ++i) {
        hmac_in[1U + i] = frame.payload[i];
    }

    if (!HMAC_SHA256_Verify(kPsk,
                             static_cast<std::uint8_t>(sizeof(kPsk)),
                             hmac_in,
                             static_cast<std::uint16_t>(1U + frame.payload_len),
                             frame.hmac,
                             kHmacTruncLen)) {
        FailSafeSupervisor::Instance().OnHmacFailure();
        SendNack(frame.seq, ErrCode::kAuthFail);
        return;
    }

    if (!gReplay.Check(frame.seq)) {
        SendNack(frame.seq, ErrCode::kReplay);
        return;
    }

    if (frame.cmd == CmdId::kHeartbeat) {
        FailSafeSupervisor::Instance().OnGatewayHeartbeatReceived(now_ms);
        return;
    }

    if (FailSafeSupervisor::Instance().IsTriggered() && frame.cmd != CmdId::kSystemReset) {
        SendNack(frame.seq, ErrCode::kFailSafeLock);
        return;
    }

    if (frame.cmd == CmdId::kManualLock && !gRateLimiter.Allow(frame.cmd, now_ms)) {
        SendNack(frame.seq, ErrCode::kRateLimited);
        return;
    }

    RemoteCmd rcmd = {};
    rcmd.cmd = frame.cmd;
    rcmd.seq = frame.seq;
    rcmd.payload_len = frame.payload_len;
    for (std::uint8_t i = 0U; i < frame.payload_len; ++i) {
        rcmd.payload[i] = frame.payload[i];
    }
    (void)xQueueSend(gRemoteCmdQueue, &rcmd, 0U);
}

} // namespace

UserTaskSlot gUserTasks[kUserTaskSlots] = {};
TaskStatus_t gTaskStatusBuf[kMaxTaskCount] = {};

TaskHandle_t gUartRxHandle = nullptr;
TaskHandle_t gSMHandle = nullptr;
TaskHandle_t gTxHandle = nullptr;
TaskHandle_t gHbHandle = nullptr;

QueueHandle_t gButtonQueue = nullptr;
QueueHandle_t gJoystickQueue = nullptr;
QueueHandle_t gTxQueue = nullptr;
QueueHandle_t gRemoteCmdQueue = nullptr;

UartDriver gUart;
AC2Parser gParser;
ReplayGuard gReplay;
RateLimiter gRateLimiter;
std::uint32_t gTxSeq = 0U;
std::uint8_t gUartRxBuf[kAC2MaxFrame] = {};

std::uint32_t gVersionLedOffMs = 0U;
std::uint32_t gDetectionLedOffMs = 0U;
std::uint32_t gGreenPulseOffMs = 0U;
std::uint32_t gYellowPulseOffMs = 0U;
std::uint32_t gRedPulseOffMs = 0U;
std::uint32_t gManualLockLedOffMs = 0U;

LastDetection gLastDetection = {};
bool gVisionPersonActive = false;

volatile std::uint8_t  gLcdState = 0U;
volatile std::uint16_t gLcdCpuLoadX10 = 0U;
volatile std::uint8_t  gLcdHbMissCount = 0U;
volatile std::uint8_t  gLcdTaskCount = 0U;
volatile std::uint32_t gLcdUptimeMs = 0U;
volatile std::uint32_t gLcdTaskListMs = 0U;
volatile std::uint16_t gLcdStackMin = 0U;
volatile std::uint8_t  gLcdRangeAngle = 0U;
volatile std::uint16_t gLcdRangeDistCm = 0U;
volatile std::uint16_t gLcdRangeThresh = 0U;
volatile bool          gLcdRangeValid = false;
volatile bool          gLcdRangeLocked = false;
volatile bool          gLcdRangeSeen = false;
volatile bool          gLcdRangeActive = false;
volatile std::uint8_t  gLcdDetectClass = 0U;
volatile std::uint8_t  gLcdDetectConf = 0U;
volatile bool          gLcdDetectSeen = false;
volatile std::uint8_t  gLcdAlertCode = 0U;
volatile std::uint16_t gLcdAlertValue = 0U;
volatile TickType_t    gLcdAlertUntilTick = 0U;
volatile bool          gSystemResetPending = false;
volatile TickType_t    gSystemResetDueTick = 0U;
std::uint32_t          gBootResetReasonBits = 0U;
volatile bool          gUserBlinkState = false;

void QueueTx(std::uint8_t cmd, const std::uint8_t* payload, std::uint8_t len) noexcept
{
    TxItem item = {};
    item.cmd = cmd;
    item.len = (len <= kAC2MaxPayload) ? len : kAC2MaxPayload;
    for (std::uint8_t i = 0U; i < item.len; ++i) {
        item.payload[i] = payload[i];
    }
    (void)xQueueSend(gTxQueue, &item, 0U);
}

void SendAck(std::uint32_t echoed_seq) noexcept
{
    PayloadAck p = {};
    p.echoed_seq = echoed_seq;
    QueueTx(CmdId::kAck,
            reinterpret_cast<const std::uint8_t*>(&p),
            static_cast<std::uint8_t>(sizeof(p)));
}

void SendNack(std::uint32_t echoed_seq, std::uint8_t err) noexcept
{
    PayloadNack p = {};
    p.echoed_seq = echoed_seq;
    p.err_code = err;
    QueueTx(CmdId::kNack,
            reinterpret_cast<const std::uint8_t*>(&p),
            static_cast<std::uint8_t>(sizeof(p)));
}

void SetLcdAlert(std::uint8_t event_code, std::uint16_t value) noexcept
{
    gLcdAlertCode = event_code;
    gLcdAlertValue = value;
    gLcdAlertUntilTick = xTaskGetTickCount() + kLcdAlertDurationTicks;
    PulseFeedback(event_code);
    QueueAuditEvent(event_code, value);
}

void QueueBootReport() noexcept
{
    PayloadBootReport p = {};
    p.reset_reason_bits = gBootResetReasonBits;
    QueueTx(CmdId::kBootReport,
            reinterpret_cast<const std::uint8_t*>(&p),
            static_cast<std::uint8_t>(sizeof(p)));
}

void QueueRangeScanReport(std::uint8_t angle_deg,
                          std::uint16_t distance_cm,
                          bool measurement_valid,
                          bool locked,
                          std::uint16_t threshold_cm) noexcept
{
    gLcdRangeAngle = angle_deg;
    gLcdRangeDistCm = distance_cm;
    gLcdRangeThresh = threshold_cm;
    gLcdRangeValid = measurement_valid;
    gLcdRangeLocked = locked;
    gLcdRangeSeen = true;

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

void RunEdgeFirmware() noexcept
{
    gBootResetReasonBits = RCC->CSR;
    __HAL_RCC_CLEAR_RESET_FLAGS();

    MPU_Init();
    InitializePlatform();

    (void)FailSafeSupervisor::Instance();
    Watchdog::Init();

    if (!POST_Run()) {
        FailSafeSupervisor::Instance().ReportEvent(FailSafeEvent::PostFailure);
        while (true) { }
    }

    if (!gUart.Init()) {
        while (true) { }
    }
    gParser.SetCallback(OnAC2Frame, nullptr);

    if (!InitializeRangeSensor()) {
        while (true) { }
    }
    if (!InitializeServoPwm()) {
        while (true) { }
    }

    gButtonQueue = xQueueCreateStatic(
        kButtonQueueLen,
        sizeof(RawButtonEdge),
        reinterpret_cast<std::uint8_t*>(gButtonQueueStorage.data()),
        &gButtonQueueCB);
    configASSERT(gButtonQueue != nullptr);

    gJoystickQueue = xQueueCreateStatic(
        kJoystickQueueLen,
        sizeof(std::uint32_t),
        reinterpret_cast<std::uint8_t*>(gJoystickQueueStorage.data()),
        &gJoystickQueueCB);
    configASSERT(gJoystickQueue != nullptr);

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

    RegisterInputCallbacks();

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

} // namespace aegis::edge
