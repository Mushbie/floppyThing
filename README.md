FloppyThing
===========


About
-----
FloppyThing is the firmware part of a floppy disk imager.
The idea is to record all the magnetic information raw from the floppy, and then figure out the data later in software.
It is based on the STM32 chip family, and uses libopencm3.
During development the hardware is an STM32F4 Discovery board, but this will be changed to a custom board based on a smaller chip.

Project State
-------------
The project is currently in an early stage of development, and is to be considered non functional.

Prerequisites
-------------
 - Python, for building libopencm3.
 - arm-none-eabi toolchain.
 
 **For Windows**
 I recommend building under MSYS2, using a windows native toolchain added to the MSYS path variable.

License
-------
The FloppyThing firmware is free software. It is distributed under the terms of the [GNU General Public License version 3][gpl3]

[gpl3]: https://www.gnu.org/licenses/gpl-3.0.html