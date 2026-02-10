### My submission for the Altium Business Card [Design Contest](https://www.linkedin.com/posts/zachariah-peterson_electronicsengineering-pcbdesign-manufacturing-activity-7411813373404692480-vh8G)

To set the nRF53 internal LDO output voltage to 3.3 V:

```shell
nrfjprog --family nrf53 --memwr 0x00FF8010 --val 5
```
