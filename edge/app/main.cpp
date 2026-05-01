#include <array>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "ac2_framer.hpp"
#include "button_classifier.hpp"
#include "fail_safe_supervisor.hpp"
#include "hmac_sha256.hpp"
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

// ---- Static task allocations ------------------------------------------------
StaticTask_t gUartRxTCB, gSMTaskTCB, gTxTaskTCB, gHbTaskTCB;
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

// Yellow LED off time — set when ManualLock arrives, cleared by StateMachineTask.
std::uint32_t gManualLockLedOffMs = 0U;

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

    // SAF-02: All other commands blocked in fail-safe.
    if (FailSafeSupervisor::Instance().IsTriggered())
    {
        SendNack(frame.seq, ErrCode::kFailSafeLock);
        return;
    }

    // SR-05: Rate limiting.
    if ((frame.cmd == CmdId::kSetState || frame.cmd == CmdId::kManualLock) &&
        !gRateLimiter.Allow(frame.cmd, now_ms))
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

// ---- Remote-command dispatcher (StateMachineTask context) -------------------

static void DispatchRemoteCmd(StateMachine& sm,
                               const RemoteCmd& rcmd) noexcept
{
    const auto now_ms = MillisecondsSinceBoot();

    switch (rcmd.cmd)
    {
    case CmdId::kSetState:
        if (rcmd.payload_len < 1U)
        {
            SendNack(rcmd.seq, ErrCode::kInvalidPayload);
            break;
        }
        {
            const auto target = static_cast<SystemState>(rcmd.payload[0U]);
            if (target == SystemState::FailSafe)
            {
                sm.ForceFailSafe(now_ms);
                FailSafeSupervisor::Instance().ReportEvent(
                    FailSafeEvent::ExternalTrigger);
            }
            else if (!FailSafeSupervisor::Instance().IsTriggered())
            {
                sm.ForceState(target, now_ms);
            }
            else
            {
                SendNack(rcmd.seq, ErrCode::kFailSafeLock);
                break;
            }
            SendAck(rcmd.seq);
        }
        break;

    case CmdId::kManualLock:
        gManualLockLedOffMs = MillisecondsSinceBoot() + 1000U;
        sm.ForceFailSafe(now_ms);
        FailSafeSupervisor::Instance().ReportEvent(FailSafeEvent::ExternalTrigger);
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

    default:
        SendNack(rcmd.seq, ErrCode::kInvalidCmd);
        break;
    }
}

// ---- Telemetry helpers ------------------------------------------------------

static void QueueTelemetryTick(const StateMachine& sm) noexcept
{
    PayloadTelemetryTick p = {};
    p.state                = static_cast<std::uint8_t>(sm.state());
    p.cpu_load_x10         = 0U;
    p.free_stack_min_words = static_cast<std::uint16_t>(
        uxTaskGetStackHighWaterMark(nullptr));
    p.hb_miss_count        =
        FailSafeSupervisor::Instance().HeartbeatMissCount();
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

    ApplyLedOutputs(sm.GetLedOutputs(MillisecondsSinceBoot()));

    for (;;)
    {
        Watchdog::Feed();

        if (FailSafeSupervisor::Instance().IsTriggered())
        {
            sm.ForceFailSafe(MillisecondsSinceBoot());
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

        const std::uint32_t now_led = MillisecondsSinceBoot();
        LedOutputs leds = sm.GetLedOutputs(now_led);
        leds.blue_on = (gVersionLedOffMs != 0U && now_led < gVersionLedOffMs);
        leds.yellow_on = (gManualLockLedOffMs != 0U && now_led < gManualLockLedOffMs);
        ApplyLedOutputs(leds);
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

    configASSERT(xTaskCreateStatic(UartRxTask, "UartRx",
        kStackUartRx, nullptr, kPrioUartRx,
        gUartRxStack, &gUartRxTCB) != nullptr);

    configASSERT(xTaskCreateStatic(StateMachineTask, "StateCore",
        kStackStateMachine, nullptr, kPrioStateMachine,
        gSMStack, &gSMTaskTCB) != nullptr);

    configASSERT(xTaskCreateStatic(TelemetryTxTask, "TelTx",
        kStackTelemetryTx, nullptr, kPrioTelemetryTx,
        gTxStack, &gTxTaskTCB) != nullptr);

    configASSERT(xTaskCreateStatic(HeartbeatTask, "Heartbeat",
        kStackHeartbeat, nullptr, kPrioHeartbeat,
        gHbStack, &gHbTaskTCB) != nullptr);

    vTaskStartScheduler();

    taskDISABLE_INTERRUPTS();
    while (true) { }
}
