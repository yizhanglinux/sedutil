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

#include <string>
#include <algorithm>
#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFDictionary.h>
#include "DtaDevMacOSDrive.h"
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/nvme/NVMeSMARTLibExternal.h>
#include <SEDKernelInterface/SEDKernelInterface.h>


// #include "DtaDevMacOSAta.h"
#include "DtaDevMacOSSata.h"
#include "DtaDevMacOSScsi.h"
// #include "DtaDevMacOSNvme.h"
#include "DtaDevMacOSBlockStorageDevice.h"


/** Factory functions
 *
 * Static class members that support instantiation of subclass members
 * with the subclass switching logic localized here for easier maintenance.
 *
 */





typedef std::map<std::string, std::string>dictionary;

/**
 * Converts a CFString to a UTF-8 std::string if possible.
 *
 * @param input A reference to the CFString to convert.
 * @return Returns a std::string containing the contents of CFString converted to UTF-8. Returns
 *  an empty string if the input reference is null or conversion is not possible.
 */
// Modified from https://gist.githubusercontent.com/peter-bloomfield/1b228e2bb654702b1e50ef7524121fb9/raw/934184166a8c3ff403dd5d7f8c0003810014f73d/cfStringToStdString.cpp per comments
static
std::string cfStringToStdString(CFStringRef input, bool & error) {
  error = false;
  if (!input)
    return {};

  // Attempt to access the underlying buffer directly. This only works if no conversion or
  //  internal allocation is required.
  auto originalBuffer{ CFStringGetCStringPtr(input, kCFStringEncodingUTF8) };
  if (originalBuffer)
    return originalBuffer;

  // Copy the data out to a local buffer.
  CFIndex lengthInUtf16{ CFStringGetLength(input) };
  CFIndex maxLengthInUtf8{ CFStringGetMaximumSizeForEncoding(lengthInUtf16,
                                                             kCFStringEncodingUTF8) + 1 }; // <-- leave room for null terminator
  std::vector<char> localBuffer((size_t)maxLengthInUtf8);

  if (CFStringGetCString(input, localBuffer.data(), maxLengthInUtf8, kCFStringEncodingUTF8))
    return localBuffer.data();

  error = true;
  return {};
}


// Create a copy of the properties of this I/O registry entry
// Receiver owns this CFMutableDictionary instance if not NULL
static void collectProperties(CFDictionaryRef cfproperties, dictionary * properties); // called recursively

static void collectProperty(const void *vkey, const void *vvalue, void * vproperties){
    dictionary * properties = (dictionary *)vproperties;

    // Get the key --  should be a string
    std::string key, value="<\?\?\?>";
    CFTypeID keyTypeID = CFGetTypeID(vkey);
    if (CFStringGetTypeID() == keyTypeID) {
        bool error=false;
        key = cfStringToStdString(reinterpret_cast<CFStringRef>(vkey), error);
        if (error) {
            LOG(E) << "Failed to get key as string " << HEXON(sizeof(const void *)) << vkey;
            return;
        }
    } else {
        LOG(E) << "Unrecognized key type " << (CFTypeRef)vkey;
        return;
    };

    // Get the value -- could be a Bool, Dict, Data, String, or Number
    CFTypeID valueTypeID = CFGetTypeID(vvalue);
    if (CFStringGetTypeID() == valueTypeID) {
        // String
        bool error=false;
        value = cfStringToStdString(reinterpret_cast<CFStringRef>(vvalue), error);
        if (error) {
            LOG(E) << "Failed to get key as string " << HEXON(sizeof(const void *)) << vkey;
            return;
        }
    } else if (CFBooleanGetTypeID() == valueTypeID) {
        // Bool
        value = std::string(CFBooleanGetValue(reinterpret_cast<CFBooleanRef>(vvalue)) ? "true" : "false");
    } else if (CFNumberGetTypeID() == valueTypeID) {
        // Number
        if (CFNumberIsFloatType(reinterpret_cast<CFNumberRef>(vvalue))) {
            // Float
            double dvalue=0.0;
            bool error=!CFNumberGetValue(reinterpret_cast<CFNumberRef>(vvalue), kCFNumberDoubleType, (void *)&dvalue);
            if (error) {
                LOG(E) << "Failed to get value as float " << HEXON(sizeof(vvalue)) << vvalue;
                return;
            }
            value = std::to_string(dvalue);
        } else {
            // Integer
            long long llvalue=0LL;
            bool error=!CFNumberGetValue(reinterpret_cast<CFNumberRef>(vvalue), kCFNumberLongLongType, (void *)&llvalue);
            if (error) {
                LOG(E) << "Failed to get value as integer " << HEXON(sizeof(vvalue)) << vvalue;
                return;
            }
            value = std::to_string(llvalue);
        }
    } else if (CFDataGetTypeID() == valueTypeID) {
        // Data
    } else if (CFArrayGetTypeID() == valueTypeID) {
        // Array
    } else if (CFDictionaryGetTypeID() == valueTypeID) {
        // Dict -- call recursively to flatten subdirectory properties into `properties'
        collectProperties(reinterpret_cast<CFDictionaryRef>(vvalue), properties);
        return;
    } else {
        // Unknown
        LOG(E) << "Failed to get value " << HEXON(sizeof(vvalue)) << vvalue << " with type ID "  << HEXON(sizeof(valueTypeID)) << valueTypeID;
        return;
    }

    (*properties)[key]=value;
}

static
void collectProperties(CFDictionaryRef cfproperties, dictionary * properties) {
  CFDictionaryApplyFunction(cfproperties, collectProperty, (void *)properties);
}


static
dictionary * copyDeviceProperties(io_service_t deviceService) {
  CFDictionaryRef cfproperties = createIOBlockStorageDeviceProperties(deviceService);

  if (cfproperties==NULL)
    return NULL;

  dictionary * properties = new dictionary;
  collectProperties(cfproperties, properties);
  return properties;
}


static
dictionary* getOSSpecificInformation(OSDEVICEHANDLE osDeviceHandle,
                                     const char* /* TODO: devref */,
                                     InterfaceDeviceID& /* TODO: interfaceDeviceIdentification */,
                                     DTA_DEVICE_INFO& device_info)
{
  io_service_t deviceService=handleDeviceService(osDeviceHandle);
  io_connect_t connection=handleConnection(osDeviceHandle);



  if (IO_OBJECT_NULL == deviceService) {
    return NULL;
  }

  bool success=false;
  if (IO_OBJECT_NULL != connection) {
    io_service_t controllerService = findParent(deviceService);
    success=(KERN_SUCCESS == TPerUpdate(connection, controllerService, &device_info));
    IOObjectRelease(controllerService);
  } else {
    success=(DtaDevMacOSBlockStorageDevice::BlockStorageDeviceUpdate(deviceService, device_info));
  }
  if (!success)
    return NULL;

  return copyDeviceProperties(deviceService);
}



DtaDevMacOSDrive * DtaDevMacOSDrive::getDtaDevMacOSDrive(const char * devref,
                                                         DTA_DEVICE_INFO &device_info)
{
  bool accessDenied=false;
  OSDEVICEHANDLE osDeviceHandle = DtaDevMacOSDrive::openDeviceHandle(devref, accessDenied);
  if (INVALID_HANDLE_VALUE==osDeviceHandle || accessDenied) {
    return NULL;
  }

  DtaDevMacOSDrive* drive = NULL;
  InterfaceDeviceID interfaceDeviceIdentification;
  memset(interfaceDeviceIdentification, 0, sizeof(interfaceDeviceIdentification));
  LOG(D4) << devref << " driveParameters:";
  dictionary * maybeDriveParameters =
    getOSSpecificInformation(osDeviceHandle, devref, interfaceDeviceIdentification, device_info);
  DtaDevMacOSDrive::closeDeviceHandle(osDeviceHandle);

  if (maybeDriveParameters == NULL) {
    //	  LOG(E) << "Failed to determine drive parameters for " << devref;
    return NULL;
  }

  dictionary & driveParameters = *maybeDriveParameters;
  IFLOG(D4)
    for (const auto & pair : driveParameters) {
      LOG(D4) << pair.first << ":\"" << pair.second << "\"";
    }

#define trySubclass(variant)                                                                \
  if ((drive = DtaDevMacOS##variant::getDtaDevMacOS##variant(devref, device_info)) != NULL) \
    {                                                                                       \
      break;                                                                                \
    } else

#define logSubclassFailed(variant)                                                                   \
  LOG(D4) << "DtaDevMacOS" #variant "::getDtaDevMacOS" #variant "(\"" << devref << "\", disk_info) " \
          << "returned NULL";

#define skipSubclass(variant)                                                                        \
  LOG(D4) << "DtaDevMacOS" #variant "::getDtaDevMacOS" #variant "(\"" << devref << "\", disk_info) " \
          << "unimplmented";


  // Create a subclass instance based on device_info.devType as determined by
  // getOSSpecificInformation.  Customizing code has device_info and
  // drive parameters available.
  //
  switch (device_info.devType) {
  case DEVICE_TYPE_SCSI:  // SCSI
  case DEVICE_TYPE_SAS:   // SCSI
    trySubclass(Scsi)
      break;

  case DEVICE_TYPE_USB:   // UAS SAT -- USB -> SCSI -> AT pass-through
    //  case DEVICE_TYPE_SATA:  // synonym
    if (!deviceNeedsSpecialAction(interfaceDeviceIdentification,avoidSlowSATATimeout)) {
      trySubclass(Sata);
    }
    if (!deviceNeedsSpecialAction(interfaceDeviceIdentification,avoidSlowSASTimeout)) {
      trySubclass(Scsi);
    }
    break;

  case DEVICE_TYPE_NVME:  // NVMe
    // TODO: Just hack by using Scsi for now.  BlockStorageDevice?
    if (deviceNeedsSpecialAction(interfaceDeviceIdentification, acceptPseudoDeviceImmediately)) {
      trySubclass(BlockStorageDevice);   // TODO: hack
      break;
    }

    //      trySubclass(Nvme)   // TODO test
    trySubclass(BlockStorageDevice);   // TODO: hack
    break;

  case DEVICE_TYPE_ATA:   // SATA / PATA
    skipSubclass(Ata)    // TODO
      break;

  case DEVICE_TYPE_OTHER:
    //	  LOG(E) << "Unimplemented device type " << devref;
    break;

  default:
    break;
  }

  delete &driveParameters;
  return drive ;
}




bool DtaDevMacOSDrive::isDtaDevMacOSDriveDevRef(const char * devref)
{
    return DtaDevMacOSBlockStorageDevice::isDtaDevMacOSBlockStorageDeviceDevRef(devref);
}
