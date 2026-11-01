/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Licensed under the GNU General Public License, version 2 only.
 * Provided without warranty. See LICENSE for the license text.
 */

/*++

    es8316ctl - bring-up tool for the ES8316 codec driver (Rubik Pi 3 WoA).

    Talks to the driver's diagnostic interface (GUID_DEVINTERFACE_ES8316):

      es8316ctl status                 jack / codec state
      es8316ctl dump                   register file 0x00..0x53
      es8316ctl read  <reg>            read one register
      es8316ctl write <reg> <value>    write one register
      es8316ctl output <hp 0|1> <mic 0|1> [hpvol 0-3] [dacvol 0-192]
      es8316ctl reinit                 re-run the D0 programming

    Numbers accept 0x prefixes.

--*/

#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <initguid.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "..\ES8316\Es8316Ioctl.h"

#pragma comment(lib, "setupapi.lib")

static HANDLE OpenCodec(void)
{
    HDEVINFO devInfo;
    SP_DEVICE_INTERFACE_DATA ifData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail;
    DWORD needed;
    HANDLE h;

    devInfo = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_ES8316, NULL, NULL,
                                   DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "SetupDiGetClassDevs failed %lu\n", GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    ifData.cbSize = sizeof(ifData);
    if (!SetupDiEnumDeviceInterfaces(devInfo, NULL, &GUID_DEVINTERFACE_ES8316, 0, &ifData))
    {
        fprintf(stderr, "no ES8316 device interface present (driver not running?)\n");
        SetupDiDestroyDeviceInfoList(devInfo);
        return INVALID_HANDLE_VALUE;
    }

    needed = 0;
    SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, NULL, 0, &needed, NULL);
    detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(needed);
    if (detail == NULL)
    {
        SetupDiDestroyDeviceInfoList(devInfo);
        return INVALID_HANDLE_VALUE;
    }
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, needed, NULL, NULL))
    {
        fprintf(stderr, "SetupDiGetDeviceInterfaceDetail failed %lu\n", GetLastError());
        free(detail);
        SetupDiDestroyDeviceInfoList(devInfo);
        return INVALID_HANDLE_VALUE;
    }

    h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "CreateFile(%ls) failed %lu\n", detail->DevicePath, GetLastError());
    }

    free(detail);
    SetupDiDestroyDeviceInfoList(devInfo);
    return h;
}

static BOOL Ioctl(HANDLE h, DWORD code, void *in, DWORD inLen, void *out, DWORD outLen, DWORD *returned)
{
    DWORD ret = 0;
    BOOL ok = DeviceIoControl(h, code, in, inLen, out, outLen, &ret, NULL);
    if (!ok)
    {
        fprintf(stderr, "IOCTL 0x%08lX failed: %lu\n", code, GetLastError());
    }
    if (returned) *returned = ret;
    return ok;
}

static const char *JackName(ULONG s)
{
    switch (s)
    {
    case Es8316JackRemoved:   return "removed";
    case Es8316JackHeadphone: return "headphone (no mic)";
    case Es8316JackHeadset:   return "headset (mic)";
    default:                  return "unknown";
    }
}

static int Usage(void)
{
    fprintf(stderr,
        "usage: es8316ctl status | dump | read <reg> | write <reg> <val> |\n"
        "       output <hp 0|1> <mic 0|1> [hpvol 0-3] [dacvol 0-192] | reinit\n");
    return 2;
}

int __cdecl main(int argc, char **argv)
{
    HANDLE h;
    int rc = 0;

    if (argc < 2)
    {
        return Usage();
    }

    h = OpenCodec();
    if (h == INVALID_HANDLE_VALUE)
    {
        return 1;
    }

    if (_stricmp(argv[1], "status") == 0)
    {
        ES8316_STATUS st;
        if (Ioctl(h, IOCTL_ES8316_GET_STATUS, NULL, 0, &st, sizeof(st), NULL))
        {
            printf("jack          : %s (GPIO_FLAG 0x%02lX)%s\n", JackName(st.JackState), st.GpioFlag,
                   st.ButtonPressed ? " button pressed" : "");
            printf("codec init    : %lu   output: %lu   mic: %lu\n", st.CodecInitialized, st.OutputEnabled, st.MicEnabled);
            printf("clocking      : MCLK %lu Hz, %lu Hz, %lu-bit\n", st.MclkHz, st.SampleRate, st.WordLength);
            printf("jack events   : %lu   polls: %lu   irq: %lu\n", st.JackEvents, st.PollCount, st.InterruptConnected);
            printf("last I2C error: 0x%08lX\n", st.LastI2cStatus);
            if (st.Version >= 2)
            {
                printf("MCLK_1 vote   : policy %lu, ADCM interface %lu, voted %lu, last 0x%08lX, retries %lu\n",
                       st.MclkPolicy, st.MclkInterface, st.MclkVoted, st.MclkLastStatus, st.MclkRetries);
                if (st.Version >= 3)
                {
                    printf("MCLK DSP error: 0x%08lX\n", st.MclkLastDspError);
                }
            }
        }
        else rc = 1;
    }
    else if (_stricmp(argv[1], "dump") == 0)
    {
        ES8316_REGISTER_DUMP dump;
        if (Ioctl(h, IOCTL_ES8316_DUMP_REGISTERS, NULL, 0, &dump, sizeof(dump), NULL))
        {
            int i;
            for (i = 0; i < dump.Count; i++)
            {
                if ((i % 16) == 0) printf("%02X:", i);
                printf(" %02X", dump.Values[i]);
                if ((i % 16) == 15 || i == dump.Count - 1) printf("\n");
            }
        }
        else rc = 1;
    }
    else if (_stricmp(argv[1], "read") == 0 && argc >= 3)
    {
        ES8316_REGISTER_IO io;
        io.Register = (UCHAR)strtoul(argv[2], NULL, 0);
        io.Value = 0;
        if (Ioctl(h, IOCTL_ES8316_READ_REGISTER, &io, sizeof(io), &io, sizeof(io), NULL))
        {
            printf("reg 0x%02X = 0x%02X\n", io.Register, io.Value);
        }
        else rc = 1;
    }
    else if (_stricmp(argv[1], "write") == 0 && argc >= 4)
    {
        ES8316_REGISTER_IO io;
        io.Register = (UCHAR)strtoul(argv[2], NULL, 0);
        io.Value = (UCHAR)strtoul(argv[3], NULL, 0);
        if (Ioctl(h, IOCTL_ES8316_WRITE_REGISTER, &io, sizeof(io), NULL, 0, NULL))
        {
            printf("reg 0x%02X <= 0x%02X\n", io.Register, io.Value);
        }
        else rc = 1;
    }
    else if (_stricmp(argv[1], "output") == 0 && argc >= 4)
    {
        ES8316_SET_OUTPUT so;
        so.HeadphoneEnable = strtoul(argv[2], NULL, 0);
        so.MicEnable = strtoul(argv[3], NULL, 0);
        so.HpVolume = (argc >= 5) ? strtoul(argv[4], NULL, 0) : 0xFFFFFFFF;
        so.DacVolume = (argc >= 6) ? strtoul(argv[5], NULL, 0) : 0xFFFFFFFF;
        if (!Ioctl(h, IOCTL_ES8316_SET_OUTPUT, &so, sizeof(so), NULL, 0, NULL)) rc = 1;
    }
    else if (_stricmp(argv[1], "reinit") == 0)
    {
        if (!Ioctl(h, IOCTL_ES8316_REINITIALIZE, NULL, 0, NULL, 0, NULL)) rc = 1;
    }
    else
    {
        rc = Usage();
    }

    CloseHandle(h);
    return rc;
}
