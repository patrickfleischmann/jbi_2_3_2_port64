Intel Altera Jam STAPL Byte-Code Player Version 2.3.2

made 64bit compatible, removed parallel port support and some other old stuff (like DOS support).

Supported JTAG hardware: PicoBitBlaster

Original Docs: readme.txt and Altera AN 425 Using the Command-Line Jam STAPL Solution for Device Programming.pdf

### Help message
```
.\jbi.exe -h

Jam STAPL ByteCode Player Version 2.3 (20231228)
Copyright (C) 2023 Intel Corporation

Port for 64bit systems
Adapted for PicoBitBlaster JTAG programmer (on USB-serial)
PF, 17.12.2025
Usage:  jbi [options] <filename>

Available options:
    -h          : show help message
    -v          : show verbose messages
    -i          : show file info only - does not execute any action
    -a<action>  : specify an action name (Jam STAPL)
    -d<proc=1>  : enable optional procedure (Jam STAPL)
    -d<proc=0>  : disable recommended procedure (Jam STAPL)
    -s<port>    : serial port name (Picoblaster: 230400, 8N1, DTR/RTS)
    -r          : don't reset JTAG TAP after use
PS C:\home\projekte\c\jbi_2_3_2_port64>
```

### Program
```
.\jbi.exe -aPROGRAM -ddo_bypass_ufm=1 -sCOM5 .\test\top2.jbc

Jam STAPL ByteCode Player Version 2.3 (20231228)
Copyright (C) 2023 Intel Corporation

Port for 64bit systems
Adapted for PicoBitBlaster JTAG programmer (on USB-serial)
PF, 17.12.2025
Debug: opened COM5, com_handle = 00000000000000C0
Device #2 Silicon ID is ALTERA10(00)
Device #1 Silicon ID is ALTERA10(00)
erasing MAXII device(s)...
erasing MAXII CFM block...
programming CFM block...
verifying CFM block...
DONE
Exit code = 0... Success
```
### Get File Info
```
.\jbi.exe -i .\test\top2.jbc

Jam STAPL ByteCode Player Version 2.3 (20231228)
Copyright (C) 2023 Intel Corporation

Port for 64bit systems
Adapted for PicoBitBlaster JTAG programmer (on USB-serial)
PF, 17.12.2025
CRC matched: CRC value = EA9B
File format is Jam STAPL ByteCode format
NOTE "CREATOR" = "QUARTUS PRIME JAM COMPOSER 25.1"
NOTE "DATE" = "2025/12/18"
NOTE "DEVICE" = "5M40Z, 5M40Z"
NOTE "NEED_FREQUENCY_CONTROL" = "0"
NOTE "FILE" = "top_outer_LED.pof, top_inner.pof"
NOTE "TARGET" = "1, 2"
NOTE "IDCODE" = "020A50DD, 020A50DD"
NOTE "USERCODE" = "00191E07, 00191E67"
NOTE "CHECKSUM" = "001921FF, 0019215F"
NOTE "SAVE_DATA" = "DEVICE_DATA"
NOTE "SAVE_DATA_VARIABLES" = "V0, A12, A13, A25, A43, A92, A94, A95, A99, A100, A105, A109, A111, A217"
NOTE "STAPL_VERSION" = "JESD71"
NOTE "JAM_VERSION" = "2.0"
NOTE "ALG_VERSION" = "68"

Actions available in this file:
PROGRAM
    DO_BLANK_CHECK (optional)
    DO_VERIFY (recommended)
    DO_SECURE (optional)
    DO_DISABLE_ISP_CLAMP (optional)
    DO_BYPASS_CFM (optional)
    DO_BYPASS_UFM (optional)
    DO_REAL_TIME_ISP (optional)
    DO_FORCE_SRAM_DOWNLOAD (optional)
    DO_DISABLE_MAXII_PRESET_BSR (optional)
    DO_READ_USERCODE (optional)
    DO_INIT_CONFIGURATION (optional)
BLANKCHECK
    DO_DISABLE_ISP_CLAMP (optional)
    DO_BYPASS_CFM (optional)
    DO_BYPASS_UFM (optional)
    DO_REAL_TIME_ISP (optional)
    DO_FORCE_SRAM_DOWNLOAD (optional)
    DO_DISABLE_MAXII_PRESET_BSR (optional)
VERIFY
    DO_DISABLE_ISP_CLAMP (optional)
    DO_BYPASS_CFM (optional)
    DO_BYPASS_UFM (optional)
    DO_REAL_TIME_ISP (optional)
    DO_FORCE_SRAM_DOWNLOAD (optional)
    DO_DISABLE_MAXII_PRESET_BSR (optional)
    DO_READ_USERCODE (optional)
ERASE
    DO_BLANK_CHECK (optional)
    DO_DISABLE_ISP_CLAMP (optional)
    DO_BYPASS_CFM (optional)
    DO_BYPASS_UFM (optional)
    DO_REAL_TIME_ISP (optional)
    DO_FORCE_SRAM_DOWNLOAD (optional)
    DO_DISABLE_MAXII_PRESET_BSR (optional)
READ_USERCODE
CHECK_IDCODE
```
