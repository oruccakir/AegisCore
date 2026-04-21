# COMMANDS

```bash
cmake -S edge -B edge/build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
```

```bash
cmake --build edge/build
```

```bash
st-flash write edge/build/aegiscore-edge.bin 0x8000000
```

```bash
st-info --probe
```