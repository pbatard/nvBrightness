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

#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

static void ChangeBrightness(bool bIncrease)
{
	OutputDebugStringA(bIncrease ? "INCREASE\r\n" : "DECREASE\r\n");
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION && lParam != 0) {
		KBDLLHOOKSTRUCT* kbhs = (KBDLLHOOKSTRUCT*)lParam;

#ifndef USE_INTERNET_KEYS
		if ((kbhs->vkCode == VK_SCROLL || kbhs->vkCode == VK_PAUSE) && (kbhs->flags == 0x0)) {
			ChangeBrightness((kbhs->vkCode == VK_PAUSE));
			return 1;
		}
#else
		// Alternate way, that uses the Internet navigation keys
		if ((kbhs->vkCode == VK_LEFT || kbhs->vkCode == VK_RIGHT) && ((kbhs->flags & 0xA1) == 0x21)) {
			ChangeBrightness((kbhs->vkCode == VK_RIGHT));
			return 1;
		}
#endif
	}

	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
	HHOOK hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(0), 0);
	MessageBox(NULL, L"HOOK RUNNING!", L"", MB_ICONEXCLAMATION | MB_SYSTEMMODAL);
	UnhookWindowsHookEx(hHook);
	return 0;
}
