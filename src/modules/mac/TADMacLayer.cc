//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

/**
 * Version 1.1: support multi senders
 */

#include "TADMacLayer.h"

#include <sstream>
#include <cassert>

#include "FWMath.h"
#include "MacToPhyControlInfo.h"
#include "BaseArp.h"
#include "BaseConnectionManager.h"
#include "PhyUtils.h"
#include "MacPkt_m.h"
#include "MacToPhyInterface.h"

Define_Module(TADMacLayer)

/**
 * Initialize method of TADMacLayer. Init all parameters, schedule timers.
 */
void TADMacLayer::initialize(int stage) {
    BaseMacLayer::initialize(stage);

    if (stage == 0) {
        BaseLayer::catDroppedPacketSignal.initialize();

        /* get sepecific parameters for TADMAC */
        role = static_cast<ROLES>(hasPar("role") ? par("role") : 1);

        wakeupInterval = hasPar("WUIInit") ? par("WUIInit") : 0.5;
        waitCCA = hasPar("waitCCA") ? par("waitCCA") : 0.1;
        waitWB = hasPar("waitWB") ? par("waitWB") : 0.3;
        waitACK = hasPar("waitACK") ? par("waitACK") : 0.3;
        waitDATA = hasPar("waitDATA") ? par("waitDATA") : 0.3;
        sysClock = hasPar("sysClock") ? par("sysClock") : 0.001;
        sysClockFactor = hasPar("sysClockFactor") ? par("sysClockFactor") : 75;
        alpha = hasPar("alpha") ? par("alpha") : 0.5;
        useCorrection = hasPar("useCorrection") ? par("useCorrection") : true;
        numberSender = hasPar("numberSender") ? par("numberSender") : 1;

        queueLength = hasPar("queueLength") ? par("queueLength") : 10;
        animation = hasPar("animation") ? par("animation") : true;
        bitrate = hasPar("bitrate") ? par("bitrate") : 15360.;
        headerLength = hasPar("headerLength") ? par("headerLength") : 10.;
        txPower = hasPar("txPower") ? par("txPower") : 50.;
        useMacAcks = hasPar("useMACAcks") ? par("useMACAcks") : false;
        maxTxAttempts = hasPar("maxTxAttempts") ? par("maxTxAttempts") : 2;
        debugEV << "headerLength: " << headerLength << ", bitrate: " << bitrate
                       << endl;

        stats = par("stats");
        nbTxDataPackets = 0;
        nbTxWB = 0;
        nbRxDataPackets = 0;
        nbRxWB = 0;
        nbMissedAcks = 0;
        nbRecvdAcks = 0;
        nbDroppedDataPackets = 0;
        nbTxAcks = 0;

        txAttempts = 0;
        lastDataPktDestAddr = LAddress::L2BROADCAST;
        lastDataPktSrcAddr = LAddress::L2BROADCAST;

        macState = INIT;

        // init the dropped packet info
        droppedPacket.setReason(DroppedPacket::NONE);
        nicId = getNic()->getId();
        WATCH(macState);
    } else if (stage == 1) {
        cPar fileNamePtr = par("logFileName");

        startTADMAC = new cMessage("startTADMAC");
        startTADMAC->setKind(TADMAC_START);

        wakeup = new cMessage("wakeup");
        wakeup->setKind(TADMAC_WAKE_UP);

        waitWBTimeout = new cMessage("waitWBTimeout");
        waitWBTimeout->setKind(TADMAC_WB_TIMEOUT);
        waitWBTimeout->setSchedulingPriority(100);

        receivedWB = new cMessage("receivedWB");
        receivedWB->setKind(TADMAC_RECEIVED_WB);

        ccaTimeout = new cMessage("ccaTimeout");
        ccaTimeout->setKind(TADMAC_CCA_TIMEOUT);
        ccaTimeout->setSchedulingPriority(100);

        sentData = new cMessage("sendData");
        sentData->setKind(TADMAC_SENT_DATA);

        resendData = new cMessage("resendData");
        resendData->setKind(TADMAC_RESEND_DATA);

        receivedACK = new cMessage("receivedACK");
        receivedACK->setKind(TADMAC_RECEIVED_ACK);

        ccaWBTimeout = new cMessage("ccaWBTimeout");
        ccaWBTimeout->setKind(TADMAC_CCA_WB_TIMEOUT);
        ccaWBTimeout->setSchedulingPriority(100);

        sentWB = new cMessage("sentWB");
        sentWB->setKind(TADMAC_SENT_WB);

        waitDATATimeout = new cMessage("waitDATATimeout");
        waitDATATimeout->setKind(TADMAC_DATA_TIMEOUT);
        waitDATATimeout->setSchedulingPriority(100);

        receivedDATA = new cMessage("receivedDATA");
        receivedDATA->setKind(TADMAC_RECEIVED_DATA);

        ccaACKTimeout = new cMessage("ccaACKTimeout");
        ccaACKTimeout->setKind(TADMAC_CCA_ACK_TIMEOUT);
        ccaACKTimeout->setSchedulingPriority(100);

        sentACK = new cMessage("sentACK");
        sentACK->setKind(TADMAC_SENT_ACK);

        if (role == NODE_RECEIVER) {
//            log_wakeupInterval.open(fileNamePtr.stringValue());

            int nodeIdx = getNode()->getIndex();
            TSR_length = 4;
            // allocate memory & initialize for TSR bank
            TSR_bank = new int*[numberSender+1];
            for (int i = 1; i <= numberSender; i++) {
                TSR_bank[i] = new int[TSR_length];
                for (int j = 0; j < TSR_length; j++) {
                    TSR_bank[i][j] = 0;
                }
            }

            logFile = new ofstream[numberSender];
            /**
             * Define route table here. Because we don't use high level so we need to fix the network topologie
             * node[0] is receiver, mac address is 00:00:00:00:00:00
             * node[1->4] is sender, mac address is from 00:00:00:00:00:01 to 00:00:00:00:00:04
             * node[5] is receiver, mac address is 00:00:00:00:00:05
             * node[6->9] is sender, mac address is from 00:00:00:00:00:06 to 00:00:00:00:00:09
             */
            routeTable = new LAddress::L2Type[numberSender];
            for (int i = 1; i <= numberSender; i++) {
                ostringstream converter;
                converter << "00:00:00:00:00:0" << (i + nodeIdx);
                routeTable[i].setAddress(converter.str().c_str());

                // Create file to log wakeup interval
                converter.clear();
                converter << fileNamePtr.stringValue() << "_" << (i + nodeIdx) << ".csv";
                logFile[i].open(converter.str().c_str());
            }

            // allocate memory & initialize for nodeWakeupInterval & nextWakeupIntervalTime
            nodeWakeupInterval = new double[numberSender+1];
            nextWakeupTime = new simtime_t[numberSender+1];
            for (int i = 1; i <= numberSender; i++) {
                nodeWakeupInterval[i] = wakeupInterval;
                nextWakeupTime[i] = (rand() % 1000 + 1) / 1000.0;
            }

            // allocate memory & initialize for nodeIdle
            nodeIdle = new int*[numberSender+1];
            nodeIndex = new int[numberSender+1];
            nodeNumberWakeup = new int[numberSender+1];
            for (int i = 1; i <= numberSender; i++) {
                nodeIndex[i] = 0;
                nodeNumberWakeup[i] = 0;
                nodeIdle[i] = new int[2];
                for (int j = 0; j < 2; j++) {
                    nodeIdle[i][j] = 0;
                }
            }
        } else {
            log_wb.open(fileNamePtr.stringValue());
        }
//        cout << wakeupInterval << endl;
//        wakeupInterval /= 1000;

        idle_array[0] = idle_array[1] = 0;
        TSR_length = 4;

        for (int i = 0; i < TSR_length; i++) {
                TSR[i] = 0;
        }

        scheduleAt(0.0, startTADMAC);
    }
}

TADMacLayer::~TADMacLayer() {
    cancelAndDelete(startTADMAC);
    cancelAndDelete(wakeup);
    cancelAndDelete(waitWBTimeout);
    cancelAndDelete(receivedWB);
    cancelAndDelete(ccaTimeout);
    cancelAndDelete(sentData);
    cancelAndDelete(resendData);
    cancelAndDelete(receivedACK);
    cancelAndDelete(ccaWBTimeout);
    cancelAndDelete(sentWB);
    cancelAndDelete(waitDATATimeout);
    cancelAndDelete(receivedDATA);
    cancelAndDelete(ccaACKTimeout);
    cancelAndDelete(sentACK);

    MacQueue::iterator it;
    for (it = macQueue.begin(); it != macQueue.end(); ++it) {
        delete (*it);
    }
    macQueue.clear();
}

void TADMacLayer::finish() {
    BaseMacLayer::finish();

    if (role == NODE_RECEIVER) {
        log_wakeupInterval.close();
    }
    // record stats
    if (stats) {
        recordScalar("nbTxDataPackets", nbTxDataPackets);
        recordScalar("nbTxPreambles", nbTxWB);
        recordScalar("nbRxDataPackets", nbRxDataPackets);
        recordScalar("nbRxPreambles", nbRxWB);
        recordScalar("nbMissedAcks", nbMissedAcks);
        recordScalar("nbRecvdAcks", nbRecvdAcks);
        recordScalar("nbTxAcks", nbTxAcks);
        recordScalar("nbDroppedDataPackets", nbDroppedDataPackets);
        recordScalar("WUInt", wakeupInterval);
        //recordScalar("timeRX", timeRX);
        //recordScalar("timeTX", timeTX);
    }
}

/**
 * Check whether the queue is not full: if yes, print a warning and drop the
 * packet. Then initiate sending of the packet, if the node is sleeping. Do
 * nothing, if node is working.
 */
void TADMacLayer::handleUpperMsg(cMessage *msg) {
    delete msg;
    return;
    if (role == NODE_RECEIVER) {
        return;
    }
    bool pktAdded = addToQueue(msg);
    if (!pktAdded)
        return;
    // force wakeup now - in sender node, we wake up on demand to transmit data
    if (macState == SLEEP) {
        if (wakeup->isScheduled()) {
            cancelEvent(wakeup);
        }
        scheduleAt(simTime(), wakeup);
    }
}

/**
 * Send wakeup beacon from receiver to sender
 */
void TADMacLayer::sendWB() {
    /**
     * For multi sender, the WB packet must send to exactly sender, cannot broadcast
     */
    macpkt_ptr_t wb = new MacPkt();
    wb->setSrcAddr(myMacAddr);
    //wb->setDestAddr(LAddress::L2BROADCAST);
    wb->setDestAddr(routeTable[currentNode]);
    wb->setKind(TADMAC_WB);
    wb->setBitLength(headerLength);

    //attach signal and send down
    attachSignal(wb);
    sendDown(wb);
    nbTxWB++;
}

/**
 * Send one short preamble packet immediately.
 */
void TADMacLayer::sendMacAck() {
    macpkt_ptr_t ack = new MacPkt();
    ack->setSrcAddr(myMacAddr);
    ack->setDestAddr(lastDataPktSrcAddr);
    ack->setKind(TADMAC_ACK);
    ack->setBitLength(headerLength);

    //attach signal and send down
    attachSignal(ack);
    sendDown(ack);
    nbTxAcks++;
    //endSimulation();
}

/**
 * Handle message in sender
 *
 */
void TADMacLayer::handleSelfMsgSender(cMessage *msg) {
    switch (macState) {
        // Call at first time after initialize the note
        case INIT:
            if (msg->getKind() == TADMAC_START) {
                changeDisplayColor(BLACK);
                phy->setRadioState(MiximRadio::SLEEP);
                macState = SLEEP;
                // Because we have multi sender so we avoid all sender start at same moment
                scheduleAt(simTime() + dblrand(), wakeup);
                return;
            }
            break;
        // This node is sleeping & time to wakeup
        case SLEEP:
            if (msg->getKind() == TADMAC_WAKE_UP) {
                // change icon to green light -> note is wait for sign
                changeDisplayColor(GREEN);
                // set antenna to receiving sign state
                phy->setRadioState(MiximRadio::RX);
                // MAC state is CCA
                macState = WAIT_WB;
                // schedule the event wait WB timeout
                scheduleAt(simTime() + waitWB, waitWBTimeout);
                // store the moment that this node is wake up
                start = simTime();
                // reset number resend data
                txAttempts = 0;
                return;
            }
            break;
        // The sender is in state WAIT_WB & receive a message
        case WAIT_WB:
            if (msg->getKind() == TADMAC_WB_TIMEOUT) {
                // change icon to black light -> note is inactive
                changeDisplayColor(BLACK);
                // Change antenna to sleep state
                phy->setRadioState(MiximRadio::SLEEP);
                macState = SLEEP;
                // schedule the next wakeup event
//                if (start + wakeupInterval < simTime()) {
//                    start += wakeupInterval * floor((simTime() - start) / wakeupInterval);
//                }
                scheduleAt(start + wakeupInterval, wakeup);
                // log the time wait for WB
                timeWaitWB = simTime() - start;
                log_wb << start << "," << round(timeWaitWB.dbl() * 1000) << "," << 0 << endl;
                return;
            }
            // duration the WAIT_WB, received the WB message -> change to CCA state & schedule the timeout event
            if (msg->getKind() == TADMAC_WB) {
                macpkt_ptr_t            mac  = static_cast<macpkt_ptr_t>(msg);
                const LAddress::L2Type& dest = mac->getDestAddr();
                // Do nothing if receive WB for other node
                if (dest != myMacAddr) {
                    return;
                }
                // Receiver is the node which send WB packet
                receiverAddress = mac->getSrcAddr();
                nbRxWB++;
                macState = CCA;
                // Don't need to call the event to handle WB timeout
                cancelEvent (waitWBTimeout);
                // schedule the CCA timeout event
                scheduleAt(simTime() + waitCCA, ccaTimeout);
                // log the time wait for WB
                timeWaitWB = simTime() - start;
                log_wb << start << "," << round(timeWaitWB.dbl() * 1000) << "," << 1 << endl;
                // reset ccaAttempts
                ccaAttempts = 0;
                delete msg;
                msg = NULL;
                return;
            }
            // during this periode, this node receive data packet from other node, do nothing, wait right WB
            if (msg->getKind() == TADMAC_DATA) {
                return;
            }
            break;

        case CCA:
            if (msg->getKind() == TADMAC_CCA_TIMEOUT) {
                changeDisplayColor(YELLOW);
                // set antenna to sending sign state
                phy->setRadioState(MiximRadio::TX);
                // send the data packet
                sendDataPacket();
                // change mac state to send data
                macState = SEND_DATA;
                // in this case, we don't need to schedule the event to handle when data is sent
                // this event will be call when the physic layer finished
                return;
            }
            if (msg->getKind() == TADMAC_WB || msg->getKind() == TADMAC_DATA) {
                delete msg;
                msg = NULL;
                if (ccaAttempts < maxCCAattempts) {
                    ccaAttempts++;
                    cancelEvent(ccaTimeout);
                    scheduleAt(simTime() + waitCCA, ccaTimeout);
                    return;
                } else {
                    cancelEvent(ccaTimeout);
                    // The channel is busy so return to sleep state
                    // change icon to black light -> note is inactive
                    changeDisplayColor(BLACK);
                    // Change antenna to sleep state
                    phy->setRadioState(MiximRadio::SLEEP);
                    macState = SLEEP;
                    scheduleAt(start + wakeupInterval, wakeup);
                    return;
                }
            }
            break;

        case SEND_DATA:
            // Finish send data to receiver
            if (msg->getKind() == TADMAC_SENT_DATA) {
                // change icon to green light -> note is wait for sign
                changeDisplayColor(GREEN);
                // set antenna to state receive sign
                phy->setRadioState(MiximRadio::RX);
                // MAC state is CCA
                macState = WAIT_ACK;
                // schedule the event wait WB timeout
                scheduleAt(simTime() + waitACK, resendData);
                return;
            }
            break;

        case WAIT_ACK:
            if (msg->getKind() == TADMAC_RESEND_DATA) {
                // if the number resend data is not reach max time
                if (txAttempts < maxTxAttempts) {
                    // No ACK received. try to send data again.
                    changeDisplayColor(GREEN);
                    phy->setRadioState(MiximRadio::RX);
                    txAttempts++;
                    macState = WAIT_WB;
                    scheduleAt(simTime() + waitWB, waitWBTimeout);
                    // store the moment to wait WB
                    timeWaitWB = simTime();
                    nbMissedAcks++;
                } else {
                    // No ACK received. try to send data again.
                    changeDisplayColor(BLACK);
                    phy->setRadioState(MiximRadio::SLEEP);
                    macState = SLEEP;
                    // schedule the next wakeup event
                    if (start + wakeupInterval < simTime()) {
                        start += wakeupInterval * floor((simTime() - start) / wakeupInterval);
                    }
                    scheduleAt(start + wakeupInterval, wakeup);
                    nbMissedAcks++;
                }
                return;
            }
            // received ACK -> change to sleep, schedule next wakeup time
            if (msg->getKind() == TADMAC_RECEIVED_ACK || msg->getKind() == TADMAC_ACK) {
                changeDisplayColor(BLACK);
                phy->setRadioState(MiximRadio::SLEEP);
                macState = SLEEP;
                // schedule the next wakeup event
                if (start + wakeupInterval < simTime()) {
                    start += wakeupInterval * floor((simTime() - start) / wakeupInterval);
                }
                scheduleAt(start + wakeupInterval, wakeup);
                //remove event wait ack timeout
                cancelEvent(resendData);
                return;
            }
            break;
    }
    opp_error("Undefined event of type %d in state %d (Radio state %d)!",
            msg->getKind(), macState, phy->getRadioState());
}

/**
 * Handle message in receiver
 *
 */
void TADMacLayer::handleSelfMsgReceiver(cMessage *msg) {
    switch (macState) {
        // Call at first time after initialize the note
        case INIT:
            if (msg->getKind() == TADMAC_START) {
                changeDisplayColor(BLACK);
                phy->setRadioState(MiximRadio::SLEEP);
                macState = SLEEP;
                simtime_t nextWakeup = 1000.0;
                for (int i = 1; i <= numberSender; i++) {
                    if (nextWakeup < nextWakeupTime[i]) {
                        nextWakeup = nextWakeupTime[i];
                        currentNode = i;
                    }
                }
                scheduleAt(nextWakeup, wakeup);
                numberWakeup = 0;
                return;
            }
            break;
        // This node is sleeping & time to wakeup
        case SLEEP:
            if (msg->getKind() == TADMAC_WAKE_UP) {
                // change icon to green light -> note is wait for sign
                changeDisplayColor(GREEN);
                // set antenna to receiving sign state
                phy->setRadioState(MiximRadio::RX);
                // MAC state is CCA
                macState = CCA_WB;
                // schedule the event wait WB timeout
                start = simTime();
                // reset CCA attempts
                ccaAttempts = 0;
                scheduleAt(start + waitCCA, ccaWBTimeout);
//                numberWakeup++;
//                log_wakeupInterval << numberWakeup << "," << round(start.dbl() * 1000) << "," << round(wakeupInterval * 1000) << endl;

                nodeNumberWakeup[currentNode]++;
                logFile[currentNode] << nodeNumberWakeup[currentNode] << ","
                        << round(start.dbl() * 1000) << ","
                        << round(nodeWakeupInterval[currentNode] * 1000) << endl;
                return;
            }
            break;
        //
        case CCA_WB:
            if (msg->getKind() == TADMAC_CCA_WB_TIMEOUT) {
                // change icon to black light -> note is inactive
                changeDisplayColor(YELLOW);
                // Change antenna to sleep state
                phy->setRadioState(MiximRadio::TX);
                // Send WB to sender
                sendWB();
                macState = SEND_WB;
                // Don't need to schedule sent wb event here, this event will be call when physical layer finish
                return;
            } else {
                // If CCA attempts don't reach maximum time
                if (ccaAttempts < maxCCAattempts) {
                    ccaAttempts++;
                    // Cancel current event
                    cancelEvent(ccaWBTimeout);
                    // reschedule check channel free to send WB
                    scheduleAt(start + waitCCA, ccaWBTimeout);
                } else {
                    /**
                     * @TODO: set 0 to TSR or not. Now we do nothing
                     */
                    // calculate the next wakeup time for this current node
                    nextWakeupTime[currentNode]+= nodeWakeupInterval[currentNode];
                    // Cancel current event
                    cancelEvent(ccaWBTimeout);
                    // find the next wakeup time
                    changeDisplayColor(BLACK);
                    phy->setRadioState(MiximRadio::SLEEP);
                    macState = SLEEP;
                    simtime_t nextWakeup = 1000.0;
                    for (int i = 1; i <= numberSender; i++) {
                        // Check if already passed the wakeup time for a node
                        if (nextWakeupTime[i] < simTime()) {
                            nextWakeupTime[i] += nodeWakeupInterval[i];
                        }
                        if (nextWakeup < nextWakeupTime[i]) {
                            nextWakeup = nextWakeupTime[i];
                            currentNode = i;
                        }
                    }
                    scheduleAt(nextWakeup, wakeup);
                    return;
                }
            }
            break;

        case SEND_WB:
            if (msg->getKind() == TADMAC_SENT_WB) {
                changeDisplayColor(GREEN);
                // set antenna to sending sign state
                phy->setRadioState(MiximRadio::RX);
                // change mac state to send data
                macState = WAIT_DATA;
                // Schedule wait data timeout event
                scheduleAt(simTime() + waitDATA, waitDATATimeout);
                return;
            }
            break;

        case WAIT_DATA:
            // if wait data timeout -> go to sleep, calculate the next wakeup interval
            if (msg->getKind() == TADMAC_DATA_TIMEOUT) {
                // change icon to green light -> note is wait for sign
                changeDisplayColor(BLACK);
                // set antenna to state receive sign
                phy->setRadioState(MiximRadio::SLEEP);
                // MAC state is CCA
                macState = SLEEP;
                // Calculate next wakeup interval
                calculateNextInterval();

                // Find next wakeup time
                simtime_t nextWakeup = 1000.0;
                for (int i = 1; i <= numberSender; i++) {
                    // Check if already passed the wakeup time for a node
                    if (nextWakeupTime[i] < simTime()) {
                        nextWakeupTime[i] += nodeWakeupInterval[i];
                    }
                    if (nextWakeup < nextWakeupTime[i]) {
                        nextWakeup = nextWakeupTime[i];
                        currentNode = i;
                    }
                }
                scheduleAt(nextWakeup, wakeup);
                return;
            }
            // Receive data -> wait CCA to send ACK
            if (msg->getKind() == TADMAC_DATA) {
                nbRxDataPackets++;
                macpkt_ptr_t            mac  = static_cast<macpkt_ptr_t>(msg);
                const LAddress::L2Type& dest = mac->getDestAddr();
                const LAddress::L2Type& src  = mac->getSrcAddr();
                // If this data packet destination is not for this receiver
                // wait for right data packet
                if (dest != myMacAddr) {
                    return;
                }

                // Calculate next wakeup interval
                calculateNextInterval(msg);

                mac = NULL;
                delete msg;
                msg = NULL;

                // if use ack
                if (useMacAcks) {
                    macState = CCA_ACK;
                    lastDataPktSrcAddr = src;
                    phy->setRadioState(MiximRadio::RX);
                    changeDisplayColor(GREEN);
                    // reset cca attempt number
                    ccaAttempts = 0;
                    scheduleAt(simTime() + waitCCA, ccaACKTimeout);
                    return;
                } else {
                    // change icon to black light -> note change to sleep state
                    changeDisplayColor(BLACK);
                    // set antenna to sleep state
                    phy->setRadioState(MiximRadio::SLEEP);
                    // MAC state is SLEEP
                    macState = SLEEP;
                    // Find next wakeup time
                    simtime_t nextWakeup = 1000.0;
                    for (int i = 1; i <= numberSender; i++) {
                        // Check if already passed the wakeup time for a node
                        if (nextWakeupTime[i] < simTime()) {
                            nextWakeupTime[i] += nodeWakeupInterval[i];
                        }
                        if (nextWakeup < nextWakeupTime[i]) {
                            nextWakeup = nextWakeupTime[i];
                            currentNode = i;
                        }
                    }
                    scheduleAt(nextWakeup, wakeup);
                    return;
                }
            }
            break;

        case CCA_ACK:
            if (msg->getKind() == TADMAC_CCA_ACK_TIMEOUT) {
                // Radio is free to send ACK
                debugEV << "State CCA_ACK, message TADMAC_CCA_ACK_TIMEOUT, new state SEND_ACK" << endl;
                changeDisplayColor(YELLOW);
                phy->setRadioState(MiximRadio::TX);
                sendMacAck();
                macState = SEND_ACK;
                return;
            } else {
            }
            break;
        case SEND_ACK:
            if (msg->getKind() == TADMAC_SENT_ACK) {
                debugEV << "State SEND_ACK, message TADMAC_SENT_ACK, new state SLEEP" << endl;
                // change icon to green light -> note is wait for sign
                changeDisplayColor(BLACK);
                // set antenna to state receive sign
                phy->setRadioState(MiximRadio::SLEEP);
                // MAC state is CCA
                macState = SLEEP;

                if (simTime() - start > wakeupInterval) {
                    start += wakeupInterval;
                }
                // schedule the event wait WB timeout
                scheduleAt(start + wakeupInterval, wakeup);
                return;
            }
            break;
    }
    opp_error("Undefined event of type %d in state %d (Radio state %d)!",
            msg->getKind(), macState, phy->getRadioState());
}

/**
 *
 */
void TADMacLayer::calculateNextInterval(cMessage *msg) {
    int n01, n11, nc01, nc11;
    n01 = n11 = nc01 = nc11 = 0;
    int n02, n12, nc02, nc12;
    n02 = n12 = nc02 = nc12 = 0;
    double x1, x2;
    x1 = x2 = 0;
    //log_tsr << "================================================" << endl;
    //log_tsr << "old TSR: ";
    // Move the array TSR to left to store the new value in TSR[TSR_lenth - 1]
    for (int i = 0; i < TSR_length - 1; i++) {
        //log_tsr << TSR[i];
        TSR[i] = TSR[i + 1];
    }
    //log_tsr << TSR[TSR_length - 1] << endl;
    TSR[TSR_length - 1] = (msg == NULL) ? 0 : 1;

    //log_tsr << "old WUInterval: " << wakeupInterval << endl;
    // Calculate X1;
    for (int i = 0; i < TSR_length / 2; i++) {
        if (TSR[i] == 1) {
            n11++;
            if (i > 0 && TSR[i - 1] == 1) {
                nc11++;
            }
        } else {
            n01++;
            if (i > 0 && TSR[i - 1] == 0) {
                nc01++;
            }
        }
    }
    x1 = n01 * nc01 * 2 / TSR_length - n11 * nc11 * 2 / TSR_length;
    //log_tsr << "n01: " << n01 << " | nc01: " << nc01 << " | n11: " << n11 << " | nc11: " << nc11 << " | x1: " << x1 << endl;
    // Calculate X2
    for (int i = TSR_length / 2; i < TSR_length; i++) {
        if (TSR[i] == 1) {
            n12++;
            if (TSR[i - 1] == 1) {
                nc12++;
            }
        } else {
            n02++;
            if (TSR[i - 1] == 0) {
                nc02++;
            }
        }
    }
    x2 = n02 * nc02 * 2 / TSR_length - n12 * nc12 * 2 / TSR_length;
    //log_tsr << "n02: " << n02 << " | nc02: " << nc02 << " | n12: " << n12 << " | nc12: " << nc12 << " | x2: " << x2 << endl;

    // calculate the traffic weighting
    double mu = alpha * x1 + (1 - alpha) * x2;
    //log_tsr << "mu: " << mu << endl;
    if (useCorrection) {
        // calculate the correlator see code matlab in IWCLD_11/First_paper/Adaptive_MAC/Done_Codes/Test_For_IWCLD.m
        if (mu * 100 == 0) {
            double idle = 0;
            if (msg != NULL) {
                macpkttad_ptr_t mac  = static_cast<macpkttad_ptr_t>(msg);
                idle = double(mac->getIdle()) / 1000.0;
                mac = NULL;
                idle_array[idx] = idle;
                idx++;
            }
            if (idle_array[0] != 0 && idle_array[1] != 0) {
                double WUInt_diff = (idle_array[0] - idle_array[1]) / 2;
                if (WUInt_diff * 100 != 0) {
                    wakeupIntervalLook = wakeupInterval + WUInt_diff;
                    wakeupInterval = (wakeupIntervalLook - idle + sysClock);
                    if (wakeupInterval < 0) {
                        wakeupInterval += wakeupIntervalLook;
                        // Move the array TSR to left to store the new value in TSR[TSR_lenth - 1]
                        for (int i = 0; i < TSR_length - 1; i++) {
                            TSR[i] = TSR[i + 1];
                        }
                        TSR[TSR_length - 1] = 0;
                    }
                    first_time++;
                }
                idle_array[0] = idle_array[1] = 0;
                idx = 0;
            }
        } else {
            if (idx == 1) {
                idx--;
            }
            if (wakeupIntervalLook * 100 == 0) {
                wakeupInterval += mu * sysClockFactor * sysClock;
                wakeupInterval = round(wakeupInterval * 1000.0) / 1000.0;
                if (wakeupInterval < 0.02) {
                    wakeupInterval = 0.02;
                }
            } else {
                wakeupInterval = wakeupIntervalLook;
            }
        }

        if (first_time == 2) {
            first_time++;
        } else {
            if (first_time == 3) {
                wakeupInterval = wakeupIntervalLook;
                wakeupIntervalLook = 0;
                first_time = 1;
            }
        }
    } else {
        wakeupInterval += mu * sysClockFactor * sysClock;
        wakeupInterval = round(wakeupInterval * 1000.0) / 1000.0;
        if (wakeupInterval < 0.02) {
            wakeupInterval = 0.02;
        }
    }
    //log_tsr << "nextInterval: " << wakeupInterval << endl;
}

/**
 * Handle TADMAC preambles and received data packets.
 */
void TADMacLayer::handleLowerMsg(cMessage *msg) {
    handleSelfMsg(msg);
}

void TADMacLayer::handleSelfMsg(cMessage *msg) {
    // simply pass the massage as self message, to be processed by the FSM.
    // Check role of this node
    if (role == NODE_SENDER) {
        handleSelfMsgSender(msg);
    } else {
        handleSelfMsgReceiver(msg);
    }
}

void TADMacLayer::sendDataPacket() {
    nbTxDataPackets++;
    macpkttad_ptr_t pkt = new MacPktTAD();
    pkt->setSrcAddr(myMacAddr);
    // Send data to the node which sent WB packet to current node
    pkt->setDestAddr(receiverAddress);
//    macpkt_ptr_t pkt = macQueue.front()->dup();
    lastDataPktDestAddr = pkt->getDestAddr();
    pkt->setKind(TADMAC_DATA);
    pkt->setByteLength(16);
    pkt->setIdle(int(timeWaitWB.dbl() * 1000));
    attachSignal(pkt);
    sendDown(pkt);
}

/**
 * Handle transmission over messages: either send another preambles or the data
 * packet itself.
 */
void TADMacLayer::handleLowerControl(cMessage *msg) {
    // Transmission of one packet is over
    if (msg->getKind() == MacToPhyInterface::TX_OVER) {
        if (macState == SEND_DATA) {
            scheduleAt(simTime(), sentData);
        }
        if (macState == SEND_WB) {
            scheduleAt(simTime(), sentWB);
        }
        if (macState == SEND_ACK) {
            scheduleAt(simTime(), sentACK);
        }
    }
    // Radio switching (to RX or TX) ir over, ignore switching to SLEEP.
    else if (msg->getKind() == MacToPhyInterface::RADIO_SWITCHING_OVER) {
//        // we just switched to TX after CCA, so simply send the first
//        // sendPremable self message
//        if ((macState == SEND_PREAMBLE)
//                && (phy->getRadioState() == MiximRadio::TX)) {
//            scheduleAt(simTime(), send_preamble);
//        }
//        if ((macState == SEND_ACK)
//                && (phy->getRadioState() == MiximRadio::TX)) {
//            scheduleAt(simTime(), send_ack);
//        }
//        // we were waiting for acks, but none came. we switched to TX and now
//        // need to resend data
//        if ((macState == SEND_DATA)
//                && (phy->getRadioState() == MiximRadio::TX)) {
//            scheduleAt(simTime(), resend_data);
//        }

    } else {
        debugEV << "control message with wrong kind -- deleting\n";
    }
    delete msg;
}

/**
 * Encapsulates the received network-layer packet into a MacPkt and set all
 * needed header fields.
 */
bool TADMacLayer::addToQueue(cMessage *msg) {
    if (macQueue.size() >= queueLength) {
        // queue is full, message has to be deleted
        debugEV << "New packet arrived, but queue is FULL, so new packet is"
                " deleted\n";
//        msg->setName("MAC ERROR");
//        msg->setKind(PACKET_DROPPED);
//        sendControlUp(msg);
//        droppedPacket.setReason(DroppedPacket::QUEUE);
//        emit(BaseLayer::catDroppedPacketSignal, &droppedPacket);
        nbDroppedDataPackets++;

        return false;
    }

    macpkt_ptr_t macPkt = new MacPkt(msg->getName());
    macPkt->setBitLength(headerLength);
    cObject * const cInfo = msg->removeControlInfo();
    //EV<<"CSMA received a message from upper layer, name is "
    //  << msg->getName() <<", CInfo removed, mac addr="
    //  << cInfo->getNextHopMac()<<endl;
    macPkt->setDestAddr(getUpperDestinationFromControlInfo(cInfo));
    delete cInfo;
    macPkt->setSrcAddr(myMacAddr);

    assert(static_cast<cPacket*>(msg));
    macPkt->encapsulate(static_cast<cPacket*>(msg));

    macQueue.push_back(macPkt);
    debugEV << "Max queue length: " << queueLength << ", packet put in queue"
            "\n  queue size: " << macQueue.size() << " macState: " << macState
                   << endl;
    return true;
}

void TADMacLayer::attachSignal(macpkt_ptr_t macPkt) {
    //calc signal duration
    simtime_t duration = macPkt->getBitLength() / bitrate;
    //create and initialize control info with new signal
    setDownControlInfo(macPkt,createSignal(simTime(), duration, txPower, bitrate));
}

/**
 * Change the color of the node for animation purposes.
 */

void TADMacLayer::changeDisplayColor(STAGE_COLOR color) {
    if (!animation)
        return;
    cDisplayString& dispStr = findHost()->getDisplayString();
    //b=40,40,rect,black,black,2"
    if (color == GREEN)
        dispStr.setTagArg("b", 3, "green");
    //dispStr.parse("b=40,40,rect,green,green,2");
    if (color == BLUE)
        dispStr.setTagArg("b", 3, "blue");
    //dispStr.parse("b=40,40,rect,blue,blue,2");
    if (color == RED)
        dispStr.setTagArg("b", 3, "red");
    //dispStr.parse("b=40,40,rect,red,red,2");
    if (color == BLACK)
        dispStr.setTagArg("b", 3, "black");
    //dispStr.parse("b=40,40,rect,black,black,2");
    if (color == YELLOW)
        dispStr.setTagArg("b", 3, "yellow");
    //dispStr.parse("b=40,40,rect,yellow,yellow,2");
}
