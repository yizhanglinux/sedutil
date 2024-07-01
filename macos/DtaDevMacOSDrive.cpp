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

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <SEDKernelInterface/SEDKernelInterface.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/storage/IOBlockStorageDriver.h>
#include "os.h"
#include "log.h"
#include "fnmatch.h"
#include "DtaEndianFixup.h"
#include "DtaHexDump.h"
#include "DtaDevMacOSDrive.h"
#include "DtaMacOSConstants.h"

/** Factory functions
 *
 * Static class members of DtaDevOSDrive that are passed through
 * to DtaDevMacOSDrive
 *
 */

bool DtaDevOSDrive::isDtaDevOSDriveDevRef(const char * devref, bool & accessDenied) {

  if (!DtaDevMacOSDrive::isDtaDevMacOSDriveDevRef(devref)) return false;

  OSDEVICEHANDLE osDeviceHandle = openDeviceHandle(devref, accessDenied);
  bool result = (INVALID_HANDLE_VALUE != osDeviceHandle && !accessDenied);
  if (INVALID_HANDLE_VALUE != osDeviceHandle) DtaDevMacOSDrive::closeDeviceHandle(osDeviceHandle);
  return result;
}

std::vector<std::string> DtaDevOSDrive::enumerateDtaDevOSDriveDevRefs(bool & accessDenied) {
    return DtaDevMacOSDrive::enumerateDtaDevMacOSDriveDevRefs(accessDenied);
}

DtaDevOSDrive * DtaDevOSDrive::getDtaDevOSDrive(const char * devref,
                                                DTA_DEVICE_INFO &device_info,
                                                bool & /* accessDenied */)
{
  return static_cast<DtaDevOSDrive *>(DtaDevMacOSDrive::getDtaDevMacOSDrive(devref, device_info));
}


OSDEVICEHANDLE DtaDevOSDrive::openDeviceHandle(const char* devref, bool& accessDenied) {
  return DtaDevMacOSDrive::openDeviceHandle(devref, accessDenied);
}

void DtaDevOSDrive::closeDeviceHandle(OSDEVICEHANDLE osDeviceHandle) {
  DtaDevMacOSDrive::closeDeviceHandle(osDeviceHandle);
}



OSDEVICEHANDLE DtaDevMacOSDrive::openAndCheckDeviceHandle(const char * devref, bool& accessDenied)
{
  OSDEVICEHANDLE osDeviceHandle = openDeviceHandle(devref, accessDenied);

  if (INVALID_HANDLE_VALUE == osDeviceHandle || accessDenied) {
    LOG(D1) << "Error opening device " << devref << " -- not found";
  }
  return osDeviceHandle;
}



namespace fs = std::__fs::filesystem;
#undef USEDRIVERUSPERCLASS
#define USEBLOCKSTORAGEDEVICE
OSDEVICEHANDLE DtaDevMacOSDrive::openDeviceHandle(const char * devref, bool& /* accessDenied */)
{
    LOG(D4) << "openDeviceHandle(\"" << devref << "\", _)";
    std::string bsdName = fs::path(devref).stem();
    if (bsdName.rfind("/dev/",0)==0)
        bsdName=bsdName.substr(5,bsdName.length());

    io_registry_entry_t mediaService = findBSDName(bsdName.c_str());
    if (!mediaService) {
        LOG(D4) << "could not find media service for bsdName=\"" << bsdName << "\"";
        return INVALID_HANDLE_VALUE;
    }
    LOG(D4) << "found media service for bsdName=\"" << bsdName << "\"";

    /**
     *
     *  For real devices, under the current regime, at least on the X86 machine I'm looking at right now, has to go
     *
     * IOSCSIPeripheralDeviceType00 or @kDriverClass or maybe even something else
     *  IOBlockStorageDevice
     *    IOBlockStorageDriver
     *      IOMedia    <-- which we just found by bsdName
     *
     *  Bogus containers, nested storage, etc. don't have the three-layer sandwich directly ending at IOMedia
     *
     **/
    io_registry_entry_t blockStorageDriverService=findParent(mediaService);
    IOObjectRelease(mediaService);
    if (!IOObjectConformsTo(blockStorageDriverService, kIOBlockStorageDriverClass)) {
        LOG(D4) << "parent of media service is not block storage driver service";
        IOObjectRelease(blockStorageDriverService);
        return INVALID_HANDLE_VALUE;
    }
    LOG(D4) << "parent of media service is block storage driver service";

    io_registry_entry_t blockStorageDeviceService=findParent(blockStorageDriverService);
    IOObjectRelease(blockStorageDriverService);
    if (!IOObjectConformsTo(blockStorageDeviceService, kIOBlockStorageDeviceClass)) {
        LOG(D4) << "parent of block storage driver service is not block storage device service";
        IOObjectRelease(blockStorageDeviceService);
        return INVALID_HANDLE_VALUE;
    }
    LOG(D4) << "parent of block storage driver service is block storage device service";

    io_connect_t connection=IO_OBJECT_NULL;
    kern_return_t kernResult=KERN_FAILURE;
    io_service_t possibleTPer=findParent(blockStorageDeviceService);
    if (!IOObjectConformsTo(possibleTPer, kDriverClass)) {
        LOG(D4) << "parent of block storage device service is not TPer Driver instance";
    } else if (((kernResult = OpenUserClient(possibleTPer, &connection)) != kIOReturnSuccess || connection == IO_OBJECT_NULL)) {
       LOG(E) << "Failed to open user client -- error=" << HEXON(8) << kernResult;
    } else {
       LOG(D4) << "Device service "              << HEXOFF << blockStorageDeviceService << "=" << HEXON(4) << blockStorageDeviceService
               << " connected to TPer instance " << HEXOFF << possibleTPer              << "=" << HEXON(4) << possibleTPer
               << " opened user client "         << HEXOFF << connection                << "=" << HEXON(4) << connection;
    }
    IOObjectRelease(possibleTPer);
    return handle(blockStorageDeviceService,connection);
 }

void DtaDevMacOSDrive::closeDeviceHandle(OSDEVICEHANDLE osDeviceHandle)
{
    if (osDeviceHandle == INVALID_HANDLE_VALUE) return;

    io_registry_entry_t connection = handleConnection(osDeviceHandle);
    if ( connection != IO_OBJECT_NULL ) {
        LOG(D4) << "Releasing connection";
        kern_return_t ret = CloseUserClient(connection);
        if ( kIOReturnSuccess != ret) {
            LOG(E) << "CloseUserClient returned " << ret;
        }
    }

    io_connect_t blockStorageDeviceService = handleDeviceService(osDeviceHandle);
    if ( blockStorageDeviceService != IO_OBJECT_NULL ) {
        LOG(D4) << "Releasing driver service";
        IOObjectRelease(blockStorageDeviceService);
    }
    LOG(D4) << "Device service "              << HEXOFF << blockStorageDeviceService << "=" << HEXON(4) << blockStorageDeviceService << " released and"
            << " closed user client "         << HEXOFF << connection                << "=" << HEXON(4) << connection;
}



std::vector<std::string> DtaDevMacOSDrive::enumerateDtaDevMacOSDriveDevRefs(bool & accessDenied)
{
    std::vector<std::string> devrefs;
    for (int i = 0; i < MAX_DISKS; i++) {
        std::string str_devref=std::string("/dev/disk")+std::to_string(i);
        const char * devref=str_devref.c_str();
        bool accessDeniedThisTime=false;
        if (isDtaDevOSDriveDevRef(devref, accessDeniedThisTime))
            devrefs.push_back(str_devref);
        else if (accessDeniedThisTime && !accessDenied) {
            LOG(E) << "You do not have permission to access the raw disk " << devref << " in write mode";
            LOG(E) << "Perhaps you might try sudo to run as root";
            accessDenied=true;
        }
    }

    return devrefs;
}
