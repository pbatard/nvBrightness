/*
 * nvBrightness - nVidia Control Panel brightness at your fingertips
 * Registry access
 * Copyright © 2012-2025 Pete Batard <pete@akeo.ie>
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
#include <windows.h>
#include <stdint.h>

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

extern wchar_t *APPLICATION_NAME, *COMPANY_NAME;

/*
 * Read a generic registry key value. If a short key_name is used, assume that
 * it belongs to the application and create the app subkey if required
 */
static __inline BOOL _GetRegistryKey(HKEY key_root, const wchar_t* key_name, DWORD reg_type,
	LPBYTE dest, DWORD dest_size)
{
	wchar_t long_key_name[MAX_PATH] = { 0 };
	BOOL r = FALSE;
	size_t i;
	LONG s;
	HKEY hSoftware = NULL, hApp = NULL;
	DWORD dwDisp, dwType = -1, dwSize = dest_size;

	memset(dest, 0, dest_size);

	if (key_name == NULL || COMPANY_NAME == NULL || APPLICATION_NAME == NULL)
		return FALSE;

	for (i = wcslen(key_name); i > 0; i--) {
		if (key_name[i] == '\\')
			break;
	}

	if (i > 0) {
		if (i >= ARRAYSIZE(long_key_name))
			return FALSE;
		wcscpy_s(long_key_name, ARRAYSIZE(long_key_name), key_name);
		long_key_name[i] = 0;
		i++;
		if (RegOpenKeyEx(key_root, long_key_name, 0, KEY_READ, &hApp) != ERROR_SUCCESS) {
			hApp = NULL;
			goto out;
		}
	} else {
		wchar_t key_base[128];
		wsprintf(key_base, L"%s\\%s", COMPANY_NAME, APPLICATION_NAME);
		if (RegOpenKeyEx(key_root, L"SOFTWARE", 0, KEY_READ|KEY_CREATE_SUB_KEY, &hSoftware) != ERROR_SUCCESS) {
			hSoftware = NULL;
			goto out;
		}
		if (RegCreateKeyEx(hSoftware, key_base, 0, NULL, 0,
			KEY_SET_VALUE | KEY_QUERY_VALUE | KEY_CREATE_SUB_KEY, NULL, &hApp, &dwDisp) != ERROR_SUCCESS) {
			hApp = NULL;
			goto out;
		}
	}

	s = RegQueryValueEx(hApp, &key_name[i], NULL, &dwType, (LPBYTE)dest, &dwSize);
	// No key means default value of 0 or empty string
	if ((s == ERROR_FILE_NOT_FOUND) || ((s == ERROR_SUCCESS) && (dwType == reg_type) && (dwSize > 0))) {
		r = TRUE;
	}
out:
	if (hSoftware != NULL)
		RegCloseKey(hSoftware);
	if (hApp != NULL)
		RegCloseKey(hApp);
	return r;
}

/* Write a generic registry key value (create the key if it doesn't exist) */
static __inline BOOL _SetRegistryKey(HKEY key_root, const wchar_t* key_name, DWORD reg_type, LPBYTE src, DWORD src_size)
{
	wchar_t long_key_name[MAX_PATH] = { 0 };
	BOOL r = FALSE;
	size_t i;
	HKEY hSoftware = NULL, hApp = NULL;
	DWORD dwDisp, dwType = reg_type;

	if (key_name == NULL || key_root == NULL || COMPANY_NAME == NULL || APPLICATION_NAME == NULL)
		return FALSE;
	if (key_root != HKEY_CURRENT_USER)
		return FALSE;

	for (i = wcslen(key_name); i > 0; i--) {
		if (key_name[i] == '\\')
			break;
	}

	if (i > 0) {
		if (i >= ARRAYSIZE(long_key_name))
			return FALSE;
		wcscpy_s(long_key_name, ARRAYSIZE(long_key_name), key_name);
		long_key_name[i] = 0;
		i++;
		if (RegOpenKeyEx(key_root, long_key_name, 0, KEY_READ | KEY_WRITE, &hApp) != ERROR_SUCCESS) {
			hApp = NULL;
			goto out;
		}
	} else {
		wchar_t key_base[128];
		wsprintf(key_base, L"%s\\%s", COMPANY_NAME, APPLICATION_NAME);
		if (RegOpenKeyEx(key_root, L"SOFTWARE", 0, KEY_READ | KEY_WRITE | KEY_CREATE_SUB_KEY, &hSoftware) != ERROR_SUCCESS) {
			hSoftware = NULL;
			goto out;
		}
		if (RegCreateKeyEx(hSoftware, key_base, 0, NULL, 0,
			KEY_SET_VALUE | KEY_QUERY_VALUE | KEY_CREATE_SUB_KEY, NULL, &hApp, &dwDisp) != ERROR_SUCCESS) {
			hApp = NULL;
			goto out;
		}
	}

	r = (RegSetValueExW(hApp, &key_name[i], NULL, dwType, src, src_size) == ERROR_SUCCESS);

out:
	if (hSoftware != NULL)
		RegCloseKey(hSoftware);
	if (hApp != NULL)
		RegCloseKey(hApp);
	return r;
}

/* Helpers for 64 bit registry operations */
#define GetRegistryKey64(root, key, pval) _GetRegistryKey(root, key, REG_QWORD, (LPBYTE)pval, sizeof(LONGLONG))
#define SetRegistryKey64(root, key, val) _SetRegistryKey(root, key, REG_QWORD, (LPBYTE)&val, sizeof(LONGLONG))
// Check that a key is accessible for R/W (will create a key if not already existing)
static __inline BOOL CheckRegistryKey64(HKEY root, const wchar_t* key) {
	LONGLONG val;
	return GetRegistryKey64(root, key, &val);
}
static __inline int64_t ReadRegistryKey64(HKEY root, const wchar_t* key) {
	LONGLONG val;
	GetRegistryKey64(root, key, &val);
	return (int64_t)val;
}
static __inline BOOL WriteRegistryKey64(HKEY root, const wchar_t* key, int64_t val) {
	LONGLONG tmp = (LONGLONG)val;
	return SetRegistryKey64(root, key, tmp);
}

/* Helpers for 32 bit registry operations */
#define GetRegistryKey32(root, key, pval) _GetRegistryKey(root, key, REG_DWORD, (LPBYTE)pval, sizeof(DWORD))
#define SetRegistryKey32(root, key, val) _SetRegistryKey(root, key, REG_DWORD, (LPBYTE)&val, sizeof(DWORD))
static __inline BOOL CheckRegistryKey32(HKEY root, const wchar_t* key) {
	DWORD val;
	return (GetRegistryKey32(root, key, &val) && SetRegistryKey32(root, key, val));
}
static __inline int32_t ReadRegistryKey32(HKEY root, const wchar_t* key) {
	DWORD val;
	GetRegistryKey32(root, key, &val);
	return (int32_t)val;
}
static __inline BOOL WriteRegistryKey32(HKEY root, const wchar_t* key, int32_t val) {
	DWORD tmp = (DWORD)val;
	return SetRegistryKey32(root, key, tmp);
}

/* Helpers for boolean registry operations */
#define ReadRegistryKeyBool(root, key) (ReadRegistryKey32(root, key) != 0)
#define WriteRegistryKeyBool(root, key, b) WriteRegistryKey32(root, key, (b)?1:0)
#define CheckRegistryKeyBool CheckRegistryKey32

/* Helpers for String registry operations */
#define GetRegistryKeyStr(root, key, str, len) _GetRegistryKey(root, key, REG_SZ, (LPBYTE)str, (DWORD)len)
#define SetRegistryKeyStr(root, key, str) _SetRegistryKey(root, key, REG_SZ, (LPBYTE)str, (DWORD)wcslen(str))
// Use a static buffer - don't allocate
static __inline wchar_t* ReadRegistryKeyStr(HKEY root, const wchar_t* key) {
	static wchar_t str[512];
	str[0] = 0;
	_GetRegistryKey(root, key, REG_SZ, (LPBYTE)str, (DWORD)ARRAYSIZE(str)-1);
	return str;
}
#define WriteRegistryKeyStr SetRegistryKeyStr

#ifdef __cplusplus
}
#endif
