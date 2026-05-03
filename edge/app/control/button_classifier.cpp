#include "button_classifier.hpp"

namespace aegis::edge {

std::optional<Event> ButtonClassifier::OnEdge(const RawButtonEdge& edge)
{
    if (has_last_edge_)
    {
        const std::uint32_t elapsed_ms = edge.timestamp_ms - last_edge_timestamp_ms_;
        if (elapsed_ms < kDebounceMs)
        {
            return std::nullopt;
        }
    }

    has_last_edge_ = true;
    last_edge_timestamp_ms_ = edge.timestamp_ms;

    if (edge.type == RawButtonEdgeType::Pressed)
    {
        if (is_pressed_)
        {
            return std::nullopt;
        }

        is_pressed_ = true;
        press_started_ms_ = edge.timestamp_ms;
        return std::nullopt;
    }

    if (!is_pressed_)
    {
        return std::nullopt;
    }

    is_pressed_ = false;

    const std::uint32_t hold_time_ms = edge.timestamp_ms - press_started_ms_;
    const EventType event_type =
        (hold_time_ms >= kLongPressThresholdMs) ? EventType::ButtonLongPress
                                                : EventType::ButtonShortPress;

    return Event{event_type, edge.timestamp_ms};
}

} // namespace aegis::edge
