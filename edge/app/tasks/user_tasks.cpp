#include "runtime/edge_runtime.hpp"

#include "lcd_driver.hpp"
#include "platform_io.hpp"

namespace aegis::edge {

void UserBlinkTask(void* ctx) noexcept
{
    const auto* slot = static_cast<const UserTaskSlot*>(ctx);
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
    gLcdRangeSeen = false;
    gLcdRangeValid = false;
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

    for (;;) {
        if (locked) {
            SetServoAngleDegrees(locked_angle);

            std::uint16_t distance_cm = 0U;
            const bool valid = MeasureRangeCm(distance_cm);
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

        SetServoAngleDegrees(angle);
        vTaskDelay(kStepDelay);
        step_count = static_cast<std::uint8_t>(step_count + 1U);
        bool report_valid = false;
        std::uint16_t report_distance_cm = 0U;

        if (step_count >= kMeasureEverySteps) {
            step_count = 0U;
            std::uint16_t distance_cm = 0U;
            const bool valid = MeasureRangeCm(distance_cm);
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

std::int8_t CreateUserTask(std::uint8_t type, std::uint8_t param) noexcept
{
    for (std::uint8_t i = 0U; i < kUserTaskSlots; ++i) {
        if (gUserTasks[i].in_use) {
            continue;
        }

        TaskFunction_t fn = UserBlinkTask;
        char tname[configMAX_TASK_NAME_LEN] = {};

        switch (static_cast<UserTaskType>(type)) {
            case UserTaskType::Blink:
                fn = UserBlinkTask;
                tname[0] = 'B'; tname[1] = 'l'; tname[2] = 'n'; tname[3] = 'k';
                tname[4] = static_cast<char>('0' + i);
                break;
            case UserTaskType::RangeScan:
                fn = UserRangeScanTask;
                tname[0] = 'R'; tname[1] = 'n'; tname[2] = 'g'; tname[3] = 'S';
                tname[4] = static_cast<char>('0' + i);
                break;
            case UserTaskType::LcdStatus:
                fn = UserLcdStatusTask;
                tname[0] = 'L'; tname[1] = 'C'; tname[2] = 'D';
                tname[3] = static_cast<char>('0' + i);
                break;
            default:
                return -1;
        }

        gUserTasks[i].task_type = static_cast<UserTaskType>(type);
        gUserTasks[i].param = param;

        const std::uint32_t depth =
            static_cast<std::uint32_t>(sizeof(gUserTasks[i].stack) / sizeof(StackType_t));
        gUserTasks[i].handle = xTaskCreateStatic(
            fn, tname, depth, &gUserTasks[i], 1U,
            gUserTasks[i].stack, &gUserTasks[i].tcb);

        if (gUserTasks[i].handle == nullptr) {
            return -1;
        }
        gUserTasks[i].in_use = true;
        return static_cast<std::int8_t>(i);
    }
    return -1;
}

bool DeleteUserTask(std::uint8_t slot_index) noexcept
{
    if (slot_index >= kUserTaskSlots) {
        return false;
    }
    if (!gUserTasks[slot_index].in_use) {
        return false;
    }

    vTaskDelete(gUserTasks[slot_index].handle);
    gUserTasks[slot_index].handle = nullptr;
    gUserTasks[slot_index].in_use = false;

    if (gUserTasks[slot_index].task_type == UserTaskType::Blink) {
        bool any_blink = false;
        for (std::uint8_t s = 0U; s < kUserTaskSlots; ++s) {
            if (gUserTasks[s].in_use && gUserTasks[s].task_type == UserTaskType::Blink) {
                any_blink = true;
                break;
            }
        }
        if (!any_blink) {
            gUserBlinkState = false;
        }
    }

    if (gUserTasks[slot_index].task_type == UserTaskType::RangeScan) {
        SetServoAngleDegrees(90U);

        bool any_range_scan = false;
        for (std::uint8_t s = 0U; s < kUserTaskSlots; ++s) {
            if (gUserTasks[s].in_use && gUserTasks[s].task_type == UserTaskType::RangeScan) {
                any_range_scan = true;
                break;
            }
        }

        if (!any_range_scan) {
            gLcdRangeActive = false;
            gLcdRangeSeen = false;
            gLcdRangeValid = false;
            gLcdRangeLocked = false;
            gLcdRangeAngle = 0U;
            gLcdRangeDistCm = 0U;
            gLcdRangeThresh = 0U;
        }
    }

    if (gUserTasks[slot_index].task_type == UserTaskType::LcdStatus) {
        LcdClear();
    }

    return true;
}

} // namespace aegis::edge
