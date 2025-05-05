# *_fakemote_*
_A Wii cIOS module that fakes Wiimotes from the input of USB game controllers._

## Features

### Supported USB game controllers
| Device Name              | Vendor Name | Vendor ID | Product ID |
|:------------------------:|:-----------:|:---------:|:----------:|
| PlayStation 3 Controller | Sony Corp.  | 054c      | 0268       |
| DualShock 4 [CUH-ZCT1x]  | Sony Corp.  | 054c      | 05c4       |
| DualShock 4 [CUH-ZCT2x]  | Sony Corp.  | 054c      | 09cc       |

- DS3 and DS4 support includes LEDs, rumble, and the accelerometer
- DS4's touchpad is used to emulate the Wiimote IR Camera pointer
- Both controllers emulate a Wiimote with the Nunchuk and Classic Controller extensions connected. Press L1+L3 to switch between them
- Three IR pointer emulation modes: direct (touchpad, only for DS4), analog axis relative (move the pointer with the right analog) and analog axis absolute (the pointer is moved proportionally to the right analog starting from the center). Press R1+R3 to switch between them

## Installation
1) Download [d2x cIOS Installer for regular Wii](https://wii.hacks.guide/cios.html)/[d2x cIOS Installer for vWii](https://wiiu.hacks.guide/#/vwii-modding) and extract it to the SD card
2) Copy `FAKEMOTE.app` to the d2x cIOS Installer directory that contains the modules of the cIOS version you want to install.
   For example, for `d2x-v10-beta52` copy `FAKEMOTE.app` to `sd:/apps/d2x-cIOS-Installer-Wii/v10/beta52/d2x-v10-beta52`
3) Open d2x cIOS Installer's `ciosmaps.xml` (located at `sd:/apps/d2x-cIOS-Installer-Wii/ciosmaps.xml`) and do the following:
   1) Locate the line containing the base IOS version you want to install. It starts with `<base ios=`.
      For base IOS 57:
      ```xml
      <base ios="57" version="5918" contentscount="25" modulescount="6">
      ```
   3) Increase `modulescount` and `contentscount` by 1.
      For base IOS 57:
      ```xml
      <base ios="57" version="5918" contentscount="26" modulescount="7">
      ```
   3) Add a `<content>` entry for `FAKEMOTE` after the last `<content module>`.
      Note that the `id` attribute must be set to the highest number among the
      existing content entries within the chosen IOS base, plus 1 (in
      hexadecimal format).
      For base IOS 57:
      ```xml
      <content id="0x23" module="FAKEMOTE" tmdmoduleid="-1"/>
      ```
4) Run d2x cIOS Installer and install the cIOS

## Usage
- You can install [Priiloader](https://wii.hacks.guide/priiloader.html) and change the IOS slot to use when running System Menu and disc games:
   - [Enter Priiloader Menu](https://wii.hacks.guide/priiloader.html#section-iii---entering-priiloader) > Settings > Use System Menu IOS (off) > IOS to use for SM (System Menu)
- You can configure your USB loader to specify the IOS slot to use when running the loader and/or games

## Notes
- This has only been tested with base IOS 57 and 58
- Use base IOS 58 to have compatibility with both USB ports
- This is still in beta-stage, therefore it might not work as expected

## Compilation

##### 1) Install `devkitARM`
- Download and install [devkitARM](https://devkitpro.org/wiki/Getting_Started)
   - Make sure to install the `devkitarm-cmake` package when using `pacman`

##### 2) Install `stripios`
1) Download `stripios`'s source code from [Leseratte's d2xl cIOS](https://github.com/Leseratte10/d2xl-cios/tree/master/stripios)
2) Compile it:
   ```bash
   g++ main.cpp -o stripios
   ```
3) Install it:
   ```bash
   cp stripios $DEVKITPRO/tools/bin
   ```

##### 3) Build `FAKEMOTE.app`
1. `mkdir build && cd build`
2. Configure it with CMake. Two options:\
  &ensp;a. `arm-none-eabi-cmake ..`\
  &ensp;b. `cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=$DEVKITPRO/cmake/devkitARM.cmake ..`
3. `make` (or `ninja` if configured with `-G Ninja`)
4. `FAKEMOTE.app` will be generated

I recommend passing `-DCMAKE_COLOR_DIAGNOSTICS:BOOL=TRUE`, especially when using Ninja.

## Credits
- [Dolphin emulator](https://dolphin-emu.org/) developers
- [Wiibrew](https://wiibrew.org/) contributors
- [d2x cIOS](https://github.com/davebaol/d2x-cios) developers
- [Aurelio Mannara](https://twitter.com/AurelioMannara/)
- _neimod_, for their [Custom IOS Module Toolkit](http://wiibrew.org/wiki/Custom_IOS_Module_Toolkit)
- Everybody else who helped me!
- ...and everybody who made Wii's scene a reality! 👍

## Disclaimer
````
THIS APPLICATION COMES WITH NO WARRANTY AT ALL, NEITHER EXPRESSED NOR IMPLIED.
NO ONE BUT YOURSELF IS RESPONSIBLE FOR ANY DAMAGE TO YOUR WII CONSOLE BECAUSE OF A IMPROPER USAGE OF THIS SOFTWARE.
````
