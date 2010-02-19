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

/*============================================================================
  FILE:         cnd_iproute2.cpp

  OVERVIEW:     This program is an interface to make the necessary calls to
                iproute2 in order to set up and take down routing tables.
                These calls are made indirectly over the command line by using
                a call to the C++ system() function. For each routing device
                visible to the kernel, cnd_iproute2 allows one table. Each
                table contains one entry, a default path to the inputted
                routing device. A source address or network prefix is also
                required in order to instantiate a table, so that packets from
                that ip address are routed through the device. A gateway
                address can also be inputted optionally for a newly added
                routing table.

  DEPENDENCIES: None
============================================================================*/

/*----------------------------------------------------------------------------
 * Include Files
 * -------------------------------------------------------------------------*/
#include "cnd_iproute2.h"
#include <utils/Log.h>
#include <sys/types.h>
#include <cstdarg>
#include <map>
#include <set>

using namespace std;

/*----------------------------------------------------------------------------
 * Preprocessor Definitions and Constants
 * -------------------------------------------------------------------------*/
#undef LOG_TAG
#define LOG_TAG "CND_IPROUTE2"

/*----------------------------------------------------------------------------
 * Type Declarations
 * -------------------------------------------------------------------------*/
// List of all actions supported from iproute2. Should match defintions
// defined below prefixed with ACTIONS
enum Cmd_line_actions
{
  ACTIONS_ADD_ENUM,
  ACTIONS_DELETE_ENUM,
  ACTIONS_FLUSH_ENUM,
  ACTIONS_REPLACE_ENUM,
  ACTIONS_SHOW_ENUM
};

// Comparator function for use in the map of active interfaces.
struct deviceMapComparator {
  bool operator() (const uint8_t *string1, const uint8_t *string2) const
  {
    return (strcmp((char *)string1, (char *)string2) < 0);
  }
};

/** Stores information needed to create a routing table and a rule. This
 *  allows the calling class to delete that table without needing to
 *  keep track of any characteristics of the device other than its name.
 *  Assumes that there can only be 1 rule associated with any defined
 *  table.
 */
class DeviceInfo
{
  private:
    // Variables relating to the routing table
    int32_t tableNumber;
    uint8_t *deviceName;
    uint8_t *gatewayAddress;

    // Variables relating to the corresponding rule.
    uint8_t *sourcePrefix;
    int32_t priorityNumber;

    // Copies inputted pointer to permanent storage, returning the pointer to
    // the newly allocated space.
    uint8_t *storeDeviceInformation(uint8_t *parameterPtr)
    {

      if(parameterPtr == NULL)
      {
          LOGE("storeDeviceInformation: invalid parameter");
          return NULL;
      }
      uint8_t *deviceInfoPtr =
          new (nothrow) uint8_t[strlen((char*)parameterPtr) + 1];

      if (deviceInfoPtr == NULL )
      {
        LOGE("storeDeviceInformation: unable to allocate memory");
        return NULL;
      }
      int newByteLength = strlen((char *)parameterPtr) * sizeof(uint8_t);

      memcpy(deviceInfoPtr, parameterPtr, newByteLength + 1);
      deviceInfoPtr[newByteLength] = '\0';

      return deviceInfoPtr;
    }

  public:
    DeviceInfo
    (
      uint8_t *deviceName,
      int32_t tableNumber,
      uint8_t *gatewayAddress,
      uint8_t *sourcePrefix,
      int32_t priorityNumber
    )
    {
      DeviceInfo::deviceName = storeDeviceInformation(deviceName);
      DeviceInfo::tableNumber = tableNumber;
      if (('\0' != gatewayAddress) && (NULL != gatewayAddress))
      {
        DeviceInfo::gatewayAddress = storeDeviceInformation(gatewayAddress);
      }

      else
      {
        DeviceInfo::gatewayAddress = '\0';
      }

      DeviceInfo::sourcePrefix = storeDeviceInformation(sourcePrefix);
      DeviceInfo::priorityNumber = priorityNumber;
    }

    ~DeviceInfo()
    {
      delete [] DeviceInfo::deviceName;
      delete [] DeviceInfo::sourcePrefix;
      if (('\0' != DeviceInfo::gatewayAddress) && (NULL != gatewayAddress))
      {
        delete [] DeviceInfo::gatewayAddress;
      }
    }

    uint8_t* getDeviceName(void)
    {
      return deviceName;
    }

    uint8_t* getGatewayAddress(void)
    {
      return gatewayAddress;
    }

    int32_t getPriorityNumber(void)
    {
      return priorityNumber;
    }

    uint8_t* getSourcePrefix(void)
    {
      return sourcePrefix;
    }

    int32_t getTableNumber(void)
    {
      return tableNumber;
    }

    void setGatewayAddress(uint8_t *gatewayAddress)
    {
      if (('\0' != DeviceInfo::gatewayAddress) && (NULL != gatewayAddress))
      {
        delete [] DeviceInfo::gatewayAddress;
      }

      DeviceInfo::gatewayAddress = storeDeviceInformation(gatewayAddress);
    }

    void setSourcePrefix(uint8_t *sourcePrefix)
    {
      delete [] DeviceInfo::sourcePrefix;
      DeviceInfo::sourcePrefix = storeDeviceInformation(sourcePrefix);
    }
};

/*----------------------------------------------------------------------------
 * Global Data Definitions
 * -------------------------------------------------------------------------*/
// Set of all table numbers currently being used. Cannot contain more than
// MAX_TABLE_NUMBER - MIN_TABLE_NUMBER elements
set<int32_t> tableNumberSet;

// Maps the name of a device to its corresponding routing characteristics
map<uint8_t*, DeviceInfo*, deviceMapComparator> deviceMap;

// If a packet does not have an associated rule, it will go to the main
// routing table and be routed to the following device by default
DeviceInfo *defaultDevice;

/*----------------------------------------------------------------------------
 * Static Variable Definitions
 * -------------------------------------------------------------------------*/
// Commands to begin the command line string
static const uint8_t *ROUTING_CMD                = (uint8_t *)"ip route";
static const uint8_t *RULE_CMD                   = (uint8_t *)"ip rule";

// List of all actions supported from iproute2. These should match values in
// above enumeration 'Cmd_line_actions'
static const uint8_t *ACTIONS_ADD_STR            = (uint8_t *)"add";
static const uint8_t *ACTIONS_DELETE_STR         = (uint8_t *)"delete";
static const uint8_t *ACTIONS_FLUSH_STR          = (uint8_t *)"flush";
static const uint8_t *ACTIONS_REPLACE_STR        = (uint8_t *)"replace";
static const uint8_t *ACTIONS_SHOW_STR           = (uint8_t *)"show";

// Keywords used to refine calls to iproute2
static const uint8_t *CMD_LINE_DEVICE_NAME       = (uint8_t *)"dev";
static const uint8_t *CMD_LINE_GATEWAY_ADDRESS   = (uint8_t *)"via";
static const uint8_t *CMD_LINE_PRIORITY_NUMBER   = (uint8_t *)"priority";
static const uint8_t *CMD_LINE_SOURCE_PREFIX     = (uint8_t *)"from";
static const uint8_t *CMD_LINE_TABLE_NUMBER      = (uint8_t *)"table";

// Keywords that refer to specific routes or tables
static const uint8_t *ALL_TABLES                 = (uint8_t *)"all";
static const uint8_t *CACHED_ENTRIES             = (uint8_t *)"cache";
static const uint8_t *DEFAULT_ADDRESS            = (uint8_t *)"default";

// Table #1 is the first usable routing table
static const int32_t MIN_TABLE_NUMBER            = 1;

// Table #253 is the 'defined' default routing table, which should not
// be overwritten
static const int32_t MAX_TABLE_NUMBER            = 252;

// Priority number 32766 diverts packets to the main table (Table #254)
static const int32_t MAX_PRIORITY_NUMBER         = 32765;

// Max number of digits in a table number is 3
static const int32_t MAX_DIGITS_TABLE_NUMBER     = 3;

// Max number of digits in a priority number is 5
static const int32_t MAX_DIGITS_PRIORITY_NUMBER  = 5;

cnd_iproute2* cnd_iproute2::instancePtr = NULL;

/*-------------------------------------------------------------------------
 * Declaration for a non-member method.
 *-----------------------------------------------------------------------*/
void flushCache
(
  void
);

uint8_t* cmdLineActionEnumToString
(
  Cmd_line_actions commandAction
);

bool modifyDefaultRoute
(
  uint8_t *deviceName,
  Cmd_line_actions commandAction
);

bool modifyRoutingTable
(
  uint8_t *deviceName,
  uint8_t *sourcePrefix,
  uint8_t *gatewayAddress,
  Cmd_line_actions commandAction
);

bool modifyRule
(
  DeviceInfo *currentDevice,
  Cmd_line_actions commandAction
);

bool cmdLineCaller
(
  const uint8_t* cmdLineFirstWord,
  ...
);

/*----------------------------------------------------------------------------
 * FUNCTION      getInstance

   DESCRIPTION   Returns a pointer to an instance of the cnd_iproute2 such
                 that only 1 instance can be open at a time.

 * DEPENDENCIES  None

 * RETURN VALUE  cnd_iproute2*

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
cnd_iproute2* cnd_iproute2::getInstance
(
  void
)
{
  if(NULL == instancePtr)
  {
    instancePtr = new cnd_iproute2;
  }

  return instancePtr;
}

/*----------------------------------------------------------------------------
 * FUNCTION      cmdLineActionEnumToString

 * DESCRIPTION   Helper function to converts values of Cmd_line_actions enum
                 to a string.

 * DEPENDENCIES  None

 * RETURN VALUE  uint8_t*

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
uint8_t* cmdLineActionEnumToString
(
  Cmd_line_actions commandAction
)
{
  switch(commandAction)
  {
    case ACTIONS_ADD_ENUM:
      return (uint8_t *)ACTIONS_ADD_STR;
      break;
    case ACTIONS_DELETE_ENUM:
      return (uint8_t *)ACTIONS_DELETE_STR;
      break;
    case ACTIONS_FLUSH_ENUM:
      return (uint8_t *)ACTIONS_FLUSH_STR;
      break;
    case ACTIONS_REPLACE_ENUM:
      return (uint8_t *)ACTIONS_REPLACE_STR;
      break;
    case ACTIONS_SHOW_ENUM:
      return (uint8_t *)ACTIONS_SHOW_STR;
      break;
    default:
      LOGE("Unsupported conversion of command action to string");
      return '\0';
  }
}
/*----------------------------------------------------------------------------
 * FUNCTION      flushCache

 * DESCRIPTION   Flushes the cache after routing table entries are changed

 * DEPENDENCIES  None

 * RETURN VALUE  None

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
void flushCache
(
  void
)
{
  if (!cmdLineCaller(ROUTING_CMD,
                     cmdLineActionEnumToString(ACTIONS_FLUSH_ENUM),
                     CACHED_ENTRIES,
                     NULL))
  {
    LOGW("Attempt to flush the routing cache failed.");
  }
}

/*----------------------------------------------------------------------------
 * FUNCTION      modifyDefaultRoute

 * DESCRIPTION   Changes the default route given the name of the device that
                 will be the new default. The default case occurs if a packet
                 is sent from some source address not associated with a defined
                 table. When this occurs, the main table will route these
                 undefined source addresses to the gateway of the defined
                 default device. This function will add or delete that default
                 route in the main table. If a default route is being deleted,
                 no input is required for deviceName. The 'replace' command
                 will change the default entry already existing in the main
                 routing table, or add the entry if it does not exist.

 * DEPENDENCIES  commandAction should be either REPLACE OR DELETE

 * RETURN VALUE  bool - True if function is successful. False otherwise.

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
bool modifyDefaultRoute
(
  uint8_t *deviceName,
  Cmd_line_actions commandAction
)
{
  uint8_t *gatewayAddress;

  switch(commandAction)
  {
    case ACTIONS_REPLACE_ENUM:
    {
      if (('\0' == deviceName) || (NULL == deviceName))
      {
        LOGE("A null device name was passed while replacing the default table");
        return false;
      }

      // Case where the default device known by cnd is the same as the new
      // device that is replacing it.
      if (('\0' != defaultDevice) && (NULL != defaultDevice) &&
          (0 == strcmp((char *)defaultDevice->getDeviceName(),
                       (char *)deviceName)))
      {
        LOGW("The new default interface %s is the same as the one known by cnd",
             deviceName);
      }

      LOGI("Replacing default routing table with %s", deviceName);

      map<uint8_t*, DeviceInfo*>::iterator deviceMapIter;
      deviceMapIter = deviceMap.find(deviceName);

      if (deviceMapIter == deviceMap.end())
      {
        LOGE("Cannot make the nonexistant table %s the default.",
             deviceName);
        return false;
      }

      if (deviceMapIter->second == NULL) {
        LOGE("Adding a default table with no known device information");
        return false;
      }

      defaultDevice = deviceMapIter->second;

      LOGI("Default device has a stored name of %s.",
           defaultDevice->getDeviceName());
      break;
    }

    case ACTIONS_DELETE_ENUM:
    {
      // The following case should only be entered if the default table is
      // being deleted when no tables exist
      if (('\0' == defaultDevice) || (NULL == defaultDevice))
      {
        LOGE("No stored default device; use deleteDefaultEntryFromMainTable.");
        return false;
      }

      LOGI("Deleting default routing table");

      break;
    }

    default:
    {
      LOGE("Unsupported command action found while changing the default table");
      return false;
    }
  }

  gatewayAddress = defaultDevice->getGatewayAddress();

  // These commands may fail if the kernel has already executed an operation on
  // its own. Treat a call to modify the main table as if was successful.
  if (('\0' == gatewayAddress) || (NULL == gatewayAddress))
  { 
    cmdLineCaller(ROUTING_CMD,
                  cmdLineActionEnumToString(commandAction),
                  DEFAULT_ADDRESS,
                  CMD_LINE_DEVICE_NAME,
                  defaultDevice->getDeviceName(),
                  NULL);
  }
  else 
  {
    cmdLineCaller(ROUTING_CMD,
                  cmdLineActionEnumToString(commandAction),
                  DEFAULT_ADDRESS,
                  CMD_LINE_GATEWAY_ADDRESS,
                  gatewayAddress,
                  CMD_LINE_DEVICE_NAME,
                  defaultDevice->getDeviceName(),
                  NULL);
  }

  if (ACTIONS_DELETE_ENUM == commandAction)
  {
    // After a deletion, there should be no default device defined in the main
    // routing table
    defaultDevice = NULL;
  }

  flushCache();

  return true;
}

/*----------------------------------------------------------------------------
 * FUNCTION      modifyRoutingTable

 * DESCRIPTION   Adds or deletes a routing table given the name of the device
                 This routing table has one route, which will route all packets
                 to the device with the inputted name. This route can
                 optionally be set up to send packets through an inputted
                 gateway address. Once the table has been modified,
                 modifyRoutingTable will call another function to create or
                 delete a rule that maps some source address' packets to this
                 table.

 * DEPENDENCIES  commandAction should be either ADD OR DELETE

 * RETURN VALUE  bool - True if function is successful. False otherwise.

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
bool modifyRoutingTable
(
  uint8_t *deviceName,
  uint8_t *sourcePrefix,
  uint8_t *gatewayAddress,
  Cmd_line_actions commandAction
)
{
  int32_t tableNumber;
  int32_t priorityNumber;

  DeviceInfo *currentDevice;
  map<uint8_t*, DeviceInfo*>::iterator deviceMapIter;
  set<int32_t>::iterator tableNumberSetIter;

  if (('\0' == deviceName) || (NULL == deviceName))
  {
    LOGE("A null device name was passed while modifying a routing table");
    return false;
  }

  switch(commandAction)
  {
    case ACTIONS_ADD_ENUM:
    {
      LOGI("Adding a routing table for interface %s", deviceName);

      if (('\0' == sourcePrefix) || (NULL == sourcePrefix))
      {
        LOGE("A null source prefix was passed when adding the %s table",
             deviceName);
        return false;
      }

      if (('\0' == gatewayAddress) || (NULL == gatewayAddress))
      {
        LOGI("A null gateway address was passed when adding the %s table",
             deviceName);
      }

      deviceMapIter = deviceMap.find(deviceName);

      if ((deviceMapIter != deviceMap.end()) &&
          (NULL == deviceMapIter->second))
      {
        LOGW("Adding duplicate routing table with corrupt device information");
        deviceMap.erase(deviceName);
      }

      // If a call to add a routing table overwrites an existing table, the
      // new source and gateway addresses will overwrite the old ones.
      // However, calls to add a duplicate table, where the source and
      // gateway addresses do not change, are ignored and will simply return
      // true.
      else if (deviceMapIter != deviceMap.end())
      {
        DeviceInfo *existingDevice = deviceMapIter->second;

        int isNewSourcePrefix = strcmp((char *)existingDevice->getSourcePrefix(),
                                       (char *)sourcePrefix);

        uint8_t *existingGateway = existingDevice->getGatewayAddress();

        // Because the gateway address is an optional parameter, must account
        // for cases where the gateway address changes from null to non-null or
        // vice-versa
        if ( !(('\0' == existingGateway) || (NULL == existingGateway)) &&
              (('\0' == gatewayAddress)  || (NULL == gatewayAddress)) )
        {
          if ( ('\0' == gatewayAddress)  || (NULL == gatewayAddress) ||
               ('\0' == existingGateway) || (NULL == existingGateway) ||
               (0 != strcmp((char *)existingGateway,
                            (char *)gatewayAddress)))
          {
            // Replace active table and rule with changes to gateway address and
            // possibly the source prefix, if it has changed.
            commandAction = ACTIONS_REPLACE_ENUM;

            modifyRule(existingDevice, ACTIONS_DELETE_ENUM);

            existingDevice->setGatewayAddress(gatewayAddress);

            if (0 != isNewSourcePrefix)
            {
              existingDevice->setSourcePrefix(sourcePrefix);
            }

            tableNumber = existingDevice->getTableNumber();

            break;
          }
        }

        // Check for differences between source addresses. If a change in the
        // gateway address has already been detected, this step of modifying the
        // rule will be done implicitly.
        else if (0 != isNewSourcePrefix)
        {
          modifyRule(existingDevice, ACTIONS_DELETE_ENUM);
          existingDevice->setSourcePrefix(sourcePrefix);
          modifyRule(existingDevice, ACTIONS_ADD_ENUM);

          return true;
        }

        else {
          if (('\0' == gatewayAddress) || (NULL == gatewayAddress))
          {
            LOGI("Adding a duplicate %s table with source %s.",
                 deviceName, sourcePrefix);
            return true;
          }

          else {
            LOGI("Adding a duplicate %s table with gateway %s and source %s.",
                 deviceName, sourcePrefix, gatewayAddress);
            return true;
          }
        }
      }

      else {
        LOGI("Device '%s' not found as an active interface", deviceName);
      }

      // Instantiating more than 252 tables simultaneously is an error
      if (MAX_TABLE_NUMBER - MIN_TABLE_NUMBER < tableNumberSet.size())
      {
        LOGE("Too many tables exist to add %s. %d tables are defined",
             deviceName, tableNumberSet.size());
        return false;
      }

      // Locate next available table number. If the previous check passed,
      // there must be a table number available
      for (int32_t nextTableNumber = MIN_TABLE_NUMBER;
           nextTableNumber < MAX_TABLE_NUMBER; nextTableNumber++)
      {
        tableNumberSetIter = tableNumberSet.find(nextTableNumber);
        if (tableNumberSetIter == tableNumberSet.end())
        {
          tableNumber = nextTableNumber;
          break;
        }
      }

      // Always map the same rule to the same table number. This allows the
      // reuse of priority numbers.
      priorityNumber = MAX_PRIORITY_NUMBER - tableNumber + 1;

      currentDevice = new DeviceInfo(deviceName,
                                     tableNumber,
                                     gatewayAddress,
                                     sourcePrefix,
                                     priorityNumber);

      // Gateway address may be null, which is allowed. However, if an
      // optional gateway address was inputted, it will be lost in the
      // iproute2 call
      if ((NULL == currentDevice) ||
          (NULL == currentDevice->getDeviceName()) ||
          (NULL == currentDevice->getSourcePrefix()))
      {
        LOGE("Failed to allocate new device information while adding table %s.", 
             deviceName);
        return false;
      }

      break;
    }

    case ACTIONS_DELETE_ENUM:
    {
      LOGI("Deleting routing table for interface %s", deviceName);

      if (deviceMap.empty())
      {
        LOGE("Deleting a table when no table exists.");
        return false;
      }

      deviceMapIter = deviceMap.find(deviceName);

      if (deviceMapIter == deviceMap.end())
      {
        LOGE("Cannot delete table %s that has not been created.", deviceName);
        return false;
      }

      currentDevice = deviceMapIter->second;
      if (currentDevice == NULL) 
      {
        LOGE("Deleting table with a stored name and null value");
        return false;
      }

      gatewayAddress = currentDevice->getGatewayAddress();
      tableNumber = currentDevice->getTableNumber();
      break;
    }

    default:
    {
      LOGE("Unsupported command action found while modifying a table");
      return false;
    }
  }

  // Convert table number int to string, null-terminating the result
  char tableNumberString[MAX_DIGITS_TABLE_NUMBER+1];
  int32_t numberOfDigits = snprintf(tableNumberString,
                                    MAX_DIGITS_TABLE_NUMBER+1,
                                    "%d",
                                    tableNumber);
  tableNumberString[numberOfDigits] = '\0';

  if (('\0' == gatewayAddress) || (NULL == gatewayAddress))
  {    
    cmdLineCaller(ROUTING_CMD,
                  cmdLineActionEnumToString(commandAction),
                  DEFAULT_ADDRESS,
                  CMD_LINE_DEVICE_NAME,
                  deviceName,
                  CMD_LINE_TABLE_NUMBER,
                  (uint8_t *)tableNumberString,
                  NULL);
  }
  else
  {
    cmdLineCaller(ROUTING_CMD,
                  cmdLineActionEnumToString(commandAction),
                  DEFAULT_ADDRESS,
                  CMD_LINE_GATEWAY_ADDRESS,
                  gatewayAddress,
                  CMD_LINE_DEVICE_NAME,
                  deviceName,
                  CMD_LINE_TABLE_NUMBER,
                  (uint8_t *)tableNumberString,
                  NULL);
  }

  switch(commandAction)
  {
    // This case should not break to account for common code with the replace
    // command.
    case ACTIONS_ADD_ENUM:
    {
      deviceMap.insert(make_pair(currentDevice->getDeviceName(),currentDevice));
      tableNumberSet.insert(tableNumber);
    }

    case ACTIONS_REPLACE_ENUM:
    {
      // If there is no default table, the new device should be the default.
      if ('\0' == defaultDevice)
      {
        LOGI("Routing table added when no default exists. Adding new default.");
        modifyDefaultRoute(deviceName, ACTIONS_REPLACE_ENUM);
      }

      break;
    }

    case ACTIONS_DELETE_ENUM:
    {
      deviceMap.erase(deviceName);
      tableNumberSet.erase(tableNumber);

      // If there are no more tables, then there should be no default device.
      if (0 == tableNumberSet.size())
      {
        LOGI("Removing default table after no devices are known to be up");
        modifyDefaultRoute('\0', ACTIONS_DELETE_ENUM);
      }

      // If the default table has been deleted and another device is available,
      // set an arbitrary new device as the new default.
      else if (defaultDevice == currentDevice)
      {
        uint8_t *newDefaultName = deviceMap.begin()->first;

        LOGI("Replacing old default device with %s", newDefaultName);
        modifyDefaultRoute(newDefaultName, ACTIONS_REPLACE_ENUM);
      }

      break;
    }

    default:
      break;
  }

  // There is no 'ip rule replace' command. When a gateway address is changed,
  // must delete the rule and add it back.
  if (ACTIONS_REPLACE_ENUM == commandAction) {
    commandAction = ACTIONS_ADD_ENUM;
  }

  bool modifyRuleRetValue = modifyRule(currentDevice, commandAction);

  // Delete device information that will no longer be used
  if (ACTIONS_DELETE_ENUM == commandAction)
  {
    delete currentDevice;
  }

  return modifyRuleRetValue;
}

/*----------------------------------------------------------------------------
 * FUNCTION      modifyRule

 * DESCRIPTION   Adds or deletes a rule given the actual device object of the
                 table associated with that rule. Every defined routing table
                 requires some rule to map packets from some given source
                 address to that routing table. This function takes an object
                 so that after a routing table has been removed, the source
                 prefix, table number, and priority number associated with that
                 table can still be accessed. This allows a call to be made to
                 iproute2 to delete the corresponding rule.

 * DEPENDENCIES  commandAction should be either ADD OR DELETE

 * RETURN VALUE  bool - True if function is successful. False otherwise.

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
bool modifyRule
(
  DeviceInfo *currentDevice,
  Cmd_line_actions commandAction
)
{
  if (('\0' == currentDevice) || (NULL == currentDevice))
  {
    LOGE("A null device was passed while modifying a rule");
    return false;
  }

  uint8_t* deviceName = currentDevice->getDeviceName();
  map<uint8_t*, DeviceInfo*>::iterator deviceMapIter;
  deviceMapIter = deviceMap.find(deviceName);

  // If a rule is being added, its corresponding table should exist in the map
  // of all routing tables.
  if ((ACTIONS_ADD_ENUM == commandAction) &&
       (deviceMapIter == deviceMap.end()))
  {
    LOGE("Cannot %s a rule for nonexistant table %s",
         cmdLineActionEnumToString(commandAction),
         deviceName);
     return false;
  }

  int32_t tableNumber = currentDevice->getTableNumber();
  int32_t priorityNumber = currentDevice->getPriorityNumber();
  uint8_t *sourcePrefix = currentDevice->getSourcePrefix();

  // Convert table number & priority number ints to string, null-terminating
  // the results
  char tableNumberString[MAX_DIGITS_TABLE_NUMBER+1];
  char priorityNumberString[MAX_DIGITS_PRIORITY_NUMBER+1];

  int32_t numberOfDigits = snprintf(tableNumberString,
                                    MAX_DIGITS_TABLE_NUMBER+1,
                                    "%d",
                                    tableNumber);
  tableNumberString[numberOfDigits] = '\0';

  numberOfDigits = snprintf(priorityNumberString,
                            MAX_DIGITS_PRIORITY_NUMBER+1,
                            "%d",
                            priorityNumber);
  priorityNumberString[numberOfDigits] = '\0';

  cmdLineCaller(RULE_CMD,
                cmdLineActionEnumToString(commandAction),
                CMD_LINE_SOURCE_PREFIX,
                sourcePrefix,
                CMD_LINE_TABLE_NUMBER,
                (uint8_t *)tableNumberString,
                CMD_LINE_PRIORITY_NUMBER,
                (uint8_t *)priorityNumberString,
                NULL);

  flushCache();

  return true;
}

/*----------------------------------------------------------------------------
 * FUNCTION      cmdLineCaller

 * DESCRIPTION   Sends a call to iproute2 over the command line. This function
                 takes in a list of an arbitrary number of words, which is
                 parsed together into one final string. This string is sent
                 over the command line using the C routine 'system'. To see
                 the standard output of a failed command in the ADB logs, the
                 'logwrapper' utility must be used while instantiating the cnd
                 process.

 * DEPENDENCIES  Should not be any spaces in any inputted argument

 * RETURN VALUE  bool - True if function is successful. False otherwise.

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
bool cmdLineCaller
(
  const uint8_t* cmdLineFirstWord,
  ...
)
{
  size_t byteLength = 0;
  size_t memLength;
  int32_t numberOfSpaces = 0;
  va_list cmdLineWordList;
  uint8_t *nextWord;
  char *cmdLineString;

  if (('\0' == cmdLineFirstWord) || (NULL == cmdLineFirstWord))
  {
    LOGE("No actual command passed to build a command line.");
    return false;
  }

  // Find length of overall command line string to determine how much
  // space to allocate for it
  byteLength = strlen((char *)cmdLineFirstWord);
  va_start(cmdLineWordList, cmdLineFirstWord);

  while((nextWord = va_arg(cmdLineWordList,uint8_t*)) != NULL)
  {
    byteLength += strlen((char *)nextWord);
    numberOfSpaces++;
  }

  va_end(cmdLineWordList);

  // Allocate command line string, which is number of bytes in inputted words
  // plus the null character, plus the number of white spaces.
  cmdLineString = new (nothrow) char[byteLength + numberOfSpaces + 1];

  if (NULL == cmdLineString)
  {
    LOGE("Could not allocate memory to build command line string.");
    return false;
  }

  memLength = strlcpy(cmdLineString,
                      (char *)cmdLineFirstWord,
                      strlen((char *)cmdLineFirstWord) * sizeof(uint8_t) + 1);
  if (memLength > strlen((char *)cmdLineFirstWord) * sizeof(uint8_t) + 1)
  {
    LOGE("Failure building first word of command line string.");
    delete [] cmdLineString;
    return false;
  }

  // Build command line string containing each inputted word.
  va_start(cmdLineWordList, cmdLineFirstWord);

  while((nextWord = va_arg(cmdLineWordList,uint8_t*)) != NULL)
  {
    // Add white space
    memLength = strlcat(cmdLineString,
                        " ",
                        strlen(cmdLineString) * sizeof(char) + 
                        sizeof(uint8_t) + 1);
    if (memLength > strlen(cmdLineString) * sizeof(char) + sizeof(uint8_t) + 1)
    {
      LOGE("Failure adding whitespace to command line string.");
      delete [] cmdLineString;
      va_end(cmdLineWordList);
      return false;
    }

    // Add next word
    memLength = strlcat(cmdLineString,
                        (char *)nextWord,
                        strlen(cmdLineString) * sizeof(char) +
                        strlen((char *)nextWord) * sizeof(uint8_t) + 1);
    if (memLength > strlen(cmdLineString) * sizeof(char) +
                    strlen((char *)nextWord) * sizeof(uint8_t) + 1)
    {
      LOGE("Failure adding next word to command line string.");
      delete [] cmdLineString;
      va_end(cmdLineWordList);
      return false;
    }
  }

  va_end(cmdLineWordList);
 
  cmdLineString[byteLength + numberOfSpaces] = '\0';

  LOGI("Iproute2 will be called with: %s", cmdLineString);

  int cmdLineExitValue = system(cmdLineString);

  delete [] cmdLineString;

  if (0 != cmdLineExitValue)
  {
    LOGE("Command line call to iproute2 failed with exitvalue %d.",
         cmdLineExitValue);
    return false;
  }

  LOGI("Iproute2 successfully called.", cmdLineString);

  return true;
}

/*----------------------------------------------------------------------------
 * FUNCTION      addRoutingTable

 * DESCRIPTION   Adds a routing table to the system that contains a single
                 default entry, a route to the device with the inputted name,
                 which will optionally route through an inputted gateway
                 address. It also adds a rule to route a given source network
                 prefix or address to the new table.

                 The parameter deviceName refers to the name of the device
                 whose table will be added (Such as wlan or wwan)
                 The parameter sourcePrefix refers to the source network prefix
                 or address that will be routed to the device (Such as
                 37.214.21/24 or 10.156.45.1)

 * DEPENDENCIES  None

 * RETURN VALUE  bool - True if function is successful. False otherwise.

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
bool cnd_iproute2::addRoutingTable
(
  uint8_t *deviceName,
  uint8_t *sourcePrefix,
  uint8_t *gatewayAddress
)
{
  if (!modifyRoutingTable(deviceName,
                          sourcePrefix,
                          gatewayAddress,
                          ACTIONS_ADD_ENUM))
  {
    return false;
  }

  return true;
}

/*----------------------------------------------------------------------------
 * FUNCTION      changeDefaultTable

 * DESCRIPTION   Changes the default device where packets are routed to. If
                 some source address does not match an already defined rule,
                 packets from that source address will be routed through the
                 main table to some default device. This function replaces the
                 default route to direct traffic to an inputted, already
                 defined device. A routing table associated with this device
                 must have been added through addRoutingTable() before it can
                 be the default.

 * DEPENDENCIES  The new default table must have already been added via the
                 addRoutingTable API.

 * RETURN VALUE  bool - True if function is successful. False otherwise.

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
bool cnd_iproute2::changeDefaultTable
(
  uint8_t *deviceName
)
{
  if (!modifyDefaultRoute(deviceName, ACTIONS_REPLACE_ENUM))
  {
    return false;
  }

  return true;
}

/*----------------------------------------------------------------------------
 * FUNCTION      deleteRoutingTable

 * DESCRIPTION   Deletes a routing table from the system along with the rule
                 corresponding to that table.

 * DEPENDENCIES  The table must have already been added via the addRoutingTable
                 API.

 * RETURN VALUE  bool - True if function is successful. False otherwise.

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
bool cnd_iproute2::deleteRoutingTable
(
  uint8_t *deviceName
)
{
  if (!modifyRoutingTable(deviceName, '\0', '\0', ACTIONS_DELETE_ENUM))
  {
    return false;
  }

  return true;
}

/*----------------------------------------------------------------------------
 * FUNCTION      deleteDefaultEntryFromMainTable

 * DESCRIPTION   Deletes the default entry in the main table for the iputted
                 interface name.

 * DEPENDENCIES  None

 * RETURN VALUE  bool - True if function is successful. False otherwise.

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
bool cnd_iproute2::deleteDefaultEntryFromMainTable
(
  uint8_t *deviceName
)
{
  LOGI("Deleting %s interface from main table.", deviceName);

  if (!cmdLineCaller(ROUTING_CMD,
                     cmdLineActionEnumToString(ACTIONS_DELETE_ENUM),
                     DEFAULT_ADDRESS,
                     CMD_LINE_DEVICE_NAME,
                     deviceName,
                     NULL))
  {
    return false;
  }

  flushCache();

  return true;
}
/*----------------------------------------------------------------------------
 * FUNCTION      showAllRoutingTables

 * DESCRIPTION   Displays the contents of all routing tables for debugging
                 purposes.

 * DEPENDENCIES  None

 * RETURN VALUE  bool - True if function is successful. False otherwise.

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
bool cnd_iproute2::showAllRoutingTables
(
  void
)
{
  return cmdLineCaller(ROUTING_CMD,
                       cmdLineActionEnumToString(ACTIONS_SHOW_ENUM),
                       CMD_LINE_TABLE_NUMBER,
                       ALL_TABLES,
                       NULL);
}

/*----------------------------------------------------------------------------
 * FUNCTION      showRoutingTable

 * DESCRIPTION   Displays the contents of the routing table associated with
                 the inputted device name.

 * DEPENDENCIES  None

 * RETURN VALUE  bool - True if function is successful. False otherwise.

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
bool cnd_iproute2::showRoutingTable
(
  uint8_t *deviceName
)
{
  if (('\0' == deviceName) || (NULL == deviceName))
  {
    LOGE("A null device name was passed while displaying a table.");
    return false;
  }

  return cmdLineCaller(ROUTING_CMD,
                       cmdLineActionEnumToString(ACTIONS_SHOW_ENUM),
                       CMD_LINE_TABLE_NUMBER,
                       deviceName,
                       NULL);
}

/*----------------------------------------------------------------------------
 * FUNCTION      showRoutingTable

 * DESCRIPTION   Displays the rules associated with all tables for debugging
                 purposes.

 * DEPENDENCIES  None

 * RETURN VALUE  bool - True if function is successful. False otherwise.

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
bool cnd_iproute2::showRules
(
  void
)
{
  return cmdLineCaller(RULE_CMD,
                       cmdLineActionEnumToString(ACTIONS_SHOW_ENUM),
                       NULL);
}
