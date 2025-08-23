/*
 * nvBrightness - nVidia Control Panel brightness at your fingertips
 *
 * Copyright © 2025 Pete Batard <pete@akeo.ie>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

// Using a GUID for the tray icon allows the executable to be moved around or change version
// without requiring the user to go through the taskbar settings to re-enable the icon.
#ifdef _DEBUG
// Note that we use two GUIDs due to a Microsoft UTTERLY MADDENING BUG, where Windows silently
// drops the tray icon, for some arcane reason that I really am NOT ready to waste DAYS on.
// And it only happens to some versions of the executable, independently of their location and
// REGARDLESS OF WHETHER THEY WERE RECOMPILED FROM THE SAME SOURCE AS UNAFFECTED EXECUTABLES!!!
// So, Microsoft, you can go screw yourselves on that one, as I have better things to do than
// chase after a stupid Windows bug/limitation/whatever.
#define TRAY_ICON_GUID { 0x64397973, 0xa694, 0x4640, { 0x80, 0x26, 0x96, 0x04, 0x46, 0x75, 0x00, 0x2c } }
#else
#define TRAY_ICON_GUID { 0x64397973, 0xa694, 0x4640, { 0x80, 0x26, 0x96, 0x04, 0x46, 0x75, 0x00, 0x2b } }
#endif

// VCP control code to read/switch a monitor's input source
#define VCP_INPUT_SOURCE            0x60

// Custom VCP input values for previous/next
#define VCP_INPUT_HOME              0x00
#define VCP_INPUT_PREVIOUS          0xfe
#define VCP_INPUT_NEXT              0xff

// How long we may retry GetCapabilitiesStringLength(), in seconds
#define VCP_CAPS_MAX_RETRY_TIME     300

// nVidia Color data definitions
#define NV_COLOR_REGISTRY_INDEX     3538946

enum {
	nvColorRed = 0,
	nvColorGreen,
	nvColorBlue,
	nvColorMax
};

enum {
	nvAttrBrightness = 0,
	nvAttrContrast,
	nvAttrGamma,
	nvAttrMax
};

enum {
	hkDecreaseBrightness = 0,
	hkIncreaseBrightness,
	hkDecreaseBrightness2,
	hkIncreaseBrightness2,
	hkPowerOffMonitor,
	hkRestoreInput,
	hkPreviousInput,
	hkNextInput,
	hkRegisterHotkeys,
	hkMax
};

extern void logger(const char* format, ...);
