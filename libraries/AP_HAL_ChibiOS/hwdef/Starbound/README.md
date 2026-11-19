# Starbound Flight Controller

See [the build notes](../../../../STARBOUND.md) for the current board revision,
serial mapping, firmware installation, and peripheral programming.

The current hardware uses an STM32H743, an ICM42688 IMU on SPI1, a BMP388-family
barometer at I2C address 0x76 or 0x77, and a microSD card. The screen uses SERIAL2 and
ELRS uses SERIAL4. There is no separate Wi-Fi module or HM-TRP radio.

ELRS uses the custom MAVLink2 integration at 460800 baud. The buzzer is disabled.
Hardware pin assignments are defined in `hwdef.dat` and active parameter
defaults in `defaults.parm`. Other parameter exports in this directory are
historical snapshots, not the defaults for the current board.
