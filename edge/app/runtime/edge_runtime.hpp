#ifndef AEGIS_EDGE_RUNTIME_HPP
#define AEGIS_EDGE_RUNTIME_HPP

#include <array>
#include <cstdint>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "ac2_framer.hpp"
#include "button_classifier.hpp"
#include "rate_limiter.hpp"
#include "replay_guard.hpp"
#include "telemetry.hpp"
#include "uart_driver.hpp"

namespace aegis::edge {

inline constexpr std::uint8_t kUserTaskSlots = 4U;
inline constexpr UBaseType_t  kMaxTaskCount  = 9U;

inline constexpr std::uint32_t kButtonQueueLen    = 8U;
inline constexpr std::uint32_t kJoystickQueueLen  = 4U;
inline constexpr std::uint32_t kTxQueueLen        = 4U;
inline constexpr std::uint32_t kRemoteCmdQueueLen = 4U;

inline constexpr std::uint32_t kSimulationSeed        = 0x12345678U;
inline constexpr TickType_t    kSimulationPeriodTicks = pdMS_TO_TICKS(100U);
inline constexpr TickType_t    kHeartbeatPeriodTicks  = pdMS_TO_TICKS(1000U);
inline constexpr TickType_t    kTelemetryPeriodTicks  = pdMS_TO_TICKS(1000U);
inline constexpr TickType_t    kTaskListPeriodTicks   = pdMS_TO_TICKS(2000U);
inline constexpr TickType_t    kSystemResetDelayTicks = pdMS_TO_TICKS(250U);
inline constexpr TickType_t    kLcdAlertDurationTicks = pdMS_TO_TICKS(3000U);

inline constexpr std::uint8_t kDetectionClassNone    = 0U;
inline constexpr std::uint8_t kDetectionClassPerson  = 1U;
inline constexpr std::uint8_t kDetectionThresholdPct = 50U;

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

enum class UserTaskType : std::uint8_t {
    Blink = 0U,
    RangeScan = 3U,
    LcdStatus = 4U
};

struct UserTaskSlot
{
    StaticTask_t  tcb;
    StackType_t   stack[384U];
    TaskHandle_t  handle;
    UserTaskType  task_type;
    std::uint8_t  param;
    bool          in_use;
};

struct LastDetection
{
    std::uint8_t class_id = 0U;
    std::uint8_t confidence_pct = 0U;
    bool valid = false;
};

static_assert(1U + kMaxTaskCount * sizeof(PackedTaskEntry) <= kAC2MaxPayload,
              "task list payload overflows AC2 max payload");

extern UserTaskSlot gUserTasks[kUserTaskSlots];
extern TaskStatus_t gTaskStatusBuf[kMaxTaskCount];

extern TaskHandle_t gUartRxHandle;
extern TaskHandle_t gSMHandle;
extern TaskHandle_t gTxHandle;
extern TaskHandle_t gHbHandle;

extern QueueHandle_t gButtonQueue;
extern QueueHandle_t gJoystickQueue;
extern QueueHandle_t gTxQueue;
extern QueueHandle_t gRemoteCmdQueue;

extern UartDriver  gUart;
extern AC2Parser   gParser;
extern ReplayGuard gReplay;
extern RateLimiter gRateLimiter;
extern std::uint32_t gTxSeq;
extern std::uint8_t gUartRxBuf[kAC2MaxFrame];

extern std::uint32_t gVersionLedOffMs;
extern std::uint32_t gDetectionLedOffMs;
extern std::uint32_t gGreenPulseOffMs;
extern std::uint32_t gYellowPulseOffMs;
extern std::uint32_t gRedPulseOffMs;
extern std::uint32_t gManualLockLedOffMs;

extern LastDetection gLastDetection;
extern bool gVisionPersonActive;

extern volatile std::uint8_t  gLcdState;
extern volatile std::uint16_t gLcdCpuLoadX10;
extern volatile std::uint8_t  gLcdHbMissCount;
extern volatile std::uint8_t  gLcdTaskCount;
extern volatile std::uint32_t gLcdUptimeMs;
extern volatile std::uint32_t gLcdTaskListMs;
extern volatile std::uint16_t gLcdStackMin;
extern volatile std::uint8_t  gLcdRangeAngle;
extern volatile std::uint16_t gLcdRangeDistCm;
extern volatile std::uint16_t gLcdRangeThresh;
extern volatile bool          gLcdRangeValid;
extern volatile bool          gLcdRangeLocked;
extern volatile bool          gLcdRangeSeen;
extern volatile bool          gLcdRangeActive;
extern volatile std::uint8_t  gLcdDetectClass;
extern volatile std::uint8_t  gLcdDetectConf;
extern volatile bool          gLcdDetectSeen;
extern volatile std::uint8_t  gLcdAlertCode;
extern volatile std::uint16_t gLcdAlertValue;
extern volatile TickType_t    gLcdAlertUntilTick;
extern volatile bool          gSystemResetPending;
extern volatile TickType_t    gSystemResetDueTick;
extern std::uint32_t          gBootResetReasonBits;
extern volatile bool          gUserBlinkState;

void RunEdgeFirmware() noexcept;

void QueueTx(std::uint8_t cmd, const std::uint8_t* payload, std::uint8_t len) noexcept;
void SendAck(std::uint32_t echoed_seq) noexcept;
void SendNack(std::uint32_t echoed_seq, std::uint8_t err) noexcept;
void SetLcdAlert(std::uint8_t event_code, std::uint16_t value) noexcept;
void QueueBootReport() noexcept;
void QueueRangeScanReport(std::uint8_t angle_deg,
                          std::uint16_t distance_cm,
                          bool measurement_valid,
                          bool locked,
                          std::uint16_t threshold_cm) noexcept;

std::int8_t CreateUserTask(std::uint8_t type, std::uint8_t param) noexcept;
bool DeleteUserTask(std::uint8_t slot_index) noexcept;
void DispatchRemoteCmd(class StateMachine& sm, const RemoteCmd& rcmd) noexcept;

void QueueTaskList() noexcept;
void QueueTelemetryTick(const class StateMachine& sm) noexcept;

void UserBlinkTask(void* ctx) noexcept;
void UserRangeScanTask(void* ctx) noexcept;
void UserLcdStatusTask(void* ctx) noexcept;

void UartRxTask(void* ctx);
void StateMachineTask(void* ctx);
void TelemetryTxTask(void* ctx);
void HeartbeatTask(void* ctx);

void RegisterInputCallbacks() noexcept;
void DrainJoystickEvents(std::uint32_t& last_press_ms,
                         std::uint16_t& press_count) noexcept;

} // namespace aegis::edge

#endif
