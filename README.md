# ADC
Analog-to-Digital Converter using the BeagleBone Black

# Set Up
On the Beagebone Black:

**Step 1:** Run the configuration script
```bash
ADC$ chmod +x config/configADC
ADC$ ./config/configADC
```

**Step 2:** Compile `adc-read` and move the binary to `/home/root/ADC/bin/`
```bash
ADC$ make
ADC$ cp ./bin/adc-read /home/root/ADC/bin/  # make this directory if it does not exist
```

**Step 3:** Start the `runadc` script
```bash
ADC$ chmod +x ./runadc
ADC$ ./runadc
```
