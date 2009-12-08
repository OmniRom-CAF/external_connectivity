/**----------------------------------------------------------------------------
  @file CRefCne.cpp

  
-----------------------------------------------------------------------------*/

/* Copyright (c) 2009, Code Aurora Forum. All rights reserved.
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


/*----------------------------------------------------------------------------
 * Preprocessor Definitions and Constants
 * -------------------------------------------------------------------------*/
#define TWO_RADIOS_ARE_CONNECTED    2
#define ONE_RADIO_IS_CONNECTED      1
#define ALL_RADIOS_ARE_DISCONNECTED 0

/*----------------------------------------------------------------------------
 * Include Files
 * -------------------------------------------------------------------------*/
#include "stdio.h"
#include "cne_svc.h"
#include "CRefCne.h"
#include "CRefCneRadio.h"
#include "RefCneDefs.h"

/*----------------------------------------------------------------------------
 * Static Member declarations
 * -------------------------------------------------------------------------*/
CRefCne* CRefCne::m_sInstancePtr = NULL;
cne_rat_type CRefCne::m_siPrefNetwork = CNE_RAT_NONE;

/*----------------------------------------------------------------------------
 * Type Declarations
 * -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Class Definitions
 * -------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------
 * FUNCTION      getInstance

 * DESCRIPTION   The user of this class will call this function to get an
                 instance of the class. All other public functions will be
                 called on this instance 

 * DEPENDENCIES  None

 * RETURN VALUE  an instance of CRefCne class

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
CRefCne* CRefCne::getInstance
(
)
{
  if (m_sInstancePtr == NULL)
  {
    m_sInstancePtr = new CRefCne;
  }
  return(m_sInstancePtr);
}
/*----------------------------------------------------------------------------
 * FUNCTION      Constructor

 * DESCRIPTION   Creates the RefCne object & initializes members appropriately 

 * DEPENDENCIES  None

 * RETURN VALUE  an instance of CRefCne class 

 * SIDE EFFECTS  RefCne object is created
 *--------------------------------------------------------------------------*/
CRefCne::CRefCne ()
{
  m_iNumActiveNetworks = NULL;
  RefCneWifi = new  CRefCneRadio(CNE_RAT_WLAN);
  RefCneWwan = new  CRefCneRadio(CNE_RAT_WWAN);
}
/*----------------------------------------------------------------------------
 * FUNCTION      RefCneCmdHdlr

 * DESCRIPTION   This the master command handler which calls specific handler
                 to handle a particular command sent by the daemon

 * DEPENDENCIES  None

 * RETURN VALUE  None

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
void CRefCne::RefCneCmdHdlr
(
  int cmd,
  int cmd_len,
  void* pCmdDataPtr
)
{
  cmd = (cne_cmd_enum_type) cmd;
  CRefCne* myself = getInstance();
  switch (cmd)
  {
    case CNE_NOTIFY_DEFAULT_NW_PREF_CMD:
      {
        RCNE_MSG_INFO("Command hdlr: Notify default"
                      " network pref cmd called [%d]",cmd);
        ref_cne_ret_enum_type ret = myself->SetPrefNetCmd(pCmdDataPtr);
        if (ret != REF_CNE_RET_OK)
        {
          //ASSERT(0);
        }
        break;
      }
    case CNE_REQUEST_UPDATE_WLAN_INFO_CMD:
      {
        RCNE_MSG_INFO("Command hdlr: Update Wifi info cmd called [%d]",cmd);
        ref_cne_ret_enum_type ret = myself->UpdateWlanInfoCmd(pCmdDataPtr);
        if (ret != REF_CNE_RET_OK)
        {
          //ASSERT(0);
        }
        break;
      }
    case CNE_REQUEST_UPDATE_WWAN_INFO_CMD:
      {
        RCNE_MSG_INFO("Command hdlr: Update WWAN info cmd called [%d]",cmd);
        ref_cne_ret_enum_type ret = myself->UpdateWwanInfoCmd(pCmdDataPtr);
        if (ret != REF_CNE_RET_OK)
        {
          //assert(0);
        }
        break;
      }
    default:
      {
        RCNE_MSG_ERROR("Command hdlr: Unrecognized command recvd [%d]",cmd);
      }
  }
  myself->ProcessStateChange();
}
/*----------------------------------------------------------------------------
 * FUNCTION      ProcessStateChange

 * DESCRIPTION   Processess the change of state of the connectivity engine
                 after the command received from the daemon is processed

 * DEPENDENCIES  None

 * RETURN VALUE  None

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
void CRefCne::ProcessStateChange
(
)
{
  RCNE_MSG_INFO("PSC:BEGIN processing state change");
  m_iNumActiveNetworks = 0;
  cne_rat_type  myPrefNet = GetPreferredNetwork();
  /* Check if the preferred network is set, if not then phone is
   * in boot up process, so do nothing */
  if (myPrefNet == NULL)
  {
    return;
  }

  CRefCneRadio* pref;
  CRefCneRadio* nonpref;
  if (myPrefNet == CNE_RAT_WLAN )
  {
    RCNE_MSG_DEBUG("PSC: Preferred RAT is Wifi, non-preferred RAT is WWAN");
    pref = RefCneWifi;
    nonpref = RefCneWwan;
  } else
  {
    RCNE_MSG_DEBUG("PSC: Preferred RAT is WWAN, non-preferred RAT is Wifi");
    pref = RefCneWwan;
    nonpref = RefCneWifi;
  }
  if (RefCneWifi->bIsDataConnected() == TRUE )
  {
    ++m_iNumActiveNetworks;
    RCNE_MSG_INFO("PSC: Wifi is in connected state");
  }
  if (RefCneWwan->bIsDataConnected() == TRUE )
  {
    ++m_iNumActiveNetworks;
    RCNE_MSG_INFO("PSC: WWAN is in connected state");
  }
  switch (m_iNumActiveNetworks)
  {
    case TWO_RADIOS_ARE_CONNECTED:
      /**
       *  If both Radios are up turn off the non-preferred network
       */
      {
        RCNE_MSG_DEBUG("PSC: both radios are up; disconnecting"
                       " non-preferred radio");
        nonpref->TurnOff();
        nonpref->SetPending(REF_CNE_NET_PENDING_DISCONNECT);
        break;
      }
    case ONE_RADIO_IS_CONNECTED:
      /**
       * If only one radio is up check if it is the preferred one,
       * if not then turn on the preferred network
       */
      {
        if (pref->bIsDataConnected() == FALSE)
        {
          RCNE_MSG_INFO("PSC: Non preferred radio is up; reconnecting"
                        " preferred radio");
          pref->TurnOn();
          pref->SetPending(REF_CNE_NET_PENDING_CONNECT);
        } else
        {
          RCNE_MSG_INFO("PSC: Preferred radio is connected");
        }
        break;
      }
    case ALL_RADIOS_ARE_DISCONNECTED:
      /**
       * If both networks are disconnected then try to bring up
       * both networks
       */
      {
        RCNE_MSG_INFO("All radios are disconnected; trying to reconnect");
        pref->TurnOn();
        pref->SetPending(REF_CNE_NET_PENDING_CONNECT);
        nonpref->TurnOn();
        nonpref->SetPending(REF_CNE_NET_PENDING_CONNECT);
        break;
      }
    default:
      {
        RCNE_MSG_WARN("PSC: number of active networks is invalid");
        //ASSERT(0);
      }
  }
}
/*----------------------------------------------------------------------------
 * FUNCTION      UpdateWlanInfoCmd

 * DESCRIPTION   The command handler for UpdateWlanInfo notification

 * DEPENDENCIES  None

 * RETURN VALUE  ref_cne_ret_enum_type

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
ref_cne_ret_enum_type CRefCne::UpdateWlanInfoCmd
(
  void* pWifiCmdData
)
{
  RCNE_MSG_DEBUG("UWLICH: Wlan update info cmd handler called");
  refCneWlanInfoCmdFmt *WlanInfoCmd;
  WlanInfoCmd  =   (refCneWlanInfoCmdFmt *)pWifiCmdData;
  if (WlanInfoCmd->status == NULL)
  {
    RCNE_MSG_ERROR("UWLICH: Invalid (==NULL) WLAN status received");
    return(REF_CNE_RET_ERROR);
  }
  RefCneWifi->UpdateStatus(WlanInfoCmd->status);
  if ( (RefCneWifi->bIsDataConnected() 
        && (RefCneWifi->iIsConActionPending()== REF_CNE_NET_PENDING_CONNECT) )
       || (!RefCneWifi->bIsDataConnected()
           && (RefCneWifi->iIsConActionPending()== REF_CNE_NET_PENDING_DISCONNECT) ) )
  {
    RCNE_MSG_DEBUG("UWLICH: Was in connection action pending state; clearing it");
    RefCneWifi->ClearPending();
  }
  RCNE_MSG_INFO("UWLICH: handled Wlan update info cmd");
  return(REF_CNE_RET_OK);
}
/*----------------------------------------------------------------------------
 * FUNCTION      UpdateWwanInfoCmd

 * DESCRIPTION   The command handler for UpdateWwanInfo notification

 * DEPENDENCIES  None

 * RETURN VALUE  ref_cne_ret_enum_type

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
ref_cne_ret_enum_type CRefCne::UpdateWwanInfoCmd
(
  void* pWwanCmdData
)
{
  RCNE_MSG_DEBUG("UWWICH: Wwan update info cmd handler called");
  refCneWwanInfoCmdFmt *WwanInfoCmd;
  WwanInfoCmd  =   (refCneWwanInfoCmdFmt *)pWwanCmdData;
  if (WwanInfoCmd->status == NULL)
  {
    RCNE_MSG_ERROR("UWWICH: Invalid (==NULL) WWAN status received");
    return(REF_CNE_RET_ERROR);
  }
  RefCneWwan->UpdateStatus(WwanInfoCmd->status);
  if ( (RefCneWwan->bIsDataConnected() 
        && (RefCneWwan->iIsConActionPending()== REF_CNE_NET_PENDING_CONNECT) )
       || (!RefCneWwan->bIsDataConnected()
           && (RefCneWwan->iIsConActionPending()== REF_CNE_NET_PENDING_DISCONNECT) ) )
  {
    RCNE_MSG_DEBUG("UWWICH: Was in connection action pending state; clearing it");
    RefCneWwan->ClearPending();
  }
  RCNE_MSG_INFO("UWWICH: handled Wwan update info cmd");
  return(REF_CNE_RET_OK);
}
/*----------------------------------------------------------------------------
 * FUNCTION      SetPrefNetCmd

 * DESCRIPTION   The command handler for set preferred network notification 

 * DEPENDENCIES  None

 * RETURN VALUE  ref_cne_ret_enum_type

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
ref_cne_ret_enum_type CRefCne::SetPrefNetCmd
(
  void* pPrefNetCmdData
)
{
  RCNE_MSG_DEBUG("SPNCH: Set preferred network command handler called");
  cne_rat_type *pPrefNetwork;
  pPrefNetwork = (cne_rat_type *)pPrefNetCmdData;
  if ( (*pPrefNetwork != CNE_RAT_WLAN)&&(*pPrefNetwork != CNE_RAT_WWAN) )
  {
    RCNE_MSG_ERROR("SPNCH: Invalid Network ID [%d] received",*pPrefNetwork);
    return(REF_CNE_RET_ERROR);
  }
  SetPreferredNetwork(pPrefNetwork);
  RCNE_MSG_DEBUG("SPNCH: handled set preferred network cmd");
  return(REF_CNE_RET_OK);
}
/*----------------------------------------------------------------------------
 * FUNCTION      SetPreferredNetwork

 * DESCRIPTION   Sets the desired network as the preferred network 

 * DEPENDENCIES  None

 * RETURN VALUE  None

 * SIDE EFFECTS  The default network for the system is changed
 *--------------------------------------------------------------------------*/
void CRefCne::SetPreferredNetwork
(
  cne_rat_type* pNetId
)
{
  m_siPrefNetwork = *pNetId;
  return;
}
/*----------------------------------------------------------------------------
 * FUNCTION      GetPreferredNetwork

 * DESCRIPTION   Informs the caller about which network is used as default 

 * DEPENDENCIES  None

 * RETURN VALUE  cne_rat_type

 * SIDE EFFECTS  None
 *--------------------------------------------------------------------------*/
cne_rat_type CRefCne::GetPreferredNetwork
(
)
{
  return(m_siPrefNetwork);
}

