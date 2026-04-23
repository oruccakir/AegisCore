#ifndef AEGIS_EDGE_VERSION_HPP
#define AEGIS_EDGE_VERSION_HPP

#include <cstdint>

namespace aegis::edge {

inline constexpr std::uint8_t kVersionMajor = 1U;
inline constexpr std::uint8_t kVersionMinor = 0U;
inline constexpr std::uint8_t kVersionPatch = 0U;

// Git SHA injected by CMake via -DAEGIS_GIT_SHA="<sha>".
// Falls back to a placeholder when not set.
#ifndef AEGIS_GIT_SHA
#define AEGIS_GIT_SHA "00000000"
#endif

// Build timestamp (Unix epoch) injected by CMake via -DAEGIS_BUILD_TS=<ts>.
#ifndef AEGIS_BUILD_TS
#define AEGIS_BUILD_TS 0U
#endif

inline constexpr char kGitSha[] = AEGIS_GIT_SHA;
inline constexpr std::uint32_t kBuildTimestamp = static_cast<std::uint32_t>(AEGIS_BUILD_TS);

} // namespace aegis::edge

#endif
