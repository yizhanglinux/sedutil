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


#include "DtaDevOSDrive.h"
#include "DtaDevWindowsDrive.h"
#include "InterfaceDeviceID.h"




// #include "DtaDevWindowsAta.h"
#include "DtaDevWindowsSata.h"
#include "DtaDevWindowsScsi.h"
#include "DtaDevWindowsNvme.h"


/** Factory functions
 *
 * Static class members that support instantiation of subclass members
 * with the subclass switching logic localized here for easier maintenance.
 *
 */



// Thanks to https://stackoverflow.com/questions/15384916/get-total-size-of-a-hard-disk-in-c-windows
static
BOOL GetDriveGeometry(LPWSTR wszPath, DISK_GEOMETRY* pdg)
{
	HANDLE hDevice = INVALID_HANDLE_VALUE;  // handle to the drive to be examined
	BOOL bResult = FALSE;                 // results flag
	DWORD junk = 0;                     // discard results

	hDevice = CreateFileW(wszPath,          // drive to open
		0,                // no access to the drive
		FILE_SHARE_READ | // share mode
		FILE_SHARE_WRITE,
		NULL,             // default security attributes
		OPEN_EXISTING,    // disposition
		0,                // file attributes
		NULL);            // do not copy file attributes

	if (hDevice == INVALID_HANDLE_VALUE)    // cannot open the drive
	{
		return (FALSE);
	}

	bResult = DeviceIoControl(hDevice,                       // device to be queried
		IOCTL_DISK_GET_DRIVE_GEOMETRY, // operation to perform
		NULL, 0,                       // no input buffer
		pdg, sizeof(*pdg),            // output buffer
		&junk,                         // # bytes returned
		(LPOVERLAPPED)NULL);          // synchronous I/O

	CloseHandle(hDevice);

	return (bResult);
}

typedef std::map<std::string, std::string>dictionary;

static
dictionary* getOSSpecificInformation(OSDEVICEHANDLE h, const char* devref, InterfaceDeviceID& interfaceDeviceIdentification, DTA_DEVICE_INFO& device_info)
{
	dictionary* presult = new dictionary;
	dictionary& property = *presult;

	DISK_GEOMETRY_EX dg = { 0 };

	/*  determine the attachment type of the drive */
	BYTE descriptorStorage[4096];
	memset(descriptorStorage, 0, sizeof(descriptorStorage));
	STORAGE_DEVICE_DESCRIPTOR& descriptor = *(STORAGE_DEVICE_DESCRIPTOR*)&descriptorStorage;

	STORAGE_PROPERTY_QUERY query;
	memset(&query, 0, sizeof(query));
	query.PropertyId = StorageDeviceProperty;
	query.QueryType = PropertyStandardQuery;

	DWORD BytesReturned;

	if (!DeviceIoControl(
		h,									// handle to a device
		IOCTL_STORAGE_QUERY_PROPERTY,		    // dwIoControlCode
		&query,								// input buffer - STORAGE_PROPERTY_QUERY structure
		sizeof(STORAGE_PROPERTY_QUERY),		// size of input buffer
		descriptorStorage,					// output buffer
		sizeof(descriptorStorage),	    	// size of output buffer
		&BytesReturned,						// number of bytes returned
		NULL)) {
		//LOG(E) << "Can not determine the device type";
		return NULL;
	}

	device_info.devType = DEVICE_TYPE_OTHER;

	// We get some information to fill in to the device information struct as
	// defaults in case other efforts are fruitless

#define copyIfAvailable(descriptorField,deviceInfoField,key)                   \
  do {                                                                        \
       size_t offset=descriptor.descriptorField##Offset;                      \
       if (offset!=0) {                                                       \
          const char * property = (const char *)(&descriptorStorage[offset]); \
          strncpy_s((char *)(&device_info.deviceInfoField),                   \
                     sizeof(device_info.deviceInfoField) +                    \
                        sizeof(device_info.deviceInfoField##Null),            \
	                 property, strlen(property));                             \
          (*presult)[key]=property;                                           \
	   }                                                                      \
  } while(0)

	copyIfAvailable(VendorId, vendorID, "vendorID");
	copyIfAvailable(ProductId, modelNum, "modelNum");
	copyIfAvailable(ProductRevision, firmwareRev, "firmwareRev");
	copyIfAvailable(SerialNumber, serialNum, "serialNum");

	{
		unsigned char* p = interfaceDeviceIdentification;
#define copyIDField(field, length) do { memcpy(p, device_info.field, length); p += length; } while (0)
		copyIDField(vendorID,    INQUIRY_VENDOR_IDENTIFICATION_Length );
		copyIDField(modelNum,    INQUIRY_PRODUCT_IDENTIFICATION_Length);
		copyIDField(firmwareRev, INQUIRY_PRODUCT_REVISION_LEVEL_Length);
	}


	switch (descriptor.BusType) {
	case BusTypeAta:
		LOG(D4) << devref << " descriptor.BusType = BusTypeAta (" << descriptor.BusType << ")";
		property["busType"] = "ATA";
		device_info.devType = DEVICE_TYPE_ATA;
		break;

	case BusTypeSata:
		LOG(D4) << devref << " descriptor.BusType = BusTypeSata (" << descriptor.BusType << ")";
		property["busType"] = "SATA";
		device_info.devType = DEVICE_TYPE_ATA;
		break;

	case BusTypeUsb:
		LOG(D4) << devref << " descriptor.BusType = BusTypeUsb (" << descriptor.BusType << ")";
		property["busType"] = "USB";
		device_info.devType = DEVICE_TYPE_USB;
		break;

	case BusTypeNvme:
		LOG(D4) << devref << " descriptor.BusType = BusTypeNvme (" << descriptor.BusType << ")";
		property["busType"] = "NVME";
		device_info.devType = DEVICE_TYPE_NVME;
		break;

	case BusTypeRAID:
		LOG(D4) << devref << " descriptor.BusType = BusTypeRAID (" << descriptor.BusType << ")";
		property["busType"] = "RAID";
		device_info.devType = DEVICE_TYPE_OTHER;
		break;

	case BusTypeSas:
		LOG(D4) << devref << " descriptor.BusType = BusTypeSas (" << descriptor.BusType << ")";
		property["busType"] = "SAS";
		device_info.devType = DEVICE_TYPE_SAS;
		break;

	default:
		LOG(D4) << devref << " has UNKNOWN descriptor.BusType " << descriptor.BusType << "?!";
		property["busType"] = "UNKN";
		device_info.devType = DEVICE_TYPE_OTHER;
		break;
	}


	// We can fill in the size (capacity) of the device regardless of its type
	//
	if (DeviceIoControl(h,                 // handle to device
		IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, // dwIoControlCode
		NULL,                             // lpInBuffer
		0,                                // nInBufferSize
		&dg,             // output buffer
		sizeof(dg),           // size of output buffer
		&BytesReturned,        // number of bytes returned
		NULL)) {
		device_info.devSize = dg.DiskSize.QuadPart;
		LOG(D4) << devref << " size = " << device_info.devSize;
	}
	else {
		device_info.devSize = 0;
		LOG(D4) << devref << " size is UNKNOWN";
	}
	char buffer[100];
	snprintf(buffer, sizeof(buffer), "%llu", device_info.devSize);
	property["size"] = std::string(buffer);

	return presult;
}
DtaDevWindowsDrive * DtaDevWindowsDrive::getDtaDevWindowsDrive(const char * devref,
                                                               DTA_DEVICE_INFO &device_info, 
	                                                           bool& accessDenied)
{
  OSDEVICEHANDLE osDeviceHandle = DtaDevWindowsDrive::openDeviceHandle(devref, accessDenied);
  if (INVALID_HANDLE_VALUE==osDeviceHandle || accessDenied) {
      return NULL;
  }

  DtaDevWindowsDrive* drive = NULL;
  InterfaceDeviceID interfaceDeviceIdentification;
  memset(interfaceDeviceIdentification, 0, sizeof(interfaceDeviceIdentification));
  LOG(D4) << devref << " driveParameters:";
  dictionary* driveParameters = getOSSpecificInformation(osDeviceHandle, devref, interfaceDeviceIdentification, device_info);
  IFLOG(D4)
	for (const auto & pair : *driveParameters) {
	  LOG(D4) << pair.first << ":\"" << pair.second << "\"";
	}
  DtaDevWindowsDrive::closeDeviceHandle(osDeviceHandle);

  if (driveParameters == NULL) {
	  LOG(E) << "Failed to determine drive parameters for " << devref;
	  return NULL;
  }

  //if ( (drive = DtaDevWindowsNvme::getDtaDevWindowsNvme(devref, disk_info)) != NULL )
  //  return drive ;
  ////  LOG(D4) << "DtaDevWindowsNvme::getDtaDevWindowsNvme(\"" << devref <<  "\", disk_info) returned NULL";

#define trySubclass(variant) \
  if ((drive = DtaDevWindows##variant::getDtaDevWindows##variant(devref, device_info)) != NULL) \
  { \
	break; \
  } else

#define logSubclassFailed(variant) \
  LOG(D4) << "DtaDevWindows" #variant "::getDtaDevWindows" #variant "(\"" << devref << "\", disk_info) returned NULL";

#define skipSubclass(variant) \
  LOG(D4) << "DtaDevWindows" #variant "::getDtaDevWindows" #variant "(\"" << devref << "\", disk_info) unimplmented";


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
	  // TODO: hack
	  if (deviceNeedsSpecialAction(interfaceDeviceIdentification, acceptPseudoDeviceImmediately)) {
		  trySubclass(Scsi);
		  break;
	  }

      skipSubclass(Nvme)   // TODO test
	  break;

  case DEVICE_TYPE_ATA:   // SATA / PATA
      skipSubclass(Ata)    // TODO
	  break;

  case DEVICE_TYPE_OTHER:
	  LOG(E) << "Unimplemented device type " << devref;
  default:
      break;
  }

  delete driveParameters;
  return drive ;
}
