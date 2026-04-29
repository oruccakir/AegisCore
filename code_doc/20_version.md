# version.hpp — Firmware Version Constants

**File:** `edge/app/version.hpp`

---

## What this file is

`version.hpp` defines the firmware version number and build metadata. These values are sent to the gateway in response to a `kGetVersion` command.

---

## Version numbers

```cpp
inline constexpr std::uint8_t kVersionMajor = 1U;
inline constexpr std::uint8_t kVersionMinor = 0U;
inline constexpr std::uint8_t kVersionPatch = 0U;
```

Semantic versioning: `1.0.0`

- **Major**: incremented when the API changes incompatibly
- **Minor**: incremented for new backwards-compatible features
- **Patch**: incremented for bug fixes

---

## Git SHA — injected at build time

```cpp
#ifndef AEGIS_GIT_SHA
#define AEGIS_GIT_SHA "00000000"
#endif

inline constexpr char kGitSha[] = AEGIS_GIT_SHA;
```

CMake computes the current git commit SHA and passes it to the compiler:

```cmake
execute_process(COMMAND git rev-parse --short HEAD OUTPUT_VARIABLE GIT_SHA ...)
target_compile_definitions(... -DAEGIS_GIT_SHA="${GIT_SHA}")
```

If the build is not run from a git repository (or CMake doesn't inject this), the fallback `"00000000"` is used. This way the firmware always builds even outside a git context.

---

## Build timestamp — injected at build time

```cpp
#ifndef AEGIS_BUILD_TS
#define AEGIS_BUILD_TS 0U
#endif

inline constexpr std::uint32_t kBuildTimestamp = static_cast<std::uint32_t>(AEGIS_BUILD_TS);
```

Similarly, CMake can inject the current Unix epoch timestamp:

```cmake
string(TIMESTAMP BUILD_TS "%s")
target_compile_definitions(... -DAEGIS_BUILD_TS=${BUILD_TS})
```

The gateway can display this as a human-readable build date. If not injected, defaults to 0 (epoch = 1970-01-01 00:00:00 UTC).

---

## How these are used

In `main.cpp` `DispatchRemoteCmd()`, when `kGetVersion` is received:

```cpp
PayloadVersionReport vr = {};
vr.major    = kVersionMajor;      // 1
vr.minor    = kVersionMinor;      // 0
vr.patch    = kVersionPatch;      // 0
vr.build_ts = kBuildTimestamp;    // Unix timestamp
for (i = 0; i < 8 && kGitSha[i] != '\0'; ++i)
    vr.git_sha[i] = kGitSha[i];  // first 8 chars of SHA
```

The `PayloadVersionReport` is then encoded into an AC2 frame and transmitted to the gateway, which displays it in the dashboard version log.
