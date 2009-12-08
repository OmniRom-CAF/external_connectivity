/*============================================================================
  FILE:         CRefCneRadio.cpp

  OVERVIEW:     The CRefCneRadio class provides means to control an air 
                interface upon creation of its object. Some of the methods
                such as bIsDataConnected, bIsConStateChanged, provide means to
                find out the current connectivity state of the radio
                
  DEPENDENCIES: The CRefCneRadio class is constructed for a unique air interface
                denoted by its RAT type. Once constructed, all other methods
                can be called on this object.
============================================================================*/

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


/*---------------------------------------------------------------------------
 * Extern Declarations
 *-------------------------------------------------------------------------*/
extern "C" void cnd_sendUnsolicitedMsg
  (
  int targetFd, 
  int msgType, 
  int dataLen, 
  void *data
  );

/*----------------------------------------------------------------------------
 * Includes
 * -------------------------------------------------------------------------*/
#include "CRefCneRadio.h"
#include "RefCneDefs.h"
#include "cne_svc.h"

/*=============================================================================
  FUNCTION      CRefCneRadio

  DESCRIPTION   Class constructor initializes class member variables

  DEPENDENCIES  None

  RETURN VALUE  CRefCneRadio instance

  SIDE EFFECTS  None
 ============================================================================*/
CRefCneRadio::CRefCneRadio
(
  cne_rat_type myRadio
):m_iRequestState(REF_CNE_NET_NOT_PENDING),
  m_iNetConState(REF_CNE_NET_STATE_UNINITIALIZED),
  m_iPrevNetConState(REF_CNE_NET_STATE_UNINITIALIZED)
{
  //Print to logcat: RefCne Radio constructor called
  /* set the command handlers */

  m_iMyRatType = myRadio;

  //Print to logcat: RefCne Radio constructed
}         
/*=============================================================================
  FUNCTION      CRefCneRadio

  DESCRIPTION   Copy constructor, not allowed.

  DEPENDENCIES  None

  RETURN VALUE  None

  SIDE EFFECTS  None
 ============================================================================*/
CRefCneRadio::CRefCneRadio(const CRefCneRadio& radio)
{
}
/*=============================================================================
  FUNCTION      bIsDataConnected

  DESCRIPTION   Querrys the Radio to see if it is connected

  DEPENDENCIES  None

  RETURN VALUE  TRUE, FALSE

  SIDE EFFECTS  None
 ============================================================================*/
bool CRefCneRadio::bIsDataConnected ()
{
  if(m_iNetConState == REF_CNE_NET_STATE_CONNECTED)
  {
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}
/*=============================================================================
  FUNCTION      iIsConActionPending

  DESCRIPTION   Querrys the Radio for a pending request

  DEPENDENCIES  None

  RETURN VALUE  TRUE, FALSE

  SIDE EFFECTS  None
 ============================================================================*/
int CRefCneRadio::iIsConActionPending
(
)
{
  return m_iRequestState;
}
/*=============================================================================
  FUNCTION      ClearPending

  DESCRIPTION   Clears the request pending flag

  DEPENDENCIES  None

  RETURN VALUE  None

  SIDE EFFECTS  None
 ============================================================================*/
void CRefCneRadio::ClearPending 
(
)
{
  m_iRequestState = REF_CNE_NET_NOT_PENDING;
}
/*=============================================================================
  FUNCTION      bIsConStateChanged

  DESCRIPTION   Querry the radio to see if the new status is different from
                previous

  DEPENDENCIES  None

  RETURN VALUE  None

  SIDE EFFECTS  None
 ============================================================================*/
bool CRefCneRadio::bIsConStateChanged
(
)
{
  if(m_iNetConState != m_iPrevNetConState)
  {
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}
/*=============================================================================
  FUNCTION      UpdateStatus

  DESCRIPTION   Maintains the previous and current state of the radio

  DEPENDENCIES  None

  RETURN VALUE  None

  SIDE EFFECTS  None
 ============================================================================*/
void CRefCneRadio::UpdateStatus
  (
  int myNetStatus
  )
{
  m_iNetState = (cne_network_state_enum_type )myNetStatus;
  m_iPrevNetConState = m_iNetConState;
  switch(myNetStatus)
  {
    case CNE_NETWORK_STATE_CONNECTED:
      {
        m_iNetConState = REF_CNE_NET_STATE_CONNECTED;
        break;
      }
    default:
      {
        m_iNetConState = REF_CNE_NET_STATE_DISCONNECTED;
      }
  }
  return;
}
/*=============================================================================
  FUNCTION      TurnOn

  DESCRIPTION   Turns the radio on

  DEPENDENCIES  None

  RETURN VALUE  None

  SIDE EFFECTS  None
 ============================================================================*/
void CRefCneRadio::TurnOn
(
)
{
  //Send Turn On command to Connectivity daemon
  cnd_sendUnsolicitedMsg
    (0, CNE_REQUEST_BRING_RAT_UP_MSG, sizeof(m_iMyRatType), &m_iMyRatType); 
  return;
}
/*=============================================================================
  FUNCTION      TurnOff

  DESCRIPTION   Turns the radio off

  DEPENDENCIES  None

  RETURN VALUE  None

  SIDE EFFECTS  None
 ============================================================================*/
void CRefCneRadio::TurnOff
(
)
{
  //Send Turn Off command to Connectivity daemon
  cnd_sendUnsolicitedMsg
    (0, CNE_REQUEST_BRING_RAT_DOWN_MSG, sizeof(m_iMyRatType), &m_iMyRatType); 
  return;
}
/*=============================================================================
  FUNCTION      SetPending

  DESCRIPTION   Sets the pending flag appropriately when radio is turned on 
                or off

  DEPENDENCIES  None

  RETURN VALUE  None

  SIDE EFFECTS  None
 ============================================================================*/
void CRefCneRadio::SetPending 
(
  ref_cne_net_con_req_enum_type flag
)
{
  m_iRequestState = flag;
}

//Implicit constructor
CRefCneRadio::CRefCneRadio
(
)
{
}


