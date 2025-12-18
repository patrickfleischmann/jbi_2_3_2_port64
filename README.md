Intel Altera Jam STAPL Byte-Code Player Version 2.3.2

made 64bit compatible, removed parallel port support and some other old stuff (like DOS support).

Supported JTAG hardware: PicoBitBlaster

Original Docs: readme.txt and Altera AN 425 Using the Command-Line Jam STAPL Solution for Device Programming.pdf

Example usage: .\jbi.exe -aPROGRAM -ddo_bypass_ufm=1 -sCOM5 .\test\top2.jbc
