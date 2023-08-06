#Cyboard Zergotech-like Custom Commission

* Keyboard Maintainer: [Cyboard](https://github.com/Cyboard-DigitalTailor)
* Hardware Supported: Cyboard Zergotech-like Custom Commission
* Hardware Availability: one-of-a-kind

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Building Firmware

Various sizes and thumb-cluster styles are supported as keymaps. To build the firmware, use the command:
`qmk compile -kb cyboard/zergotech_like -km default

## Flashing

To flash the firmware to your Cyboard,
1. Enter the bootloader by double-tapping the reset button on the back of the board, next to the USB port.
2. A USB device called `RPI-RP2` should appear on your computer.
3. Drag the `.uf2` firmware file into `RPI-RP2`.  `RPI-RP2` should now disappear, and your Cyboard is ready to use with your new firmware!
