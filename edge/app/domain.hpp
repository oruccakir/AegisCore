#ifndef AEGIS_EDGE_DOMAIN_HPP
#define AEGIS_EDGE_DOMAIN_HPP

#include <cstdint>

namespace aegis::edge {

enum class SystemState : std::uint8_t
{
    Idle,
    Search,
    Track,
    FailSafe
};

enum class EventType : std::uint8_t
{
    ButtonShortPress,
    ButtonLongPress,
    SimTargetDetected,
    SimTargetLost
};

struct Event
{
    EventType type;
    std::uint32_t timestamp_ms;
};

struct LedOutputs
{
    bool green_on;
    bool red_on;
    bool blue_on = false;
    bool yellow_on = false;
};

enum class RawButtonEdgeType : std::uint8_t
{
    Pressed,
    Released
};

struct RawButtonEdge
{
    RawButtonEdgeType type;
    std::uint32_t timestamp_ms;
};

} // namespace aegis::edge

#endif
