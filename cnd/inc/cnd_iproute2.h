/* Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Code Aurora nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef CND_IPROUTE2_H
#define CND_IPROUTE2_H

/**----------------------------------------------------------------------------
  @file cnd_iproute2.h

        cnd_iproute2 is an interface to make the necessary calls to iproute2
        in order to set up and take down routing tables. Defines APIS so that
        a routing table associated with a RAT can be added or deleted. Also
        allows the user to change the default routing table when a given
        source address is not already associated with a RAT.
-----------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------
 * Include Files
 * -------------------------------------------------------------------------*/
#include <sys/types.h>

/*----------------------------------------------------------------------------
 * Preprocessor Definitions and Constants
 * -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Type Declarations
 * -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Class Definitions
 * -------------------------------------------------------------------------*/

class cnd_iproute2
{
  public:
    /**
    * @brief Returns an instance of the Cnd_iproute2 class.

      The user of this class will call this function to get an
      instance of the class. All other public functions will be
      called on this instance

    * @param   None
    * @see     None
    * @return  An instance of the Cnd_iproute2 class is returned.
    */
    static cnd_iproute2* getInstance
    (
      void
    );

    /**
    * @brief Create a routing table for a RAT using iproute2
    *
    * The user of this function passes in the name of the RAT that
    * matches the name already defined in the Android system. The
    * user also needs to locate the gateway address and source
    * prefix assocated with that RAT device. If a table is added
    * when no another tables exist, it will automatically become the
    * default table.
    *
    * @param deviceName       The name of the device whose table
    *                         will be added (Such as wlan or wwan)
    * @param sourcePrefix     The source network prefix or address
    *                         that will be routed to the device
    *                         (Such as 37.214.21/24 or 10.156.45.1)
    * @param gatewayAddress   The gateway address of the device.
    * @return                 True if function is successful. False
    *                         otherwise.
    */
    bool addRoutingTable
    (
      uint8_t *deviceName,
      uint8_t *sourcePrefix,
      uint8_t *gatewayAddress
    );

    /**
    * @brief Change the default routing table that is associated
    *        with any source addresses not bound to another table.
    *
    * The user of this function passes in the name of the RAT that
    * matches the name already defined in the Android system. That
    * device will become the new default. If this RAT is already the
    * default, this function simply returns true.
    *
    * @param deviceName       The name of the device whose table
    *                         will be added (Such as wlan or wwan)
    * @return                 True if function is successful. False
    *                         otherwise.
    */
    bool changeDefaultTable
    (
      uint8_t *deviceName
    );

    /**
    *  @brief Deletes a default entry from the main table.
    *
    *  @param deviceName       The name of the device whose default
    *                          entry in the main table will be
    *                          deleted (Such as wlan or wwan)
    *  @return                 True if function is successful. False
    *                          otherwise.
    */
    bool deleteDefaultEntryFromMainTable
    (
      uint8_t *deviceName
    );

    /**
    *  @brief Deletes a routing table from the system along with the
    *         rule corresponding to that table.
    *
    *  @param deviceName       The name of the device whose table will be
    *                          deleted (Such as wlan or wwan)
    *  @return                 True if function is successful. False
    *                          otherwise.
    */
    bool deleteRoutingTable
    (
      uint8_t *deviceName
    );


    /**
    *  Displays the contents of all routing tables for debugging
    *  purposes.
    *
    *  @return                 True if function is successful. False
    *                          otherwise.
    */
    bool showAllRoutingTables
    (
      void
    );

    /**
    *  Displays the contents of the routing table associated with
    *  the inputted device name.
    *
    *  @param deviceName       The name of the device to be displayed
    *                          (Usually wlan or wwan)
    *  @return                 True if function is successful. False
    *                          otherwise.
    */
    bool showRoutingTable
    (
      uint8_t *deviceName
    );

    /**
    *  Displays the rules associated with all tables for debugging
    *  purposes.
    *
    *  @return                 True if function is successful. False
    *                          otherwise.
    */
    bool showRules
    (
      void
    );

  private:
    /* constructor */
    cnd_iproute2(){};
    /* destructor */
    ~cnd_iproute2(){};

    static cnd_iproute2* instancePtr;
};


#endif /* CND_IPROUTE2_H*/
