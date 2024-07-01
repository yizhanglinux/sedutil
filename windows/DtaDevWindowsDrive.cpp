/* C:B**************************************************************************
   This software is Copyright (c) 2014-2024 Bright Plaza Inc. <drivetrust@drivetrust.com>

   This file is part of sedutil.

   sedutil is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   sedutil is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with sedutil.  If not, see <http://www.gnu.org/licenses/>.

   * C:E********************************************************************** */

#include "os.h"

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <regex>
#include <iostream>


   // The next four lines are from
   // https://github.com/microsoft/Windows-driver-samples/blob/main/storage/tools/spti/src/spti.c

#include <devioctl.h>
#include <ntdddisk.h>
#include <ntddscsi.h>

#include "DtaEndianFixup.h"
#include "DtaHexDump.h"
#include "DtaDevWindowsDrive.h"
#include "ParseDiscovery0Features.h"


/**
 *
 * Static class members of DtaDevOSDrive that are passed through
 * to DtaDevWindowsDrive
 *
 */


bool DtaDevOSDrive::isDtaDevOSDriveDevRef(const char* devref, bool& accessDenied) {
  OSDEVICEHANDLE osDeviceHandle=openDeviceHandle(devref, accessDenied);
  bool result = (osDeviceHandle!=INVALID_HANDLE_VALUE && !accessDenied);
  if (osDeviceHandle!=INVALID_HANDLE_VALUE) closeDeviceHandle(osDeviceHandle);
  return result && DtaDevWindowsDrive::isDtaDevWindowsDriveDevRef(devref);
}

std::vector<std::string> DtaDevOSDrive::enumerateDtaDevOSDriveDevRefs(bool & accessDenied) {
    return DtaDevWindowsDrive::enumerateDtaDevWindowsDriveDevRefs(accessDenied);
}

DtaDevOSDrive* DtaDevOSDrive::getDtaDevOSDrive(const char* devref,
                                               DTA_DEVICE_INFO& disk_info,
                                               bool& accessDenied)
{
    return static_cast<DtaDevOSDrive*>(DtaDevWindowsDrive::getDtaDevWindowsDrive(devref, disk_info, accessDenied));
}

OSDEVICEHANDLE DtaDevOSDrive::openDeviceHandle(const char* devref, bool& accessDenied) {
    return DtaDevWindowsDrive::openDeviceHandle(devref, accessDenied);
}

void DtaDevOSDrive::closeDeviceHandle(OSDEVICEHANDLE osDeviceHandle) {
    DtaDevWindowsDrive::closeDeviceHandle(osDeviceHandle);
}

bool DtaDevWindowsDrive::isDtaDevWindowsDriveDevRef(const char * devref)
{
  const std::regex re("^"  "\\\\" "\\\\" "\\." "\\\\" "PhysicalDrive" "\\d+" "$");
  const char * dlast = devref + strlen(devref);
  return std::regex_search(devref, dlast, re);
}


OSDEVICEHANDLE DtaDevWindowsDrive::openAndCheckDeviceHandle(const char * devref, bool& accessDenied)
{
  OSDEVICEHANDLE osDeviceHandle = openDeviceHandle(devref, accessDenied);

  if (INVALID_HANDLE_VALUE == osDeviceHandle || accessDenied) {
      DWORD err = GetLastError();
      LOG(D1) << "Error opening device " << devref << " Error " << err;
      if (accessDenied) {
        LOG(E) << "You do not have proper authority to access the raw disk";
        LOG(E) << "Try running as Administrator";
      }
  }
  return osDeviceHandle;
}


OSDEVICEHANDLE DtaDevWindowsDrive::openDeviceHandle(const char* devref, bool& accessDenied)
{
    LOG(D4) << "Opening device handle for " << devref;
    OSDEVICEHANDLE osDeviceHandle = (OSDEVICEHANDLE) CreateFile(
        devref,
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_WRITE | FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);
    if (INVALID_HANDLE_VALUE != osDeviceHandle)
        LOG(D4) << "Opened device handle " << HEXON(2) << (size_t)osDeviceHandle << " for " << devref;
    else {
        LOG(D4) << "Failed to open device handle for " << devref;
        accessDenied = (ERROR_ACCESS_DENIED==GetLastError());
    }
    return osDeviceHandle;
}

void DtaDevWindowsDrive::closeDeviceHandle(OSDEVICEHANDLE osDeviceHandle) {
    LOG(D4) << "Closing device handle " << HEXON(2) << (size_t)osDeviceHandle;
    (void)CloseHandle((HANDLE)osDeviceHandle);
    LOG(D4) << "Closed device handle";
}


std::vector<std::string> DtaDevWindowsDrive::enumerateDtaDevWindowsDriveDevRefs(bool & accessDenied)
{
    std::vector<std::string> devrefs;

    for (int i = 0; i < MAX_DISKS; i++) {
        std::string str_devref=std::string("\\\\.\\PhysicalDrive")+std::to_string(i);
        const char * devref=str_devref.c_str();
        bool accessDeniedThisTime=false;
        if (isDtaDevOSDriveDevRef(devref, accessDeniedThisTime))
            devrefs.push_back(str_devref);
        else if (accessDeniedThisTime) {
            accessDenied=true;
        }
    }

    return devrefs;
}

uint8_t DtaDevWindowsDrive::discovery0(DTA_DEVICE_INFO& disk_info) {
    void* d0Response = alloc_aligned_MIN_BUFFER_LENGTH_buffer();
    if (d0Response == NULL)
        return DTAERROR_COMMAND_ERROR;
    memset(d0Response, 0, MIN_BUFFER_LENGTH);

    int lastRC = sendCmd(IF_RECV, 0x01, 0x0001, d0Response, MIN_BUFFER_LENGTH);
    if ((lastRC) != 0) {
        LOG(D4) << "Acquiring Discovery 0 response failed " << lastRC;
        return DTAERROR_COMMAND_ERROR;
    }
    parseDiscovery0Features((uint8_t*)d0Response, disk_info);
    free_aligned_MIN_BUFFER_LENGTH_buffer(d0Response);
    return DTAERROR_SUCCESS;
}
