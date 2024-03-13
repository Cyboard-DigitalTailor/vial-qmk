# Cyboard

* Keyboard Maintainer: [Cyboard](https://github.com/Cyboard-DigitalTailor)
* Hardware Supported: Cyboard Imprints and Dactyls
* Hardware Availability: [Imprint](https://www.cyboard.digital/product-page/imprint), [Dactyl](https://www.cyboard.digital/product-page/custom-dactyl-manuform)

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Building Firmware

Various sizes and thumb-cluster styles are supported as keymaps. To build the firmware, use the command:
`make cyboard/{MODEL}/{THUMB STYLE}_{ROWS}:vial`
where `{MODEL}` can be `imprint` or `dactyl`
, `{THUMB STYLE}` can be `imprint`, `manuform`, `dual_arcs`, or `single_arc`
, and `{ROWS}` can be `function_row`, `number_row`, or `letters_only`

## Flashing

To flash the firmware to your Cyboard,
1. Enter the bootloader by double-tapping the reset button on the back of the board.
2. A USB device called `RPI-RP2` should appear on your computer.
3. Drag the `.uf2` firmware file into `RPI-RP2`.  `RPI-RP2` should now disappear.
4. Repeat steps 1-3 on the other half.  Note: there is a rubber plug in the right-hand USB-C-to-computer port by default. Remove that and use that port for flashing. The split connection USB-C port will not be able to flash.
Your Cyboard is now ready to use with your new firmware!
