# update-psu

Update PSU firmware.

## Building for arm64 platform

```bash
autoreconf --install
./configure --host=aarch64-linux-gnu --target=aarch64-linux-gnu
make
```

## Usage

```bash
Usage: update_psu ${i2c_busnum} 0x${i2c_slave_addr} ${fw_path}
```
