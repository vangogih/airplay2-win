# airplay2-win
Airplay2 for windows.

Migrate [AirplayServer](https://github.com/KqSMea8/AirplayServer) and [dnssd](https://github.com/jevinskie/mDNSResponder) to Windows Platform.

## Fork features
- Support window rescaling. Screencast perfectly fits to the window.
- Bilinear rendering filter. Less pixelation on making upscale.

Also tested cubic interpolation but it causes too much performance degradation.

## Known issues
- After resizing the window with sharing screen UV layers can be drawn with wrong scale.
To fix that try to change window size from bigger to smaller.
- Also if you will get total black screen after resizing, just click on your device screen

## Build

- Open `airplay2-win.sln` in Visual Studio 2019.
- Make `airplay-dll-demo` as Start Project.
- `Ctrl + B`, Build `airplay-dll-demo`.
- The generated lib and dll files will be placed in `AirPlayServer` folder.

## Reference

- [shairplay](https://github.com/juhovh/shairplay) 
- [dnssd](https://github.com/jevinskie/mDNSResponder)
- [AirplayServer](https://github.com/KqSMea8/AirplayServer)
- [xindawn-windows-airplay-mirroring-sdk](https://github.com/xindawndev/xindawn-windows-airplay-mirroring-sdk)
  