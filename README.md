(c) 2018-2024 HomeAccessoryKid
lcm-demo helps to test and demo LCM4ESP32
use with [LCM4ESP32](https://github.com/HomeACcessoryKid/LCM4ESP32)

## signature creation
```
openssl sha384 -binary -out main.bin.sig main.bin
printf "%08x" `cat main.bin | wc -c`| xxd -r -p >>main.bin.sig
openssl sha384 -binary -out c3main.bin.sig c3main.bin
printf "%08x" `cat c3main.bin | wc -c`| xxd -r -p >>c3main.bin.sig
openssl sha384 -binary -out s2main.bin.sig s2main.bin
printf "%08x" `cat s2main.bin | wc -c`| xxd -r -p >>s2main.bin.sig
```