package org.mixim.modules.node;

//
// Example host module using a BMAC Network interface card.
//
module HostTADMAC extends WirelessNodeBattery
{
    parameters:
        arpType = default("org.mixim.modules.netw.ArpHost");
        nicType = default("org.mixim.modules.nic.NicTADMAC");
}
