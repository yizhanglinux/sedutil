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
#pragma once
#include "os.h"
#include <iomanip>
#include "DtaDevOSDrive.h"

/** virtual implementation for a disk interface-generic disk drive
 */
class DtaDevMacOSDrive : public DtaDevOSDrive {
    using DtaDevOSDrive::DtaDevOSDrive;

public:

  /** Factory function to look at the devref to filter out whether it could be a DtaDevMacOSDrive
   *
   * @param devref OS device reference e.g. "/dev/sda"
   */

  static bool isDtaDevMacOSDriveDevRef(const char * devref);

    /** Factory function to enumerate all the devrefs that pass the above filter
     *
     */
  static
  std::vector<std::string> enumerateDtaDevMacOSDriveDevRefs(bool & accessDenied);

    /** Factory function to look at the devref and create an instance of the appropriate subclass of
   *  DtaDevMacOSDrive
   *
   * @param devref OS device reference e.g. "/dev/disk1"
   * @param device_info reference to DTA_DEVICE_INFO structure filled out during device identification
   */
  static DtaDevMacOSDrive * getDtaDevMacOSDrive(const char * devref,
                                                DTA_DEVICE_INFO & device_info);


  virtual uint8_t discovery0(DTA_DEVICE_INFO & device_info)=0;


  static
  OSDEVICEHANDLE openAndCheckDeviceHandle(const char * devref, bool& accessDenied);

  static
  OSDEVICEHANDLE openDeviceHandle(const char* devref, bool& accessDenied);

  static
  void closeDeviceHandle(OSDEVICEHANDLE osDeviceHandle);

};

static inline void * alloc_aligned_MIN_BUFFER_LENGTH_buffer () {
  return aligned_alloc( IO_BUFFER_ALIGNMENT,
                        (((MIN_BUFFER_LENGTH + IO_BUFFER_ALIGNMENT - 1)
                          / IO_BUFFER_ALIGNMENT)
                         * IO_BUFFER_ALIGNMENT) );
}
static inline void free_aligned_MIN_BUFFER_LENGTH_buffer (void * aligned_buffer) {
  free(aligned_buffer);
}
