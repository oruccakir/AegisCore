# COMMANDS

```bash
sudo apt update && sudo apt install -y gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi cmake ninja-build stlink-tools
```

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

```bash
git submodule update --init --recursive
```
