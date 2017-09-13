sbc windows
===========

A sbc encode/decode library migrated from [bluez project](http://www.bluez.org/sbc-13/).

## Compilation Trick
To avoid compilation issues, you should better switch compiler to c++ mode.

## .snd & .wav file format
They are nearly same except header region and data endian.
- `.snd` header size is 24 bytes.
- `.wav` header size is 44 bytes.
- `.snd` data region is in Big-Endian.
- `.wav` data region is in Little-Endian.

## Reference
https://en.wikipedia.org/wiki/Au_file_format
https://en.wikipedia.org/wiki/SND_(file)
http://www.bluez.org/sbc-13/
https://www.kernel.org/pub/linux/bluetooth/

## To be continued ...