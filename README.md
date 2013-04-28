Keeper
======

Experimental password storage device and LUKS SD card reader


For hardware:

See KiCad project in hardware/


To build firmware:

1. Get ChibiOS

2. Edit Makefile, set CHIBIOS to the path of ChibiOS

3. Type make

4. Profit

Currently, there's only a serial console for testing various modules and
hardware features.

Further reading:

docs/cryptfile.dia - details about the preliminary encrypted file structure

docs/dbfile.dia    - password & account database file structure

Both files require Gnome Dia for viewing.

cd.rattan@gmail.com
