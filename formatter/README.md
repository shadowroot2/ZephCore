nRF formatter tools

- QSPI is formatted for all supported boards
- Watch out for softdevice version! Flashing the wrong version can corrupt the node and you'll need a full bootloader reflash with adafruit-nrfutil!
  - You can check what softdevice version you use if you open INFO_UF2.TXT on the storage drive when in DFU mode. Bootloader should say "sxxx 6.x.x" for v6 and "sxxx 7.x.x" for v7
- Formatter output logs from the process over serial
- After format, it puts back the device to Mass Storage DFU mode
