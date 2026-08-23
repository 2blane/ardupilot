# Build Notes

## Mac Build
For mac install it was a nightmare to setup the toolchain to build. Basically though, python had an openssl problem using an old version of 1.1 instead of 3 something.
Then, we had to manually download the tarball for the build components `gcc-arm-none-eabi` and put it in /usr/local -> then update the $PATH so that it found the binaries.
Then, when we ran the ./waf code we had to go through 20 security prompts to allow the code to run. Now we've got it working so it shouldn't be a problem going forward.

1) For build the bootloader
```
./waf distclean ; Tools/scripts/build_bootloaders.py Starbound
```

2) For building the board
```
./waf configure --board Starbound ; ./waf copter
```

3) All in one command
```
./waf distclean ; Tools/scripts/build_bootloaders.py Starbound ; ./waf configure --board Starbound ; ./waf copter ; arm-none-eabi-objcopy -I ihex -O binary build/Starbound/bin/arducopter_with_bl.hex build/Starbound/bin/arducopter_with_bl.bin
```

### Creating an ABIN file for SD-card ArduPilot updates
The Starbound bootloader checks the SD card for this file on startup:
```
/APM/UPDATE/ardupilot.abin
```

Build the normal Starbound Copter firmware:
```
./waf configure --board Starbound
./waf copter
```

The build creates:
```
build/Starbound/bin/arducopter.abin
```

Copy or upload that file to the SD card as:
```
/APM/UPDATE/ardupilot.abin
```

On the next boot, the bootloader verifies and flashes the file, deletes it after a successful update, and appends the result to:
```
/APM/UPDATE/ardupilot-update.log
```

If you need to create the ABIN manually from an existing `.bin`, run:
```
Tools/scripts/make_abin.sh build/Starbound/bin/arducopter.bin build/Starbound/bin/arducopter.abin
```

4) A) Uploading the code over usb with dfu-util
```
dfu-util -a 0 --dfuse-address 0x08000000 -D build/Starbound/bin/arducopter_with_bl.bin -R
```
4) B) Uploading code using STM32 Programmer CLI. Much Faster!
You might need to run STM32_Programmer_CLI -l to get the USB number.
```
STM32_Programmer_CLI -c port=USB1 -e all -d build/Starbound/bin/arducopter_with_bl.hex -v
```

5) If STM32 Programmer is giving read permission errors then do this instead:
```
STM32_Programmer_CLI -c port=USB1 -d build/Starbound/bin/arducopter_with_bl.hex
```
Then you can check it with the following command to see if you get data.
```
STM32_Programmer_CLI -c port=USB1 -r8 0x08000000 64
```

### Installing with stm32flash (alternative to STM32_Programmer_CLI)
You might need to run the following to get the USB
```
ls /dev/tty.*
```
### Installing code:
```
stm32flash -w build/Starbound/bin/arducopter_with_bl.hex -v -g 0x08000000 -R -i rts,dtr,-rts,-rts,-rts,-rts,-rts,-rts,-rts,-rts,-rts,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,dtr,dtr,dtr,dtr,dtr,dtr,dtr,dtr:rts,rts,rts,rts,rts,rts,rts,rts,rts,-dtr,dtr /dev/tty.usbserial-110
```

```
# SETS THE BOOT0 PIN TO 1
stm32flash -w build/Starbound/bin/arducopter_with_bl.hex -v -g 0x08000000 -R -i -rts,-rts,-rts,-rts,-rts,-rts,-rts,-rts,-rts,-rts,-rts,-rts:-rts /dev/tty.usbserial-110
```

```
# SETS THE BOOT0 PIN TO 0
stm32flash -w build/Starbound/bin/arducopter_with_bl.hex -v -g 0x08000000 -R -i rts,rts,rts,rts,rts,rts,rts,rts,rts,rts,rts,rts,rts,rts,rts,rts,rts,rts:rts /dev/tty.usbserial-110
```

```
# SETS THE NRST TO 1
stm32flash -w build/Starbound/bin/arducopter_with_bl.hex -v -g 0x08000000 -R -i dtr,dtr,dtr,dtr,dtr,dtr,dtr,dtr,dtr,dtr,dtr,dtr,dtr,dtr,dtr,dtr,dtr,dtr:dtr /dev/tty.usbserial-110
```

```
# SETS THE NRST TO 0
stm32flash -w build/Starbound/bin/arducopter_with_bl.hex -v -g 0x08000000 -R -i -dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr,-dtr:-dtr /dev/tty.usbserial-110
```

5) Uploading firmware to the cloud for our firmware updater.
```
darwincli firmware upload 2 build/Starbound/bin/arducopter_with_bl.hex "New firmware update"
```

## Docker Build
To build for our drone run the following inside the main directory.

1) Create the docker container
```
docker build . -t ardupilot --build-arg USER_UID=1000 --build-arg USER_GID=1000
```

2) Run docker container
```
docker run --rm -it -v "$(pwd):/ardupilot" -u "1000:1000" ardupilot:latest bash
```

3) You may need to run the following to generate a boot loader for the board
```
./waf distclean
Tools/scripts/build_bootloaders.py Starbound
```

4) Configure which board you are building.
```
./waf configure --build Starbound
```

5) Build out the board
```
./waf copter
```

6) Two part command - if for some reason it says Starbound is invalid board just keeping calling the command it takes a second to be valid... very odd.
```
./waf distclean ; Tools/scripts/build_bootloaders.py Starbound
./waf list_boards ; ./waf configure --board Starbound ; ./waf copter
```

## Custom Build
To make a custom build of Ardupilot for a new board use the following steps:
1) Create a new folder or copy an existing board folder under libraries/AP_HAL_ChibiOS/hwdef
2) Edit the files in that folder for the new board configuration.
3) Add a row for your new board under Tools/AP_Bootloader/board_types.txt
4) Update Tools/scripts/generate_manifest.py adding in your brand and row of data
5) Run the commands above for Build Notes and Docker Build if necessary.
6) The firmware should be created and found under Tools/bootloaders.

# CAN'T SAVE PARAMATERS
We found a line in the parameters file that if in the firmware defaults.parm it won't allow you to save parameters. The line we found was `FORMAT_VERSION,120` and this needs to be removed.
