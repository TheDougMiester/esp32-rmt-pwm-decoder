This library, esp32-rmt-pwm-decoder, is fairly simple, but wouldn’t have been possible without the work done by Suat Özgür and his contributors on the RCSwitch project (jttps://github.com/sui77/rc-switch/). This library also would not be possible without the excellent work done by Wolfgang Schmieder in his development of RcSwitchReceiver (https://github.com/dac1e/RcSwitchReceiver/). I’m especially grateful to Herr Schmieder for his quick response to my concerns while he was developing his library. Thanks again, sir. Let me also take a second to thank the folks working on pioarduino and esp32-arduino-lib-builder. Without your tools, my esp32 might well be in a dumpster somewhere.

The purpose of this library is to decode a 315MHz or 433MHz remote control with a minimal amount of overhead -- but not so minimal that it couldn’t be easily modified. Since I plan to use this library with a talking, arm-moving, skeleton – and do it all on one chip, I needed to decode the values and interfere with any other operations on the chip, else I would have just used RcSwitchReceiver (I don’t mean to cast any shade on RcSwitchReceiver, it’s brilliant and I shamelessly borrowed from it to do what I’ve done).

As written, this library supports only the PT2262 protocol, but is easily modified to handle any protocol in the RcSwitchReceiver library (and probably any IR receiver, but your mileage may vary). I think you’ll only need to modify one line in the library (the template in the .h file) for this to work for another protocol.

In the header file, near the bottom, you’ll find a line that looks a like this:

static makeTimingSpec< 1, 450, 20,   1,   31,    1,  3,    3,  1, false>   rxProtocol; //PT2262

the second field (450) is the base pulse width in microseconds – it’s the average pulse width for my 4-button key fob. I have an 8-button remote with an average pulse width of 350. If I want to use that instead, I can change the 350 to 450. If I want to use them both at the same time, I’d open up the error window a bit wider and change the 20 to 30 or so.

How do you find out your average pulse width to stick in there? Well, either use a $USD20 logic analyzer and PulseView software (free) to look at the pulses, or you can use the example in RcSwitchReceiver called LearnRemoteControl.ino. Another alternative is to use AdvanceReceiveDemo in the rc-switch library. I (unnecessarily) used all three, but the point of the exercise is to know your average pulse width and use their tools to figure out the protocol being used by your remote. Either RcSwitchReceiver or AdvanceReceiveDemo will tell you which protocol you have, if you don’t already know it. I duplicated the format in RcSwitchReceiver, so it’s a simple matter of replacing the PT2262 protocol I have with whatever you’re using.

My hardware:

1. ESP32-S3-DEVKITC-1-N8R2 (WROOM-1); about $USD15 from Digikey
2. 3DMakerWorld Adafruit Keyfob 4 Button RF Remote Control – 315MHz; about $USD10 off eBay
3. 315MHz transmitter/receiver kit; about $USD5 off eBay
4. A breadboard.

I wired everything together using wires taken from an ethernet cable and kept them as short as possible to keep the EMF to a minimum; those receivers are cheap and kind of noisy. I recommend adding an antenna to the receiver. I’m presently powering the receiver off the 5V and GND pins on the ESP32, but that will change when I put the final system together. 

Notes about the software framework.

I wanted to use the rmt code in ESP-IDF version 5 – mostly to learn about the platform. So, I used platformio and the arduino framework..and learned that platformio wasn’t supporting version 5, but a community fork of platformio called pioarduino was supporting it. So, I installed that and pressed on with VsCode.

That decision led to other headaches though. The first headache was the noise filter.  PT2262 pulse widths are around 350µs, so I wanted to filter out anything shorter than 2000µs or so. But... for the .signal_range_min_ns, the max value the register can hold follows the formula  uint32_t filter_reg_value = filter_clock_resolution_hz * signal_range_min_ns) / 1000000000UL. Niiice. If you select RMT_CLK_SRC_DEFAULT, that’s an 80MHz clock, so the best minimum pulse you can get is 1250ns (1.25 µs), a long cry from the 120µs or so I wanted. I ended up using RMT_CLK_SRC_RC_FAST, 8.5MHz, because I could get up to 10 µs or so. At some point, I’ll try RMT_CLK_SRC_XTAL and .resolution_hz = 100000 (100KHz), that may give me the results I wanted.

The next problem I faced was my monitor going nuts with an rmt buffer overflow error. In ESP-IDF < v5.3, you can’t even turn off the error message. At first, I ended up downloading https://github.com/espressif/esp32-arduino-lib-builder and rebuilding the rmt library after commenting out the message. To make things easier, I used the GUI in tools/config_editor/app.py

The next problem after that was finding out that when I turned on the DMA feature (to offload processing out of the CPU), my code stopped processing data. No idea why. In frustration, I decided to migrate my pioarduino to v5.5. That was sort of bleeding edge at the time I wrote this (March 2025). I found a version of esp32-arduino-lib-builder with 5.5, built everything and modified the platform.json file in ~/.platformio/platforms/espressif32 to point to the proper directories (and zip files which I zipped myself). It was kind of a pain to figure out (I’ll post notes on it elsewhere), but it ultimately worked. I was disappointed that the noise filter issue wasn’t solved, but the streaming buffer error had been fixed and the DMA problem was solved too.

My final problem was that it turns out signal_range_max_ns isn't exactly a filter. Any pulses above that value trigger a "done" event. This is a problem, since the maximum value I could fit in to signal_range_min_ns is way too small, so very often noise finds its way into your pulse train. After a crap-ton of bug hunting and circuit re-wiring, I realized we can recover from noise by leveraging the fact that most remotes send the signal 3 times; I just needed to make sure I collected the entire pulse train. So, I set the signal_range_max_ns to its max value, which is calculated by uint32_t idle_reg_value = resolution_hz * signal_range_max_ns / 1000000000UL. In other words, 32767 will be the biggest value you can fit in idle_reg_value. If you want a larger signal_range_max_ns, reduce your resolution_hz. If you dig into my code, you’ll find I did an evil thing to search for a second decoded PT2262 value in the train: When an error is encountered, I altered the counter of a “for” loop inside the loop. At some point, I’ll go back and fix that.

Summary: 

In all, I can’t say if my approach is superior to Wolfgang Schmieder’s for my project. His is certainly cleaner, while mine is more reliant on native esp32 hardware (which I hope means mine runs slightly faster and is less CPU intensive). I did learn quite a bit about ESP32 programming from this, so I have no regrets. But I do hope version 6 of ESP-IDF is better than version 5. There are concepts here that are very, very awkward and limiting.
