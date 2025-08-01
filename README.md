nvBrightness: nVidia Control Panel brightness at your fingertips
================================================================

[![Build status](https://img.shields.io/github/actions/workflow/status/pbatard/nvBrightness/vs2022.yml?style=flat-square)](https://github.com/pbatard/nvBrightness/actions/workflows/vs2022.yml)
[![Release](https://img.shields.io/github/release-pre/pbatard/nvBrightness.svg?style=flat-square)](https://github.com/pbatard/nvBrightness/releases)
[![Github stats](https://img.shields.io/github/downloads/pbatard/nvBrightness/total.svg?style=flat-square)](https://github.com/pbatard/nvBrightness/releases)
[![Licence](https://img.shields.io/badge/license-GPLv3-blue.svg?style=flat-square)](https://www.gnu.org/licenses/gpl-3.0.en.html)

## Motivation

Because I recently got an OLED monitor that runs in HDR mode, and that I need to adjust the
brightness quickly and easily. Other solutions, like Twinkle Tray's SDR brightness, do not
provide adjustments that look good compared to the nVidia's Control Panel brightness, but
the nVidia Control Panel takes **ages** to launch and nVidia, in their great wisdom, also
removed any automated/3rd-party means to control brightness in their modern drivers, such as
the one they used to provide through `nvcpl.dll`...

Since there happens to be 2 useless keys (`Scroll Lock` and `Pause|Break`) on pretty much
every keyboard, that could be put to better use, I might as well control brightness with
these, using the exact brightness method that the nVidia control panel uses.

In short, this is software *by me, for me, for my specific hardware and workflow*, which you
might also find useful for you, but which I am not planning to expand for your specific use.

## How it works

1. We add a handler to intercept the `Scroll Lock` and `Pause` keys (which effectively
   **disables** them for any other purpose. As a matter of fact, the `Scroll Lock` light
   should no longer toggle while the software is running) to call on a function to increase
   or decrease the brightness.
2. Just like the nVidia control panel does (in `nvxdapix.dll` which is called by the driver
   and not the control panel app) we call on `SetDeviceGammaRamp()` with a ramp table.
3. This ramp table is computed **exactly** like the nVidia driver computes it, according to
   brightness integer between `80` and `120`, with `100` being the normal brightness value
   (hence why there are really only 20 brightness steps between darkest and normal).
4. For good measure, and so that the nVidia driver reapplies the brightness settings on
   startup/change of screen, we also update **all** the `BrightnessRed`/`BrightnessGreen`/
   `BrightnessBlue` registry values (Registry Keys `3538946`/`3538947`/`3538948`) under
   `HKEY_CURRENT_USER\Software\NVIDIA Corporation\Global\NVTweak\Devices\...`. This includes
   both the current monitor **and** every other monitor for which these values also exist.
   Note that the `XXXXXXXXXX-0` value nVidia uses for monitor devices is sadly different
   from any monitor ID that Windows or the nVidia API reports (it seems to be a private,
   internal ID, possibly the CRC32 of some string, that the driver uses, and that is a major
   PITA to try to reverse engineer), so we currently have no means to equate a specific
   registry entry to a specific monitor.
   Also note that, on startup, we do read from these values to try to guess what our last
   brightness was set to.
