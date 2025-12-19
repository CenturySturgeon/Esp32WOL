# ESP32WOL

An full web server with 2FA to wake devices in your local network.

- **Index**
    - Overview
    - Esp-idf  Setup
    - Esp Menu Config Setup
    - Credentials Fabricator
    - NVS Secrets


## NVS Secrets

Use the following commands to create an NVS compatible secrets binary file from your CSV file and flash it to the SOC.

```bash
# Create secrets.bin
python $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate secrets.csv secrets.bin 0x10000 # 0cnum must match the one in partitions.csv

# Flash the parition to the SOC
# Remember to reset the SOC (hold right button, then press and release left button, then release right button) before doing this
parttool.py --partition-table-file partitions.csv write_partition --partition-name storage --input secrets.bin
```