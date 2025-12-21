# ESP32WOL

An full web server with 2FA to wake devices in your local network.

- **Index**
    - Esp-idf Setup
    - Gettign Started
        - Esp Config Menu Setup
        - Dependencies
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

### Dependencies

This project uses an MDNs service to allow other devices in your network to discover the webserver using esp32.local as the url instead of the ip

```bash
idf.py add-dependency "espressif/mdns"
```

### NVS Partition

Use the following commands to create an NVS compatible secrets binary file from your CSV file and flash it to the SOC.

```bash
# Create secrets.bin
python $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate secrets.csv secrets.bin 0x10000 # 0x_num must match the one in partitions.csv

# Flash the parition to the SOC

# IMPORTANT: Remember to reset the SOC (hold right button, then press and release left button, then release right button) before doing this
parttool.py --partition-table-file partitions.csv write_partition --partition-name storage --input secrets.bin
```

#### Create the server certs
```bash
openssl req -x509 -newkey rsa:2048 \
    -keyout server.key \
    -out server.crt \
    -days 3650 \
    -nodes \
    -sha256

# Create the DER certs (ESP32 has less issues with these)
openssl x509 -in server.crt -outform der -out server.der
openssl rsa -in server.key -outform der -out server_key.der
```



#### Get the root cert for all  the sites you'll be HTTPS-connecting to
```bash
getroot() {
    local domain="$1"
    local output="$2.pem"

    echo "Connecting to $domain..."

    # 1. Get the last cert sent by the server (usually the Intermediate)
    openssl s_client -connect "${domain}:443" -showcerts </dev/null 2>/dev/null | \
    awk '/BEGIN CERTIFICATE/{ cert = $0; next } 
         { cert = cert "\n" $0 } 
         /END CERTIFICATE/{ last_cert = cert } 
         END { printf "%s\n", last_cert }' > temp_intermediate.pem

    # 2. Extract the URL of the Root/Issuer certificate
    local issuer_url=$(openssl x509 -in temp_intermediate.pem -noout -issuer_url)

    if [ -z "$issuer_url" ]; then
        echo "Could not find issuer URL. The last cert might already be the Root."
        mv temp_intermediate.pem "$output"
    else
        echo "Downloading Root from: $issuer_url"
        # 3. Download and convert from DER (binary) to PEM (text)
        curl -sL "$issuer_url" | openssl x509 -inform DER -outform PEM -out "$output"
        rm temp_intermediate.pem
    fi

    echo "--- Verification ---"
    # Corrected command: x509 (not x649)
    openssl x509 -in "$output" -noout -subject -issuer -dates
}

# Use as 'getroot api.ipify.org my_cert.pem'
```
