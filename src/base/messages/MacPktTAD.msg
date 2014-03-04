//**************************************************************************
// * file:        MacPktTAD.msg
// *
// * author:      Nguyen Van Thiep
// *
// * copyright:   (C) 2014 CAIRN - ENSSAT - IRISA
// *			  Lannion, 22300, France
// *
// *              This program is free software; you can redistribute it 
// *              and/or modify it under the terms of the GNU General Public 
// *              License as published by the Free Software Foundation; either
// *              version 2 of the License, or (at your option) any later 
// *              version.
// *              For further information see file COPYING 
// *              in the top level directory
// **************************************************************************
// * part of:     TAD (Traffic awared dynamic) Mac protocol
// * description: - Definie Mac package for TAD protocol
// **************************************************************************/

cplusplus {{
    #include "SimpleAddress.h"
    #include "MacPkt_m.h"
}}

class LAddress::L2Type extends void;
class MacPkt;

packet MacPktTAD extends MacPkt
{
//    LAddress::L2Type destAddr; // destination mac address
//    LAddress::L2Type srcAddr;  // source mac address
//    long             sequenceId; // Sequence Number to detect duplicate messages
	long           idle;  // The idle time that this node waited WB from receiver before send data
	int            wbMiss;  // The number wake up without receipt WB
}
