#include "runtime/edge_runtime.hpp"

#include "fail_safe_supervisor.hpp"
#include "platform_io.hpp"
#include "state_machine.hpp"
#include "version.hpp"

namespace aegis::edge {

void DispatchRemoteCmd(StateMachine& sm, const RemoteCmd& rcmd) noexcept
{
    const auto now_ms = MillisecondsSinceBoot();

    switch (rcmd.cmd) {
        case CmdId::kManualLock:
            gManualLockLedOffMs = MillisecondsSinceBoot() + 1000U;
            sm.ForceFailSafe(now_ms);
            FailSafeSupervisor::Instance().ReportEvent(FailSafeEvent::ExternalTrigger);
            SetLcdAlert(AuditCode::kFailSafe, 0U);
            SendAck(rcmd.seq);
            break;

        case CmdId::kGetVersion: {
            gVersionLedOffMs = MillisecondsSinceBoot() + 1000U;

            PayloadVersionReport vr = {};
            vr.major = kVersionMajor;
            vr.minor = kVersionMinor;
            vr.patch = kVersionPatch;
            vr.build_ts = kBuildTimestamp;
            for (std::uint8_t i = 0U; i < sizeof(vr.git_sha) && kGitSha[i] != '\0'; ++i) {
                vr.git_sha[i] = static_cast<std::uint8_t>(kGitSha[i]);
            }
            QueueTx(CmdId::kVersionReport,
                    reinterpret_cast<const std::uint8_t*>(&vr),
                    static_cast<std::uint8_t>(sizeof(vr)));
            break;
        }

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

            const auto* pd = reinterpret_cast<const PayloadDetectionResult*>(rcmd.payload);
            gLastDetection.class_id = pd->class_id;
            gLastDetection.confidence_pct = pd->confidence_pct;
            gLastDetection.valid = true;
            gLcdDetectClass = pd->class_id;
            gLcdDetectConf = pd->confidence_pct;
            gLcdDetectSeen = true;
            gDetectionLedOffMs = MillisecondsSinceBoot() + 300U;

            if (pd->class_id == kDetectionClassPerson &&
                pd->confidence_pct >= kDetectionThresholdPct) {
                if (!gVisionPersonActive) {
                    SetLcdAlert(AuditCode::kVisionHit, pd->confidence_pct);
                    gVisionPersonActive = true;
                }
                sm.ForceState(SystemState::Track, now_ms);
            } else if (pd->class_id == kDetectionClassNone ||
                       pd->confidence_pct < kDetectionThresholdPct) {
                gVisionPersonActive = false;
                sm.ForceState(SystemState::Idle, now_ms);
            } else {
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
            const std::int8_t slot = CreateUserTask(rcmd.payload[0U], rcmd.payload[1U]);
            if (slot < 0) {
                SendNack(rcmd.seq, ErrCode::kBusy);
                break;
            }
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

} // namespace aegis::edge
