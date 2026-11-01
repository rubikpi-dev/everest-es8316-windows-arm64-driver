/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

/* Host regression tests. All registry/PnP operations below use an in-memory
 * device; no real registry, installed device or codec is modified. */
#include <windows.h>
#include <commctrl.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../Settings.h"
#include "../../ES8316/Tuning.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); exit(1); } } while (0)

static DWORD TestValues[SETTING_COUNT];
static BOOL TestPresent[SETTING_COUNT];
static UINT Writes, Deletes, Restarts, Errors, Frees;
static UINT FailWrite;
static BOOL DenyWrite, FailRestart;
static SP_DEVINSTALL_PARAMS_W TestParams;

static UINT FindSetting(PCWSTR name)
{
    UINT i;
    for (i = 0; i < SETTING_COUNT; ++i)
        if (wcscmp(name, Settings[i].Name) == 0) return i;
    CHECK(FALSE);
    return 0;
}
static LSTATUS WINAPI TestQuery(HKEY key, LPCWSTR name, LPDWORD reserved,
                                LPDWORD type, LPBYTE data, LPDWORD bytes)
{
    UINT index = FindSetting(name);
    UNREFERENCED_PARAMETER(key); UNREFERENCED_PARAMETER(reserved);
    if (!TestPresent[index]) return ERROR_FILE_NOT_FOUND;
    CHECK(*bytes >= sizeof(DWORD));
    *type = REG_DWORD; *bytes = sizeof(DWORD);
    memcpy(data, &TestValues[index], sizeof(DWORD));
    return ERROR_SUCCESS;
}
static LSTATUS WINAPI TestSet(HKEY key, LPCWSTR name, DWORD reserved, DWORD type,
                              const BYTE *data, DWORD bytes)
{
    UINT index = FindSetting(name);
    UNREFERENCED_PARAMETER(key); UNREFERENCED_PARAMETER(reserved);
    CHECK(type == REG_DWORD && bytes == sizeof(DWORD));
    ++Writes;
    if (DenyWrite || (FailWrite != 0 && Writes == FailWrite)) return ERROR_ACCESS_DENIED;
    memcpy(&TestValues[index], data, bytes); TestPresent[index] = TRUE;
    return ERROR_SUCCESS;
}
static LSTATUS WINAPI TestDelete(HKEY key, LPCWSTR name)
{
    UNREFERENCED_PARAMETER(key);
    ++Deletes; TestPresent[FindSetting(name)] = FALSE;
    return ERROR_SUCCESS;
}
static HKEY WINAPI TestOpen(HDEVINFO set, PSP_DEVINFO_DATA device,
                            DWORD scope, DWORD profile, DWORD type, REGSAM access)
{
    UNREFERENCED_PARAMETER(set); UNREFERENCED_PARAMETER(device);
    CHECK(scope == DICS_FLAG_GLOBAL && profile == 0 && type == DIREG_DEV);
    if (DenyWrite && (access & KEY_SET_VALUE)) {
        SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE;
    }
    return (HKEY)(ULONG_PTR)1;
}
static LSTATUS WINAPI TestClose(HKEY key) { UNREFERENCED_PARAMETER(key); return ERROR_SUCCESS; }
static BOOL WINAPI TestGetParams(HDEVINFO set, PSP_DEVINFO_DATA device, PSP_DEVINSTALL_PARAMS_W params)
{
    UNREFERENCED_PARAMETER(set); UNREFERENCED_PARAMETER(device);
    CHECK(params->cbSize == sizeof(*params)); *params = TestParams; return TRUE;
}
static BOOL WINAPI TestSetParams(HDEVINFO set, PSP_DEVINFO_DATA device, PSP_DEVINSTALL_PARAMS_W params)
{
    UNREFERENCED_PARAMETER(set); UNREFERENCED_PARAMETER(device);
    if (FailRestart) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    TestParams = *params; ++Restarts; return TRUE;
}
static int WINAPI TestMessage(HWND window, LPCWSTR message, LPCWSTR title, UINT type)
{
    UNREFERENCED_PARAMETER(window); UNREFERENCED_PARAMETER(message);
    UNREFERENCED_PARAMETER(title); UNREFERENCED_PARAMETER(type);
    ++Errors; return IDOK;
}
static BOOL WINAPI TestFree(HANDLE heap, DWORD flags, LPVOID memory)
{
    ++Frees; return HeapFree(heap, flags, memory);
}
#define RegQueryValueExW TestQuery
#define RegSetValueExW TestSet
#define RegDeleteValueW TestDelete
#define SetupDiOpenDevRegKey TestOpen
#define RegCloseKey TestClose
#define SetupDiGetDeviceInstallParamsW TestGetParams
#define SetupDiSetDeviceInstallParamsW TestSetParams
#define MessageBoxW TestMessage
#define HeapFree TestFree
#include "../ES8316Prop.c"

static void Reset(PAGE_CONTEXT *ctx)
{
    UINT i;
    ZeroMemory(ctx, sizeof(*ctx));
    ZeroMemory(TestPresent, sizeof(TestPresent));
    ZeroMemory(&TestParams, sizeof(TestParams));
    TestParams.cbSize = sizeof(TestParams);
    TestParams.FlagsEx = DI_FLAGSEX_INSTALLEDDRIVER;
    Writes = Deletes = Restarts = Errors = FailWrite = 0;
    DenyWrite = FailRestart = FALSE;
    for (i = 0; i < SETTING_COUNT; ++i) {
        ctx->Values[i] = ctx->Saved[i].Value = TestValues[i] = Settings[i].DefaultValue;
    }
}

static void TestValidation(void)
{
    DWORD value;
    UINT i;
    CHECK(ParseNumber(L"4294967295", &value) && value == MAXDWORD);
    CHECK(ParseNumber(L" 0x300 ", &value) && value == 0x300);
    CHECK(ParseNumber(L"08", &value) && value == 8);
    CHECK(ParseNumber(L"0XFFFFFFFF", &value) && value == MAXDWORD);
    CHECK(!ParseNumber(L"4294967296", &value));
    CHECK(!ParseNumber(L"0x100000000", &value));
    CHECK(!ParseNumber(L"-1", &value)); CHECK(!ParseNumber(L"+1", &value));
    CHECK(!ParseNumber(L"0x", &value)); CHECK(!ParseNumber(L"", &value));
    CHECK(!ParseNumber(L"12 xyz", &value)); CHECK(!ParseNumber(L"1.5", &value));
    for (i = 0; i < SETTING_COUNT; ++i) CHECK(ValidValue(i, Settings[i].DefaultValue));
    CHECK(!ValidValue(FindSetting(L"MicPgaGain"), 11));
    CHECK(!ValidValue(SettingBclkDiv, 32));
    CHECK(!ValidValue(SettingWordLength, 17));
    CHECK(!ValidValue(FindSetting(L"MicAlc"), 2));
    CHECK(!ValidValue(FindSetting(L"DacVolume"), 193));
    CHECK(ValidClockPair(12288000, 48000)); CHECK(ValidClockPair(24576000, 48000));
    CHECK(ValidClockPair(98304000, 48000));
    CHECK(!ValidClockPair(12288000, 44100)); CHECK(!ValidClockPair(0, 0));
    CHECK(!ValidClockPair(98304001, 48000));
#define TEST_TUNING_BOUND(name, def, minimum, maximum) \
    CHECK(Es8316BoundTuningValue(def, def, minimum, maximum) == def); \
    CHECK(Es8316BoundTuningValue(minimum, def, minimum, maximum) == minimum); \
    CHECK(Es8316BoundTuningValue(maximum, def, minimum, maximum) == maximum); \
    CHECK(Es8316BoundTuningValue((ULONG)(minimum - 1), def, minimum, maximum) == def); \
    CHECK(Es8316BoundTuningValue(maximum + 1, def, minimum, maximum) == def); \
    CHECK(ValidValue(FindSetting(L## #name), def));
    ES8316_TUNING_FIELDS(TEST_TUNING_BOUND)
#undef TEST_TUNING_BOUND
    CHECK(!ValidValue(FindSetting(L"HpEnableDelayMs"), 1001));
    CHECK(!ValidValue(FindSetting(L"HpMuteDelayMs"), 1001));
    CHECK(!ValidValue(FindSetting(L"DacRampRate"), 5));
}

static void TestPersistence(void)
{
    PAGE_CONTEXT ctx;
    BOOL rollback;
    UINT gain = FindSetting(L"MicPgaGain"), alc = FindSetting(L"MicAlc");
    Reset(&ctx);
    CHECK(SaveSettings((HKEY)1, &ctx, RequestRestart, &rollback) == ERROR_SUCCESS);
    CHECK(Writes == 0 && Restarts == 0);
    ctx.Values[gain] = 4;
    CHECK(SaveSettings((HKEY)1, &ctx, RequestRestart, &rollback) == ERROR_SUCCESS);
    CHECK(Writes == 1 && Restarts == 1 && TestValues[gain] == 4);
    CHECK(TestParams.FlagsEx == (DI_FLAGSEX_INSTALLEDDRIVER | DI_FLAGSEX_PROPCHANGE_PENDING));
    CHECK(!HasChanges(&ctx));
    CHECK(SaveSettings((HKEY)1, &ctx, RequestRestart, &rollback) == ERROR_SUCCESS && Writes == 1);

    Reset(&ctx);
    ctx.Values[gain] = 4; TestPresent[alc] = TRUE; TestValues[alc] = 0;
    CHECK(SaveSettings((HKEY)1, &ctx, RequestRestart, &rollback) == ERROR_REVISION_MISMATCH);
    CHECK(Writes == 0 && Restarts == 0);

    Reset(&ctx);
    ctx.Values[SettingSampleRate] = 44100;
    CHECK(SaveSettings((HKEY)1, &ctx, RequestRestart, &rollback) == ERROR_INVALID_DATA);
    CHECK(Writes == 0 && Restarts == 0);

    Reset(&ctx);
    ctx.Values[alc] = 0; ctx.Values[gain] = 4; FailWrite = 2;
    CHECK(SaveSettings((HKEY)1, &ctx, RequestRestart, &rollback) == ERROR_ACCESS_DENIED);
    CHECK(!rollback && !TestPresent[alc] && !TestPresent[gain] && Deletes == 1 && Restarts == 0);
    CHECK(HasChanges(&ctx));

    Reset(&ctx);
    TestPresent[alc] = ctx.Saved[alc].Present = TRUE;
    ctx.Values[alc] = 0; ctx.Values[gain] = 4; FailRestart = TRUE;
    CHECK(SaveSettings((HKEY)1, &ctx, RequestRestart, &rollback) == ERROR_ACCESS_DENIED);
    CHECK(!rollback && TestPresent[alc] && TestValues[alc] == 1 && !TestPresent[gain]);
    CHECK(Restarts == 0);

    Reset(&ctx);
    ctx.Values[gain] = 4; DenyWrite = TRUE;
    CHECK(SaveSettings((HKEY)1, &ctx, RequestRestart, &rollback) == ERROR_ACCESS_DENIED);
    CHECK(!rollback && !TestPresent[gain] && Restarts == 0);
}

static BOOL CALLBACK AcceptPage(HPROPSHEETPAGE page, LPARAM parameter)
{
    *(HPROPSHEETPAGE *)parameter = page; return TRUE;
}
static BOOL CALLBACK RejectPage(HPROPSHEETPAGE page, LPARAM parameter)
{
    UNREFERENCED_PARAMETER(page); UNREFERENCED_PARAMETER(parameter); return FALSE;
}

static void TestPage(void)
{
    PAGE_CONTEXT ctx;
    PROPSHEETPAGEW psp = {0};
    SP_PROPSHEETPAGE_REQUEST request = {0};
    SP_DEVINFO_DATA device = {0};
    HPROPSHEETPAGE page;
    HWND parent, dialog;
    UINT i, before;
    NMHDR notification = {0};
    WCHAR help[600];
    Reset(&ctx);
    g_Instance = GetModuleHandleW(NULL);
    parent = CreateWindowExW(0, L"STATIC", L"Hidden test host", 0, 0, 0, 600, 600, NULL, NULL, g_Instance, NULL);
    CHECK(parent != NULL);
    psp.lParam = (LPARAM)&ctx;
    dialog = CreateDialogParamW(g_Instance, MAKEINTRESOURCEW(IDD_SETTINGS), parent, PageDialog, (LPARAM)&psp);
    CHECK(dialog != NULL);
    CHECK(SendDlgItemMessageW(dialog, IDC_SETTINGS, LB_GETCOUNT, 0, 0) == SETTING_COUNT);
    for (i = 0; i < SETTING_COUNT; ++i) {
        HWND label = GetDlgItem(dialog, IDC_HELP_TEXT);
        HDC dc = GetDC(label);
        HFONT font = (HFONT)SendMessageW(label, WM_GETFONT, 0, 0);
        HGDIOBJ oldFont = SelectObject(dc, font);
        RECT bounds, measured;
        ctx.Selected = (int)i; ShowSetting(dialog, &ctx);
        GetWindowTextW(label, help, ARRAYSIZE(help));
        GetClientRect(label, &bounds); measured = bounds;
        DrawTextW(dc, help, -1, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        if (measured.bottom > bounds.bottom) fwprintf(stderr, L"Clipped help: %s\n", Settings[i].Name);
        CHECK(measured.bottom <= bounds.bottom);
        SelectObject(dc, oldFont); ReleaseDC(label, dc);
    }
    ctx.Selected = (int)FindSetting(L"MicAlc"); ShowSetting(dialog, &ctx);
    SendDlgItemMessageW(dialog, IDC_CHOICE, CB_SETCURSEL, 0, 0);
    notification.code = PSN_RESET;
    SendMessageW(dialog, WM_NOTIFY, 0, (LPARAM)&notification);
    CHECK(Writes == 0 && Restarts == 0);
    notification.code = PSN_APPLY;
    SendMessageW(dialog, WM_NOTIFY, 0, (LPARAM)&notification);
    CHECK(GetWindowLongPtrW(dialog, DWLP_MSGRESULT) == PSNRET_NOERROR);
    CHECK(Writes == 1 && Restarts == 1 && TestValues[FindSetting(L"MicAlc")] == 0);
    DestroyWindow(dialog);

    Reset(&ctx); DenyWrite = TRUE;
    dialog = CreateDialogParamW(g_Instance, MAKEINTRESOURCEW(IDD_SETTINGS), parent, PageDialog, (LPARAM)&psp);
    CHECK(dialog != NULL && !ctx.Writable);
    CHECK(!IsWindowEnabled(GetDlgItem(dialog, IDC_VALUE)));
    CHECK(!IsWindowEnabled(GetDlgItem(dialog, IDC_DEFAULT)));
    CHECK(ApplySettings(dialog, &ctx) && Writes == 0);
    DestroyWindow(dialog); DestroyWindow(parent);

    request.cbSize = sizeof(request); request.PageRequested = SPPSR_ENUM_ADV_DEVICE_PROPERTIES;
    device.cbSize = sizeof(device); request.DeviceInfoData = &device;
    before = Frees;
    CHECK(ES8316PropPageProvider(&request, AcceptPage, (LPARAM)&page));
    CHECK(Frees == before); CHECK(DestroyPropertySheetPage(page)); CHECK(Frees == before + 1);
    CHECK(!ES8316PropPageProvider(&request, RejectPage, 0)); CHECK(Frees == before + 2);
    CHECK(!ES8316PropPageProvider(NULL, AcceptPage, 0));
    CHECK(!ES8316PropPageProvider(&request, NULL, 0));
}

int main(void)
{
    TestValidation(); TestPersistence(); TestPage();
    puts("PASS: parsing, bounds, clock pairs, persistence, conflicts, rollback, restart flags, Cancel/Apply, read-only UI, text fit, page lifetime");
    return 0;
}
