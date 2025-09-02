nvBrightness: nVidia Control Panel brightness at your fingertips
================================================================

[![Build status](https://img.shields.io/github/actions/workflow/status/pbatard/nvBrightness/vs2022.yml?style=flat-square)](https://github.com/pbatard/nvBrightness/actions/workflows/vs2022.yml)
[![Release](https://img.shields.io/github/release-pre/pbatard/nvBrightness.svg?style=flat-square)](https://github.com/pbatard/nvBrightness/releases)
[![Github stats](https://img.shields.io/github/downloads/pbatard/nvBrightness/total.svg?style=flat-square)](https://github.com/pbatard/nvBrightness/releases)
[![Licence](https://img.shields.io/badge/license-GPLv3-blue.svg?style=flat-square)](https://www.gnu.org/licenses/gpl-3.0.en.html)

![nvBrightness screenshot](https://raw.githubusercontent.com/pbatard/nvBrightness/master/icons/nvBrightness.png)


## Motivation

Because I recently got an OLED monitor that runs in HDR mode, and that I want to adjust the
brightness for quickly and easily. Other solutions, like Twinkle Tray's SDR brightness, do
not provide adjustments that look good compared to the nVidia's Control Panel brightness,
but the nVidia Control Panel takes **ages** to launch and nVidia, in their great wisdom,
also removed any automated/3rd-party means to control brightness in their modern drivers,
such as the one they used to provide through `nvcpl.dll`...

Since there are plenty of unused keyboard combinations that can be put to better use, I
might as well control brightness through those, using the exact brightness method that the
nVidia control panel uses.

In short, this is software *by me, for me, for my specific hardware and workflow*, which you
might also find useful for you, but which I am not planning to expand for your specific use.


## How it works

1. By default, we register the `Win`+`Shift`+`Num +` and `Win`+`Shift`+`Num -` keyboard
   shortcuts. Alternatively, if you choose so, the Internet navigation keys of your keyboard
   and `Alt`-`←` and  `Alt`-`→` are registered. Also, so that we can provide a shortcut for
   easy display poweroff, we register `Win`+`Shift`+`End`.
2. Just like the nVidia control panel does (though it does so somewhat indirectly in
   `nvxdapix.dll`'s `DesktopColorSettings::SetGammaRampEx()`) we call the **undocumented**
   `NvAPI_DISP_SetTargetGammaCorrection()` nVidia API with a ramp table.
3. This ramp table is computed **exactly** like the nVidia driver computes it, according to
   brightness integer between `80` and `120`, with `100` being the normal brightness value
   (hence why there are really only 20 brightness steps between darkest and normal with the
   nVidia control panel). Unlike nVidia however, we allow the use non-integer values, which
   means you can have as many steps as you want and get a much smoother experience.
4. For good measure, and so that the nVidia driver reapplies the brightness settings on
   startup/change of screen, we also update the relevant `BrightnessRed`/`BrightnessGreen`/
   `BrightnessBlue` registry values (Registry Keys `3538946`/`3538947`/`3538948`) under
   `HKEY_CURRENT_USER\Software\NVIDIA Corporation\Global\NVTweak\Devices\###########-0`.
   Note that the `###########-0` value associated to the display (which is **not** an nVidia
   `displayID`, because that would be too easy) is retrieved by calling the **undocumented**
   `NvAPI_SYS_GetLUIDFromDisplayID()` nVidia API.
   Also note that, on startup, we do read from these values to find what the current nVidia
   brightness was set to.
5. If your display supports it (not all do), we also provide means of switching/restoring
   inputs through [VCP](https://en.wikipedia.org/wiki/Monitor_Control_Command_Set), so that
   you can conveniently switch your monitor to a different source, through shortcuts:
   `Win`+`Shift`+`PageUp`, `Win`+`Shift`+`PageDn` and `Win`+`Shift`+`Home`. Note that, with
   some displays, VCP detection can take some time (multiple minutes), but once complete,
   you should see the name of the available inputs (e.g. "DP1", "HDMI2") in the _Inputs_
   _Control_ submenu.
6. If you have multiple displays, you can also switch the display shorcuts apply to (except
   for the `Win`+`Shift`+`End` one, that applies to **all** connected displays) by using
   `Win`+`Shift`+`,` and `Win`+`Shift`+`.`.


## Mini FAQ

### How comes 100% brightness in this software equates 50% in the nVidia control panel?

Because the output looks atrocious if you go above 50% in the control panel, so we do what
nVidia should have been doing and set our brightness range where it actually belongs.

### Why did you default to using numpad keys? What if I don't have a numpad!

We made this deliberate decision because most people who don't have a numpad would be using
laptops, where brightness controls are provided through different means. And again, this
software is designed for our usage workflow, where using the numpad keys makes the most
sense. Finally, if you don't have a numpad, you can always switch to using the internet keys
or `Alt`-`←` and  `Alt`-`→` shortcuts.

### What does "Wake up to Home" do?

**If** your display supports VCP, it attempts to restore the input on wake from sleep to
what is was before the computer was put in standby. This can be useful if you put your
computer to sleep, then switched to a different monitor input to access a different system
and, now that you are done with it, you want to restore to your main system's video output.
"Home" should be seen as the main active monitor input for your current system.

### Do you plan to ...?

Nope.

### But I didn't even finish my question!!

And I already told you above that I am not planning to expand this software for your specific
wishes. So if you want contrast control, alternate key mappings, more fine grained monitor
support and whatnot, you're going to have to ask elsewhere.


## Note to nVidia

I didn't spend 2 weeks reverse engineering your proprietary DLLs to be slapped with some
kind of NDA bullshit. I did **not** sign any NDA with you, did not somehow happen to gain
access to any of your NDA documentation, and **nobody** provided me any info or hints about
your undocumented APIs. This is good old-fashioned disassembling (with Ghidra), debugging
(with x64Dbg) and lengthy trial and error...

Oh, and I am an EU resident, where we have laws that prevent overreaching EULA terms, like
_"Thou shall not reverse engineer binaries that pertain to the hardware **YOU OWN**"_.

With the obscene amount of money I paid for nVidia branded GPUs, I believe that I am
entitled to the freedom of using them in whichever way I see fit, especially if it is to
compensate for the lack of care you appear to be displaying to regular (non AI) consumers.
This kind of **instant response** control for brightness, through customizable keyboard
shortcuts, **should** be provided by your control panel, instead of leaving us with its
current, clunky, mouse-only interface, that literally takes **10 whole seconds to launch!!!**

So, nVidia, if you are unhappy about this project, it's merely a reflection of the fact that
you should have done a better job.

Oh, and while I'm at it, and since it looks like you now have a whole business revolving
around _Predictive Interpolation_ (which is really what "AI" is, as there certainly is no
"intelligence" in what's going on under the hood) and it's time you (and others) realise
that you are on the wrong side of history when it comes to releasing closed source software,
let me give you a very interesting property of _Predictive Interpolation_ when it gets
applied to universal decompilation (which **will** happen, even though folks like yourself,
Microsoft, Meta and Apple obviously have a vested interest in not being the ones providing
it): As opposed to text/image/audio generation, that always ends up polluting the data
source set engines are generated from, decompilation **CANNOT** do the same, because, just
like mathematical proof, the generated code can be formally validated to produce the
expected source binary, and if not, can be eliminated from the dataset.
Which means that one can rely on an ever expanding **unpolluted** dataset of source → binary
code, that can be used for _Predictive Interpolation_ decompilation.

The days of closed source code (and related obnoxious NDAs) are numbered and whether you
like it or not, you will either have to open source ALL of your binaries, or somebody else,
who might not be a "somebody" at all (but who might ironically be using nVidia hardware to
do so) will do that for you...
