# Enhancement Pack

The DBPro profile runs the forms of the Enhancement Pack plugin on every platform.
These are the places where OpenDBC differs from the Windows plugin.

| Family | Behaviour |
| --- | --- |
| File blocks | ZIP archives of any extension. Deflate compression; a key turns on ZipCrypto for later additions. Standard tools such as `unzip` open them. Blowfish-wrapped `.fb` files from early releases do not open; the last DBPro release could not open them either. |
| Data files | Delimited text only. An `.xls` file is an error: save it as CSV. |
| File maps | Windows uses named shared memory. Other platforms use the file `opendbc-filemap-<name>` in the temporary folder, which is removed once the last program closes the map. String fields are 255 bytes. `WAIT FOR FILE MAP EVENT` waits up to 5 seconds. |
| Ogg Vorbis | Numbers 1 to 254, apart from sounds. Volume is 0 to 100; `GET OGG VORBIS VOLUME` returns the attenuation in hundredths of a decibel, as DirectSound did (0 to -9600). `ENCODE TO OGG VORBIS` reads WAV files at quality -0.1 to 1.0 (0.5 by default). |
| System information | Drive geometry the platform hides without administrator rights reads 0. Memory units are 0 (KB), 1 (MB) and 2 (GB). |
| Speech | `AVSpeechSynthesizer` on macOS and iOS, SAPI on Windows. Android and Linux have no voice: `GET VOICE API()` returns 0 and messages are dropped. Voices are listed by identifier. On Apple, `SEND VOICE MESSAGE XML` speaks SSML and reads SAPI markup as plain text. Headless runs never speak. |
| File dialogs | The platform's dialog. Filters take the form `"Text\|*.txt;*.dat\|All\|*.*"`. Cancelling, or a headless run, returns `""`. |
| EAX | Compatibility stubs: commands do nothing, functions return 0, and `GET EAX()` reports no EAX. |
