# ESP-IDF Dev Container

This dev container uses the official Espressif IDF Docker image for ESP32 development.

## Prerequisites

- Docker installed and running
- VS Code with the "Dev Containers" extension (`ms-vscode-remote.remote-containers`)
- USB device access (for flashing/debugging)

## Getting Started

1. Open this project in VS Code
2. When prompted, click "Reopen in Container" (or use Command Palette: `Dev Containers: Reopen in Container`)
3. Wait for the container to build and start

## Building the Project

Once inside the container, open a terminal and run:

```bash
# Set target chip (e.g., esp32, esp32s3, esp32c3, esp32c6)
idf.py set-target esp32s3

# Build the project
idf.py build

# Flash to device (adjust port as needed)
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
idf.py -p /dev/ttyUSB0 monitor

# Build, flash, and monitor in one command
idf.py -p /dev/ttyUSB0 flash monitor
```

## USB Device Access

The container runs with `--privileged` and mounts `/dev` to allow access to USB serial devices. Common device paths:
- `/dev/ttyUSB0` - USB-to-Serial adapters
- `/dev/ttyACM0` - Native USB CDC devices

## Debugging with JTAG

For JTAG debugging (e.g., with ESP-PROG):

1. Connect your JTAG adapter
2. Run OpenOCD: `idf.py openocd`
3. In another terminal or VS Code, start GDB debugging

## Supported Targets

This project supports multiple ESP32 variants. Check the `sdkconfig.defaults.*` files for target-specific configurations:
- ESP32
- ESP32-S3
- ESP32-C3
- ESP32-C5
- ESP32-C6
- ESP32-P4
