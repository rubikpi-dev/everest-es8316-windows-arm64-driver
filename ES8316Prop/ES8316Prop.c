/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

/*++

Device Manager property page for the ES8316 codec. Stores per-device settings
through SetupAPI in the same hardware key used by WdfDeviceOpenRegistryKey.
Windows performs the PnP restart after Apply; no codec I/O runs in this DLL.

--*/

#include <windows.h>
#include <commctrl.h>
#include <setupapi.h>
#include <strsafe.h>
#include <wctype.h>
#include "resource.h"
#include "Settings.h"

typedef struct _STORED_VALUE { DWORD Value; BOOL Present; } STORED_VALUE;
typedef struct _PAGE_CONTEXT {
    LONG References;
    HDEVINFO DevInfo;
    SP_DEVINFO_DATA DevInfoData;
    STORED_VALUE Saved[SETTING_COUNT];
    DWORD Values[SETTING_COUNT];
    int Selected;
    BOOL Loading;
    BOOL Writable;
} PAGE_CONTEXT;

static HINSTANCE g_Instance;

static BOOL ParseNumber(PCWSTR text, DWORD *value)
{
    DWORD number = 0, base = 10, digit;
    BOOL haveDigit = FALSE;
    while (iswspace(*text)) ++text;
    if (text[0] == L'0' && (text[1] == L'x' || text[1] == L'X')) {
        base = 16;
        text += 2;
    }
    while (*text != L'\0' && !iswspace(*text)) {
        if (*text >= L'0' && *text <= L'9') digit = *text - L'0';
        else if (*text >= L'a' && *text <= L'f') digit = *text - L'a' + 10;
        else if (*text >= L'A' && *text <= L'F') digit = *text - L'A' + 10;
        else return FALSE;
        if (digit >= base || number > (MAXDWORD - digit) / base) return FALSE;
        number = number * base + digit;
        haveDigit = TRUE;
        ++text;
    }
    while (iswspace(*text)) ++text;
    if (!haveDigit || *text != L'\0') return FALSE;
    *value = number;
    return TRUE;
}

static BOOL ValidValue(UINT index, DWORD value)
{
    const SETTING *setting = &Settings[index];
    UINT i;
    if (setting->Choices != NULL) {
        for (i = 0; i < setting->ChoiceCount; ++i)
            if (setting->Choices[i].Value == value) return TRUE;
        return FALSE;
    }
    return value >= setting->Minimum && value <= setting->Maximum;
}

static BOOL ValidClockPair(DWORD clock, DWORD rate)
{
    static const DWORD ratios[] = {256, 384, 400, 500, 512, 768, 1024};
    UINT i;
    if (clock == 0 || rate == 0) return FALSE;
    for (i = 0; i < ARRAYSIZE(ratios); ++i) {
        if (clock % ratios[i] == 0 && clock / ratios[i] == rate) return TRUE;
        if (clock % 2 == 0 && (clock / 2) % ratios[i] == 0 &&
            (clock / 2) / ratios[i] == rate) return TRUE;
    }
    return FALSE;
}

static void FormatValue(UINT index, DWORD value, PWSTR text, size_t count)
{
    const SETTING *setting = &Settings[index];
    UINT i;
    for (i = 0; i < setting->ChoiceCount; ++i) {
        if (setting->Choices[i].Value == value) {
            StringCchPrintfW(text, count, L"%s (%lu)", setting->Choices[i].Label, value);
            return;
        }
    }
    StringCchPrintfW(text, count, setting->Hex ? L"0x%lX" : L"%lu", value);
}

static LSTATUS ReadValue(HKEY key, UINT index, STORED_VALUE *value)
{
    DWORD type = 0, bytes = sizeof(value->Value);
    LSTATUS status;
    value->Value = Settings[index].DefaultValue;
    value->Present = FALSE;
    status = RegQueryValueExW(key, Settings[index].Name, NULL, &type,
                            (BYTE *)&value->Value, &bytes);
    if (status == ERROR_FILE_NOT_FOUND) {
        value->Value = Settings[index].DefaultValue;
        return ERROR_SUCCESS;
    }
    if (status != ERROR_SUCCESS) return status;
    if (type != REG_DWORD || bytes != sizeof(DWORD)) return ERROR_INVALID_DATA;
    value->Present = TRUE;
    return ERROR_SUCCESS;
}

static BOOL HasChanges(const PAGE_CONTEXT *ctx)
{
    UINT i;
    for (i = 0; i < SETTING_COUNT; ++i)
        if (ctx->Values[i] != ctx->Saved[i].Value) return TRUE;
    return FALSE;
}

static DWORD RequestRestart(PAGE_CONTEXT *ctx)
{
    SP_DEVINSTALL_PARAMS_W params;
    ZeroMemory(&params, sizeof(params));
    params.cbSize = sizeof(params);
    if (!SetupDiGetDeviceInstallParamsW(ctx->DevInfo, &ctx->DevInfoData, &params))
        return GetLastError();
    params.FlagsEx |= DI_FLAGSEX_PROPCHANGE_PENDING;
    if (!SetupDiSetDeviceInstallParamsW(ctx->DevInfo, &ctx->DevInfoData, &params))
        return GetLastError();
    return ERROR_SUCCESS;
}

static DWORD SaveSettings(HKEY key, PAGE_CONTEXT *ctx,
                          DWORD (*restart)(PAGE_CONTEXT *), BOOL *rollbackFailed)
{
    STORED_VALUE current;
    BOOL written[SETTING_COUNT] = {FALSE};
    DWORD error = ERROR_SUCCESS;
    LSTATUS status;
    UINT i;
    *rollbackFailed = FALSE;
    if (!HasChanges(ctx)) return ERROR_SUCCESS;
    for (i = 0; i < SETTING_COUNT; ++i) {
        if (ctx->Values[i] != ctx->Saved[i].Value && !ValidValue(i, ctx->Values[i]))
            return ERROR_INVALID_DATA;
        status = ReadValue(key, i, &current);
        if (status != ERROR_SUCCESS) return (DWORD)status;
        if (current.Value != ctx->Saved[i].Value || current.Present != ctx->Saved[i].Present)
            return ERROR_REVISION_MISMATCH;
    }
    if ((ctx->Values[SettingMclkHz] != ctx->Saved[SettingMclkHz].Value ||
         ctx->Values[SettingSampleRate] != ctx->Saved[SettingSampleRate].Value) &&
        !ValidClockPair(ctx->Values[SettingMclkHz], ctx->Values[SettingSampleRate]))
        return ERROR_INVALID_DATA;

    for (i = 0; i < SETTING_COUNT; ++i) {
        if (ctx->Values[i] == ctx->Saved[i].Value) continue;
        status = RegSetValueExW(key, Settings[i].Name, 0, REG_DWORD,
                               (const BYTE *)&ctx->Values[i], sizeof(DWORD));
        if (status != ERROR_SUCCESS) { error = (DWORD)status; break; }
        written[i] = TRUE;
    }
    if (error == ERROR_SUCCESS) error = restart(ctx);
    if (error != ERROR_SUCCESS) {
        for (i = 0; i < SETTING_COUNT; ++i) {
            if (!written[i]) continue;
            if (ctx->Saved[i].Present)
                status = RegSetValueExW(key, Settings[i].Name, 0, REG_DWORD,
                                       (const BYTE *)&ctx->Saved[i].Value, sizeof(DWORD));
            else
                status = RegDeleteValueW(key, Settings[i].Name);
            if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
                *rollbackFailed = TRUE;
        }
        return error;
    }
    for (i = 0; i < SETTING_COUNT; ++i) {
        if (written[i]) {
            ctx->Saved[i].Value = ctx->Values[i];
            ctx->Saved[i].Present = TRUE;
        }
    }
    return ERROR_SUCCESS;
}

static void ShowError(HWND dialog, PCWSTR action, DWORD error)
{
    WCHAR systemText[256] = L"", message[700];
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, error, 0, systemText, ARRAYSIZE(systemText), NULL);
    StringCchPrintfW(message, ARRAYSIZE(message), L"%s\n\nError %lu: %s",
                     action, error, systemText);
    MessageBoxW(dialog, message, L"ES8316 codec settings", MB_OK | MB_ICONERROR);
}

static void ShowSetting(HWND dialog, PAGE_CONTEXT *ctx)
{
    UINT index = (UINT)ctx->Selected, i;
    const SETTING *setting = &Settings[index];
    HWND combo = GetDlgItem(dialog, IDC_CHOICE);
    WCHAR text[160], valueText[80];
    int selected = -1;
    ctx->Loading = TRUE;
    ShowWindow(GetDlgItem(dialog, IDC_VALUE), setting->Choices == NULL ? SW_SHOW : SW_HIDE);
    ShowWindow(combo, setting->Choices != NULL ? SW_SHOW : SW_HIDE);
    EnableWindow(GetDlgItem(dialog, IDC_VALUE), ctx->Writable);
    EnableWindow(combo, ctx->Writable);
    EnableWindow(GetDlgItem(dialog, IDC_DEFAULT), ctx->Writable);
    if (setting->Choices != NULL) {
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        for (i = 0; i < setting->ChoiceCount; ++i) {
            FormatValue(index, setting->Choices[i].Value, text, ARRAYSIZE(text));
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text);
            if (setting->Choices[i].Value == ctx->Values[index]) selected = (int)i;
        }

        if (selected == -1) {
            StringCchPrintfW(text, ARRAYSIZE(text), L"Existing value: %lu (unsupported)", ctx->Values[index]);
            selected = (int)SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text);
        }
        SendMessageW(combo, CB_SETCURSEL, (WPARAM)selected, 0);
    } else {
        FormatValue(index, ctx->Values[index], text, ARRAYSIZE(text));
        SetDlgItemTextW(dialog, IDC_VALUE, text);
    }
    FormatValue(index, setting->DefaultValue, valueText, ARRAYSIZE(valueText));
    StringCchPrintfW(text, ARRAYSIZE(text), L"%s\nDefault: %s", setting->Name, valueText);
    SetDlgItemTextW(dialog, IDC_DEFAULT_TEXT, text);
    SetDlgItemTextW(dialog, IDC_HELP_TEXT, setting->Help);
    ctx->Loading = FALSE;
}

static BOOL CaptureSetting(HWND dialog, PAGE_CONTEXT *ctx)
{
    UINT index = (UINT)ctx->Selected;
    const SETTING *setting = &Settings[index];
    WCHAR text[80], message[260];
    DWORD value = 0;
    BOOL valid;
    if (!ctx->Writable) return TRUE;
    if (setting->Choices != NULL) {
        LRESULT selected = SendDlgItemMessageW(dialog, IDC_CHOICE, CB_GETCURSEL, 0, 0);
        if (selected >= 0 && (UINT)selected < setting->ChoiceCount)
            value = setting->Choices[selected].Value;
        else if (selected == (LRESULT)setting->ChoiceCount)
            value = ctx->Values[index];
        else return FALSE;
        valid = TRUE;
    } else {
        GetDlgItemTextW(dialog, IDC_VALUE, text, ARRAYSIZE(text));
        valid = ParseNumber(text, &value);
    }
    if (!valid || (value != ctx->Saved[index].Value && !ValidValue(index, value))) {
        StringCchPrintfW(message, ARRAYSIZE(message),
                         L"Enter a whole number from %lu to %lu for %s. Decimal and 0x hexadecimal are accepted.",
                         setting->Minimum, setting->Maximum, setting->Name);
        MessageBoxW(dialog, message, L"Invalid codec setting", MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(dialog, IDC_VALUE));
        return FALSE;
    }
    ctx->Values[index] = value;
    return TRUE;
}

static void LoadSettings(HWND dialog, PAGE_CONTEXT *ctx)
{
    HKEY key;
    UINT i;
    DWORD error = ERROR_SUCCESS;
    ctx->Loading = TRUE;
    ctx->Writable = FALSE;
    key = SetupDiOpenDevRegKey(ctx->DevInfo, &ctx->DevInfoData, DICS_FLAG_GLOBAL,
                               0, DIREG_DEV, KEY_QUERY_VALUE);
    if (key == INVALID_HANDLE_VALUE) error = GetLastError();
    for (i = 0; i < SETTING_COUNT; ++i) {
        ctx->Saved[i].Value = Settings[i].DefaultValue;
        if (error == ERROR_SUCCESS) error = (DWORD)ReadValue(key, i, &ctx->Saved[i]);
        ctx->Values[i] = ctx->Saved[i].Value;
        SendDlgItemMessageW(dialog, IDC_SETTINGS, LB_ADDSTRING, 0, (LPARAM)Settings[i].Label);
    }
    if (key != INVALID_HANDLE_VALUE) RegCloseKey(key);
    if (error == ERROR_SUCCESS) {
        key = SetupDiOpenDevRegKey(ctx->DevInfo, &ctx->DevInfoData, DICS_FLAG_GLOBAL,
                                   0, DIREG_DEV, KEY_QUERY_VALUE | KEY_SET_VALUE);
        if (key != INVALID_HANDLE_VALUE) {
            ctx->Writable = TRUE;
            RegCloseKey(key);
        }
        SetDlgItemTextW(dialog, IDC_ACCESS, ctx->Writable ?
            L"Saved settings; changes take effect after restart." :
            L"Read only. Reopen Device Manager as administrator to edit.");
    } else {
        SetDlgItemTextW(dialog, IDC_ACCESS, L"Settings could not be read. Editing is disabled.");
        ShowError(dialog, L"The saved configuration could not be read. Displayed defaults may not reflect the device.", error);
    }
    ctx->Selected = 0;
    SendDlgItemMessageW(dialog, IDC_VALUE, EM_SETLIMITTEXT, 32, 0);
    SendDlgItemMessageW(dialog, IDC_SETTINGS, LB_SETCURSEL, 0, 0);
    ShowSetting(dialog, ctx);
}

static BOOL ApplySettings(HWND dialog, PAGE_CONTEXT *ctx)
{
    HKEY key;
    DWORD error;
    BOOL rollbackFailed;
    if (!CaptureSetting(dialog, ctx)) return FALSE;
    if (!HasChanges(ctx)) return TRUE;
    if (!ctx->Writable) return FALSE;
    if ((ctx->Values[SettingMclkHz] != ctx->Saved[SettingMclkHz].Value ||
         ctx->Values[SettingSampleRate] != ctx->Saved[SettingSampleRate].Value) &&
        !ValidClockPair(ctx->Values[SettingMclkHz], ctx->Values[SettingSampleRate])) {
        MessageBoxW(dialog, L"MCLK and sample rate do not form a supported clock ratio. Adjust both settings before applying. See the sample rate description.",
                    L"Invalid codec clock settings", MB_OK | MB_ICONWARNING);
        return FALSE;
    }
    key = SetupDiOpenDevRegKey(ctx->DevInfo, &ctx->DevInfoData, DICS_FLAG_GLOBAL,
                               0, DIREG_DEV, KEY_QUERY_VALUE | KEY_SET_VALUE);
    if (key == INVALID_HANDLE_VALUE) {
        ShowError(dialog, L"Settings could not be saved. Reopen Device Manager as administrator if access is denied.", GetLastError());
        return FALSE;
    }
    error = SaveSettings(key, ctx, RequestRestart, &rollbackFailed);
    RegCloseKey(key);
    if (error != ERROR_SUCCESS) {
        ShowError(dialog, rollbackFailed ?
            L"Saving failed and some previous settings could not be restored. Close and reopen this page to inspect the saved configuration before restarting." :
            (error == ERROR_REVISION_MISMATCH ?
            L"Settings changed outside this page. Nothing was saved. Close and reopen this page, then try again." :
            L"Settings could not be applied. Previous settings were retained. Correct the error and try again."), error);
        if (rollbackFailed) {
            ctx->Writable = FALSE;
            ShowSetting(dialog, ctx);
            SetDlgItemTextW(dialog, IDC_ACCESS, L"Save failed. Close and reopen to reload settings.");
        }
        return FALSE;
    }
    PropSheet_UnChanged(GetParent(dialog), dialog);
    SetDlgItemTextW(dialog, IDC_ACCESS, L"Saved. Close Properties to complete the restart request.");
    return TRUE;
}

static INT_PTR CALLBACK PageDialog(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    PAGE_CONTEXT *ctx = (PAGE_CONTEXT *)GetWindowLongPtrW(dialog, DWLP_USER);
    switch (message) {
    case WM_INITDIALOG:
        ctx = (PAGE_CONTEXT *)((LPPROPSHEETPAGEW)lParam)->lParam;
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)ctx);
        LoadSettings(dialog, ctx);
        return TRUE;
    case WM_COMMAND:
        if (ctx == NULL || ctx->Loading) break;
        if (LOWORD(wParam) == IDC_SETTINGS && HIWORD(wParam) == LBN_SELCHANGE) {
            int selected = (int)SendDlgItemMessageW(dialog, IDC_SETTINGS, LB_GETCURSEL, 0, 0);
            if (selected >= 0 && selected < (int)SETTING_COUNT && CaptureSetting(dialog, ctx)) {
                ctx->Selected = selected;
                ShowSetting(dialog, ctx);
            } else {
                SendDlgItemMessageW(dialog, IDC_SETTINGS, LB_SETCURSEL, (WPARAM)ctx->Selected, 0);
            }
            return TRUE;
        }
        if (!ctx->Writable) break;
        if (LOWORD(wParam) == IDC_DEFAULT && HIWORD(wParam) == BN_CLICKED) {
            ctx->Values[ctx->Selected] = Settings[ctx->Selected].DefaultValue;
            ShowSetting(dialog, ctx);
            PropSheet_Changed(GetParent(dialog), dialog);
            return TRUE;
        }
        if ((LOWORD(wParam) == IDC_VALUE && HIWORD(wParam) == EN_CHANGE) ||
            (LOWORD(wParam) == IDC_CHOICE && HIWORD(wParam) == CBN_SELCHANGE)) {
            PropSheet_Changed(GetParent(dialog), dialog);
            return TRUE;
        }
        break;
    case WM_NOTIFY:
        if (ctx == NULL) break;
        switch (((LPNMHDR)lParam)->code) {
        case PSN_APPLY:
            SetWindowLongPtrW(dialog, DWLP_MSGRESULT,
                ApplySettings(dialog, ctx) ? PSNRET_NOERROR : PSNRET_INVALID_NOCHANGEPAGE);
            return TRUE;
        case PSN_KILLACTIVE:
            SetWindowLongPtrW(dialog, DWLP_MSGRESULT, !CaptureSetting(dialog, ctx));
            return TRUE;
        case PSN_RESET:

            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void ReleaseContext(PAGE_CONTEXT *ctx)
{
    if (InterlockedDecrement(&ctx->References) == 0) HeapFree(GetProcessHeap(), 0, ctx);
}

static UINT CALLBACK PageCallback(HWND window, UINT message, LPPROPSHEETPAGEW page)
{
    PAGE_CONTEXT *ctx = (PAGE_CONTEXT *)page->lParam;
    UNREFERENCED_PARAMETER(window);
    if (message == PSPCB_ADDREF) InterlockedIncrement(&ctx->References);
    if (message == PSPCB_RELEASE) ReleaseContext(ctx);
    return 1;
}

BOOL WINAPI ES8316PropPageProvider(PSP_PROPSHEETPAGE_REQUEST request,
                                  LPFNADDPROPSHEETPAGE addPage, LPARAM parameter)
{
    PAGE_CONTEXT *ctx;
    PROPSHEETPAGEW page;
    HPROPSHEETPAGE handle;
    if (request == NULL || request->cbSize < sizeof(*request) || addPage == NULL ||
        request->PageRequested != SPPSR_ENUM_ADV_DEVICE_PROPERTIES ||
        request->DeviceInfoData == NULL) return FALSE;
    ctx = (PAGE_CONTEXT *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*ctx));
    if (ctx == NULL) return FALSE;
    ctx->References = 1;
    ctx->DevInfo = request->DeviceInfoSet;
    ctx->DevInfoData = *request->DeviceInfoData;
    ZeroMemory(&page, sizeof(page));
    page.dwSize = sizeof(page);
    page.dwFlags = PSP_USECALLBACK;
    page.hInstance = g_Instance;
    page.pszTemplate = MAKEINTRESOURCEW(IDD_SETTINGS);
    page.pfnDlgProc = PageDialog;
    page.pfnCallback = PageCallback;
    page.lParam = (LPARAM)ctx;
    handle = CreatePropertySheetPageW(&page);
    ReleaseContext(ctx);
    if (handle == NULL) return FALSE;
    if (!addPage(handle, parameter)) {
        DestroyPropertySheetPage(handle);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    UNREFERENCED_PARAMETER(reserved);
    if (reason == DLL_PROCESS_ATTACH) g_Instance = instance;
    return TRUE;
}
