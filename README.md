# ESP32WOL

An full web server with 2FA to wake devices in your local network.

- **Index**
    - Esp-idf Setup
    - Gettign Started
        - Esp Menu Config Setup
        - NVS Partition
        - Credentials Fabricator

## Getting Started

### Config Menu Setup

For this project to work properly, you need to configure some values on the Esp32 via the config menu (run the `idf.py menuconfig` command on terminal):

- Serial flasher config -> Flash size -> 4 MB
- Serial flasher config -> Detect flash size when flashing bootloader
- Partition Table -> Partition Table (Single factory app, no OTA)  -> Custom partition table CSV (default value)
- Component config -> ESP-TLS -> Enable client session tickets
- Component config -> ESP HTTPS server -> Enable ESP_HTTPS_SERVER component


### NVS Partition

Use the following commands to create an NVS compatible secrets binary file from your CSV file and flash it to the SOC.

```bash
# Create secrets.bin
python $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate secrets.csv secrets.bin 0x10000 # 0x_num must match the one in partitions.csv

# Flash the parition to the SOC
# Remember to reset the SOC (hold right button, then press and release left button, then release right button) before doing this
parttool.py --partition-table-file partitions.csv write_partition --partition-name storage --input secrets.bin
```

