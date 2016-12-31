# E-Ink adapter board and driver code

See https://hackaday.io/project/7443-e-ink-display-adapter.

This version has grayscale support that depends on some modifications to the
rev. 2 adapter board. Specifically, CKV must be controlled directly by the
microcontroller and not through the shift register. So far I have done this by
breadboarding the shift register and wiring its pins (except CKV) to the
appropriate pins on the board, then wiring CKV separately.

### Previous work and licensing

This is heavily based on previous work:
 * PetteriAimonen [explanations and PCB](http://essentialscrap.com/eink/index.html) (unknown license) and [driver code](https://github.com/PetteriAimonen/ED060SC4_driver) (public domain license)
 * Sprite_tm [explanations and PCB](http://spritesmods.com/?art=einkdisplay) (CC-BY-SA license) and [driver code](http://git.spritesserver.nl/espeink.git/) [(here's a github fork)](https://github.com/take-i/espeink) (beer-ware license)
 * zephray [NekoCal project](https://github.com/nbzwt/NekoCal), in particular the driver code (MIT license)

Additionally, the driver code uses the [esp-open-rtos ESP8266 SDK](https://github.com/Superhouse/esp-open-rtos.git) (various licenses).

As far as I'm concerned, I'd license this repository under the MIT license, but
the PCB may be under CC-BY-SA, and there may be other restrictions if you use
the esp-open-rtos SDK.
