#ifndef AEGIS_EDGE_BUTTON_CLASSIFIER_HPP
#define AEGIS_EDGE_BUTTON_CLASSIFIER_HPP

#include <optional>

#include "domain.hpp"

namespace aegis::edge {

class ButtonClassifier
{
public:
    [[nodiscard]] std::optional<Event> OnEdge(const RawButtonEdge& edge);

private:
    static constexpr std::uint32_t kDebounceMs = 20U;
    static constexpr std::uint32_t kLongPressThresholdMs = 500U;

    bool has_last_edge_ = false;
    bool is_pressed_ = false;
    std::uint32_t last_edge_timestamp_ms_ = 0U;
    std::uint32_t press_started_ms_ = 0U;
};

} // namespace aegis::edge

#endif
