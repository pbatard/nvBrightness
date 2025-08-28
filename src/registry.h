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
static __inline DWORD GetRegistryKey(HKEY key_root, const wchar_t* key_name, DWORD reg_type,
	LPBYTE dest, DWORD dest_size)
{
	wchar_t long_key_name[MAX_PATH] = { 0 };
	DWORD r = 0;
	size_t i;
	LONG s;
	HKEY hSoftware = NULL, hApp = NULL;
	DWORD dwDisp, dwType = -1, dwSize = dest_size;

	if (dest != NULL)
		memset(dest, 0, dest_size);

	if (key_name == NULL || COMPANY_NAME == NULL || COMPANY_NAME[0] == L'\0' ||
		APPLICATION_NAME == NULL || APPLICATION_NAME[0] == L'\0')
		return 0;

	for (i = wcslen(key_name); i > 0 && key_name[i] != L'\\'; i--);

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
	if ((s == ERROR_FILE_NOT_FOUND) || ((s == ERROR_SUCCESS) && (dwType == reg_type)))
		r = dwSize;
out:
	if (hSoftware != NULL)
		RegCloseKey(hSoftware);
	if (hApp != NULL)
		RegCloseKey(hApp);
	return r;
}
#define GetRegistryKeySize(root, key, type) GetRegistryKey(root, key, type, NULL, 0)

/* Write a generic registry key value (create the key if it doesn't exist) */
static __inline BOOL SetRegistryKey(HKEY key_root, const wchar_t* key_name, DWORD reg_type, LPBYTE src, DWORD src_size)
{
	wchar_t long_key_name[MAX_PATH] = { 0 };
	BOOL r = FALSE;
	size_t i;
	HKEY hSoftware = NULL, hApp = NULL;
	DWORD dwDisp, dwType = reg_type;

	if (key_name == NULL || COMPANY_NAME == NULL || COMPANY_NAME[0] == L'\0' ||
		APPLICATION_NAME == NULL || APPLICATION_NAME[0] == L'\0')
		return FALSE;
	// MSVC's static analyzer is dumb and needs BOTH conditions
	if (key_root == NULL || key_root != HKEY_CURRENT_USER)
		return FALSE;

	for (i = wcslen(key_name); i > 0 && key_name[i] != L'\\'; i--);

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

/* Delete a registry value */
static __inline BOOL DeleteRegistryValue(HKEY key_root, const wchar_t* key_name)
{
	wchar_t long_key_name[MAX_PATH] = { 0 };
	BOOL r = FALSE;
	HKEY hApp = NULL;
	LONG s;
	size_t i;

	if (key_name == NULL || key_root == NULL || COMPANY_NAME == NULL || APPLICATION_NAME == NULL)
		return FALSE;
	if (key_root != HKEY_CURRENT_USER)
		return FALSE;

	for (i = wcslen(key_name); i > 0 && key_name[i] != L'\\'; i--);

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
		wsprintf(key_base, L"SOFTWARE\\%s\\%s", COMPANY_NAME, APPLICATION_NAME);
		if (RegOpenKeyEx(key_root, key_base, 0, KEY_READ | KEY_WRITE, &hApp) != ERROR_SUCCESS) {
			hApp = NULL;
			goto out;
		}
	}

	s = RegDeleteValue(hApp, &key_name[i]);
	r = ((s == ERROR_SUCCESS) || (s == ERROR_FILE_NOT_FOUND));

out:
	if (hApp != NULL)
		RegCloseKey(hApp);
	return r;
}

/* Helpers for 64 bit registry operations */
#define GetRegistryKey64(root, key, pval) GetRegistryKey(root, key, REG_QWORD, (LPBYTE)pval, sizeof(LONGLONG))
#define SetRegistryKey64(root, key, val) SetRegistryKey(root, key, REG_QWORD, (LPBYTE)&val, sizeof(LONGLONG))
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
#define GetRegistryKey32(root, key, pval) GetRegistryKey(root, key, REG_DWORD, (LPBYTE)pval, sizeof(DWORD))
#define SetRegistryKey32(root, key, val) SetRegistryKey(root, key, REG_DWORD, (LPBYTE)&val, sizeof(DWORD))
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
#define GetRegistryKeyStr(root, key, str, len) GetRegistryKey(root, key, REG_SZ, (LPBYTE)str, (DWORD)len)
#define SetRegistryKeyStr(root, key, str) SetRegistryKey(root, key, REG_SZ, (LPBYTE)str, (DWORD)wcslen(str) * sizeof(WCHAR))
// Use a static buffer - don't allocate
static __inline wchar_t* ReadRegistryKeyStr(HKEY root, const wchar_t* key) {
	static wchar_t str[512 + 1];
	str[0] = 0;
	GetRegistryKey(root, key, REG_SZ, (LPBYTE)str, (DWORD)sizeof(str));
	str[ARRAYSIZE(str) - 1] = 0;
	return str;
}
#define WriteRegistryKeyStr SetRegistryKeyStr

/* Helpers for Multi-String registry operations */
#define GetRegistryKeyMultiStr(root, key, multi_str, len) GetRegistryKey(root, key, REG_MULTI_SZ, (LPBYTE)multi_str, (DWORD)len)
#define SetRegistryKeyMultiStr(root, key, multi_str, len) SetRegistryKey(root, key, REG_MULTI_SZ, (LPBYTE)multi_str, (DWORD)len)
// Use a static buffer - don't allocate
static __inline wchar_t* ReadRegistryKeyMultiStr(HKEY root, const wchar_t* key) {
	static wchar_t multi_str[512 + 2];
	multi_str[0] = 0;
	multi_str[1] = 0;
	GetRegistryKey(root, key, REG_SZ, (LPBYTE)multi_str, (DWORD)sizeof(multi_str));
	multi_str[ARRAYSIZE(multi_str) - 1] = 0;
	multi_str[ARRAYSIZE(multi_str) - 2] = 0;
	return multi_str;
}
static __inline void WriteRegistryKeyMultiStr(HKEY root, const wchar_t* key, const wchar_t* val) {
	size_t len;
	for (len = 0; val[len] != 0; len += wcslen(&val[len]) + 1);
	SetRegistryKeyMultiStr(root, key, val, (len + 1) * sizeof(wchar_t));
}

#ifdef __cplusplus
}
#endif
