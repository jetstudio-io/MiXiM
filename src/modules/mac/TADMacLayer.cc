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
 * Version 2.0: WB is broadcast
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
#include <stdlib.h>
#include <time.h>

Define_Module(TADMacLayer)

/**
 * Initialize method of TADMacLayer. Init all parameters, schedule timers.
 */
void TADMacLayer::initialize(int stage) {
    BaseMacLayer::initialize(stage);

    if (stage == 0) {
        srand(time(NULL));
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
        usePriority = hasPar("usePriority") ? par("usePriority") : true;
        useWBMiss = hasPar("useWBMiss") ? par("useWBMiss") : true;
        numberSender = hasPar("numberSender") ? par("numberSender") : 1;
        startAt = hasPar("startAt") ? par("startAt") : 0.001;
        logFileName = par("logFileName").stringValue();


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
            log_tsr.open("results/tsr.csv");

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
            /**
             * Define route table here. Because we don't use high level so we need to fix the network topologie
             * node[0] is receiver, mac address is 00:00:00:00:00:00
             * node[1->4] is sender, mac address is from 00:00:00:00:00:01 to 00:00:00:00:00:04
             * node[5] is receiver, mac address is 00:00:00:00:00:05
             * node[6->9] is sender, mac address is from 00:00:00:00:00:06 to 00:00:00:00:00:09
             */
            routeTable = new LAddress::L2Type[numberSender+1];
            for (int i = 1; i <= numberSender; i++) {
                ostringstream converter;
                converter << "00:00:00:00:00:0" << (i + nodeIdx);
                routeTable[i].setAddress(converter.str().c_str());

                // Create file to log wakeup interval
                converter.str("");
                converter.clear();
                converter << logFileName << "_" << (i + nodeIdx) << ".csv";
                logFile.open(converter.str().c_str());
                logFile << "WU Interval for node:" << i + nodeIdx << endl;
                logFile.close();
//                cout << routeTable[i] << endl;
            }
            // allocate memory & initialize for nodeWakeupInterval & nextWakeupIntervalTime
            nodeWakeupInterval = new double[numberSender+1];
            nodeWakeupIntervalLock = new double[numberSender+1];
            nextWakeupTime = new simtime_t[numberSender+1];
            for (int i = 1; i <= numberSender; i++) {
                nodeWakeupInterval[i] = wakeupInterval;
                nodeWakeupIntervalLock[i] = 0.0;
                nextWakeupTime[i] = (rand() % 1000 + 1) / 1000.0;
//                nextWakeupTime[i] = (100 * i) / 1000.0;
                //cout << nextWakeupTime[i] << endl;
            }

            // allocate memory & initialize for nodeIdle
            nodeIdle = new double*[numberSender+1];
            nodeIndex = new int[numberSender+1];
            nodeNumberWakeup = new int[numberSender+1];
            nodeFirstTime = new int[numberSender+1];
            nodePriority = new int[numberSender+1];
            nodeCollision = new int[numberSender+1];
            nodeChoosen = new int[numberSender+1];
            nodeBroken = new int[numberSender+1];
            for (int i = 1; i <= numberSender; i++) {
                nodePriority[i] = 0;
                nodeIndex[i] = 0;
                nodeNumberWakeup[i] = 0;
                nodeFirstTime[i] = 1;
                nodeIdle[i] = new double[2];
                nodeIdle[i][0] = nodeIdle[i][1] = 0;
                nodeCollision[i] = 0;
                nodeChoosen[i] = 0;
                nodeBroken[i] = 0;
            }
        } else {
            logFile.open("results/sender.csv");
        }
        nbCollision = 0;
        numberWakeup = 0;
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

    // record stats
    if (stats) {
        recordScalar("nbTxDataPackets", nbTxDataPackets);
        recordScalar("nbTxPreambles", nbTxWB);
        recordScalar("nbRxDataPackets", nbRxDataPackets);
        recordScalar("nbRxPreambles", nbRxWB);
        recordScalar("nbMissedAcks", nbMissedAcks);
        recordScalar("nbRecvdAcks", nbRecvdAcks);
        recordScalar("nbTxAcks", nbTxAcks);
        recordScalar("numberWakeup", numberWakeup);
        recordScalar("nbCollision", nbCollision);
        if (role == NODE_RECEIVER) {
            for (int i = 1; i <= numberSender; i++) {
                ostringstream converter;
                converter << "nodeCollision_" << i;
                recordScalar(converter.str().c_str(), nodeCollision[i]);
                // Create file to log wakeup interval
                converter.str("");
                converter.clear();
                converter << "nodeChoosen_" << i;
                recordScalar(converter.str().c_str(), nodeChoosen[i]);

                converter.str("");
                converter.clear();
                converter << "nodeBroken_" << i;
                recordScalar(converter.str().c_str(), nodeBroken[i]);
            }
        }
    }
}

/**
 * We don't user upper layer - will be implement after
 */
void TADMacLayer::handleUpperMsg(cMessage *msg) {
    delete msg;
    msg = NULL;
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
 * /!\ NOTE: This function is used only in recevier
 * Find next wakeup time
 * Change physic layer state
 * Change node color
 * Change state to SLEEP
 */
void TADMacLayer::scheduleNextWakeup() {
    changeDisplayColor(BLACK);
    phy->setRadioState(MiximRadio::SLEEP);
    macState = SLEEP;
    // find the next wakeup time
    simtime_t nextWakeup = 10000.0;
    currentNode = 0;
    bool collision = false;
    bool *isCollision = new bool[numberSender+1];
    for (int i = 1; i <= numberSender; i++) {
        isCollision[i] = false;
        // Check if already passed the wakeup time for a node
        if (nextWakeupTime[i] < simTime()) {
//            cout << simTime() << "|" << nextWakeupTime[i] << "|" << i << "|" << nodeWakeupInterval[i] << endl;
            int tmp = ceil((simTime().dbl() - nextWakeupTime[i].dbl()) / nodeWakeupInterval[i]);
            nextWakeupTime[i] += tmp * nodeWakeupInterval[i];
            for (int j = 0; j < tmp; j++) {
                updateTSR(i, 0);
            }
//            cout << simTime() << "|" << nextWakeupTime[i] << "|" << i << "|" << nodeWakeupInterval[i] << endl;
        }
        if (usePriority) {
            if (nextWakeup > nextWakeupTime[i]) {
                // if 2 wakeup time is too close so we must choose only 1
                if (nextWakeup < nextWakeupTime[i] + waitCCA + waitDATA + sysClock) {
                    // the priority of node i is lower than current node so we can't choose node i
                    // need to increase priority, update nextwakuptime & TSR for node i
                    if (nodePriority[i] < nodePriority[currentNode]) {
                        nodePriority[i]++;
                        nextWakeupTime[i] += nodeWakeupInterval[i];
                        updateTSR(i, 0);
                    } else { // choose node i so need to update for current node
                        nodePriority[currentNode]++;
                        nextWakeupTime[currentNode] += nodeWakeupInterval[currentNode];
                        updateTSR(currentNode, 0);
                        nextWakeup = nextWakeupTime[i];
                        currentNode = i;
                    }
                    collision = true;
                    isCollision[i] = true;
                    isCollision[currentNode] = true;
                } else { // current node may can be wakeup later -> don't need to increase priority & update TSR
                    nextWakeup = nextWakeupTime[i];
                    currentNode = i;
                }
            } else if ((nextWakeupTime[i] < nextWakeup + waitCCA + waitDATA + sysClock) && (nodePriority[i] > nodePriority[currentNode])) {
                nodePriority[currentNode]++;
                nextWakeupTime[currentNode] += nodeWakeupInterval[currentNode];
                updateTSR(currentNode, 0);
                nextWakeup = nextWakeupTime[i];
                currentNode = i;
                collision = true;
                isCollision[i] = true;
                isCollision[currentNode] = true;
            }
        } else {
            if (nextWakeup > nextWakeupTime[i]) {
                nextWakeup = nextWakeupTime[i];
                currentNode = i;
            }
        }
    }
    // reset priority of current node
    if (collision) {
        nbCollision++;
        nodePriority[currentNode] = 0;
        nodeChoosen[currentNode]++;
        for (int i = 1; i <= numberSender; i++) {
            if (isCollision[i]) {
                nodeCollision[i]++;
                if (i != currentNode) {
                    nodeBroken[i]++;
                }
            }
        }
    }
    scheduleAt(nextWakeup, wakeup);
}

void TADMacLayer::writeLog() {
    int nodeIdx = getNode()->getIndex();
    ostringstream converter;
    converter << logFileName << "_" << (currentNode + nodeIdx) << ".csv";
    logFile.open(converter.str().c_str(), fstream::app);
    logFile << nodeNumberWakeup[currentNode] << ","
            << round(start.dbl() * 1000) << ","
            << round(nodeWakeupInterval[currentNode] * 1000) << endl;
    logFile.close();
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
                double tmp = (rand() % 1000 + 1) / 1000.0;
//                cout << getNode()->getIndex() << ":" << tmp << endl;
                scheduleAt(simTime() + tmp, wakeup);
//                scheduleAt(simTime() + startAt, wakeup);
                return;
            }
            break;
        // This node is sleeping & time to wakeup
        case SLEEP:
            if (msg->getKind() == TADMAC_WAKE_UP) {
                //cout << "sender wakeup" << endl;
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
                logFile << round(start.dbl() * 1000) << " ";
                numberWakeup++;
                return;
            }
            break;
        // The sender is in state WAIT_WB & receive a message
        case WAIT_WB:
            if (msg->getKind() == TADMAC_WB_TIMEOUT) {
                //cout << "sender no wb -> sleep" << endl;
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
                // increase number wake up missed
                wbMiss++;
                logFile << round(timeWaitWB.dbl() * 1000) << "," << wbMiss << endl;
                return;
            }
            // duration the WAIT_WB, received the WB message -> change to CCA state & schedule the timeout event
            if (msg->getKind() == TADMAC_WB) {
                macpkt_ptr_t            mac  = static_cast<macpkt_ptr_t>(msg);
                const LAddress::L2Type& dest = mac->getDestAddr();
                // Do nothing if receive WB for other node
                if (dest != myMacAddr) {
                    mac = NULL;
//                    delete msg;
//                    msg = NULL;
                    return;
                }
                //cout << "sender receipt wb" << endl;
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
                // reset ccaAttempts
                ccaAttempts = 0;
                mac = NULL;
//                delete msg;
//                msg = NULL;
                logFile << round(timeWaitWB.dbl() * 1000) << "," << wbMiss << endl;
                return;
            }
            // during this periode, this node receive data packet or ack from other node, do nothing, wait right WB
            if (msg->getKind() == TADMAC_DATA || msg->getKind() == TADMAC_ACK) {
//                delete msg;
//                msg = NULL;
                return;
            }
            break;

        case CCA:
            if (msg->getKind() == TADMAC_CCA_TIMEOUT) {
                //cout << "sender send data" << endl;
                changeDisplayColor(YELLOW);
                // set antenna to sending sign state
                phy->setRadioState(MiximRadio::TX);
                // send the data packet - this function will be called in handleLowerControl
//                sendDataPacket();
                // change mac state to send data
                macState = SEND_DATA;
                // in this case, we don't need to schedule the event to handle when data is sent
                // this event will be call when the physic layer finished
                return;
            }
            // Receive a packet from other node while check channel
            if (msg->getKind() == TADMAC_WB || msg->getKind() == TADMAC_DATA || msg->getKind() == TADMAC_ACK) {
//                delete msg;
//                msg = NULL;
                if (ccaAttempts < maxCCAattempts) {
                    //cout << "sender cca attempts" << endl;
                    ccaAttempts++;
                    cancelEvent(ccaTimeout);
                    scheduleAt(simTime() + waitCCA, ccaTimeout);
                    return;
                } else {
                    //cout << "sender channel busy" << endl;
                    cancelEvent(ccaTimeout);
                    // The channel is busy so return to sleep state
                    // change icon to black light -> note is inactive
                    changeDisplayColor(BLACK);
                    // Change antenna to sleep state
                    phy->setRadioState(MiximRadio::SLEEP);
                    macState = SLEEP;
                    // schedule the next wakeup event
                    if (start + wakeupInterval < simTime()) {
                        start += wakeupInterval * floor((simTime() - start) / wakeupInterval);
                    }
                    scheduleAt(start + wakeupInterval, wakeup);
                    return;
                }
            }
            break;

        case SEND_DATA:
            // Finish send data to receiver
            if (msg->getKind() == TADMAC_SENT_DATA) {
                //cout << "sender sent data -> wait ack" << endl;
                // change icon to green light -> note is wait for sign
                changeDisplayColor(GREEN);
                // set antenna to state receive sign
                phy->setRadioState(MiximRadio::RX);
                // MAC state is CCA
                macState = WAIT_ACK;
                // schedule the event wait WB timeout
                scheduleAt(simTime() + waitACK, resendData);
                // reset number WB missed - after send data
                wbMiss = 0;
                return;
            }
            break;

        case WAIT_ACK:
            if (msg->getKind() == TADMAC_RESEND_DATA) {
                // if the number resend data is not reach max time
                if (txAttempts < maxTxAttempts) {
                    //cout << "sender no ack -> resend" << endl;
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
                    //cout << "sender no ack -> sleep" << endl;
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
                //cout << "sender receipt ack -> sleep" << endl;
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
            if (msg->getKind() == TADMAC_DATA || msg->getKind() == TADMAC_WB) {
                //cout << "sender receipt other packet" << endl;
//                delete msg;
//                msg = NULL;
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
                scheduleNextWakeup();
                return;
            }
            break;
        // This node is sleeping & time to wakeup
        case SLEEP:
            if (msg->getKind() == TADMAC_WAKE_UP) {
                //cout << "receiver wakeup" << endl;
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
                numberWakeup++;
//                log_wakeupInterval << numberWakeup << "," << round(start.dbl() * 1000) << "," << round(wakeupInterval * 1000) << endl;

                nodeNumberWakeup[currentNode]++;
                writeLog();
                return;
            }
            break;
        //
        case CCA_WB:
            if (msg->getKind() == TADMAC_CCA_WB_TIMEOUT) {
                //cout << "receiver send wb" << endl;
                // change icon to black light -> note is inactive
                changeDisplayColor(YELLOW);
                // Change antenna to sleep state
                phy->setRadioState(MiximRadio::TX);
                // Send WB to sender - this action will be executed when radio change state successful
                macState = SEND_WB;
                // Don't need to schedule sent wb event here, this event will be call when physical layer finish
                return;
            } else {
                // If CCA attempts don't reach maximum time
                if (ccaAttempts < maxCCAattempts) {
                    //cout << "receiver cca wb attempts" << endl;
                    ccaAttempts++;
                    // Cancel current event
                    cancelEvent(ccaWBTimeout);
                    // reschedule check channel free to send WB
                    scheduleAt(simTime() + waitCCA, ccaWBTimeout);
                } else {
                    //cout << "receiver channel busy" << endl;
                    // Cancel current event
                    cancelEvent(ccaWBTimeout);
                    // schedule for next wakeup time
                    scheduleNextWakeup();
                }
                return;
            }
            break;

        case SEND_WB:
            if (msg->getKind() == TADMAC_SENT_WB) {
                //cout << "receiver wait data" << endl;
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
                //cout << "receiver no data" << endl;
                // Calculate next wakeup interval
                calculateNextInterval();
                // schedule for next wakeup time
                scheduleNextWakeup();

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
                    mac = NULL;
//                    delete msg;
//                    msg = NULL;
                    return;
                }
                cancelEvent(waitDATATimeout);
                // Calculate next wakeup interval
                //cout << "receipt data" << endl;
                calculateNextInterval(msg);
                //cout << "calculateNextInterval" << endl;

                mac = NULL;
//                delete msg;
//                msg = NULL;

                // if use ack
                if (useMacAcks) {
                    //cout << "receiver have data, will send ack" << endl;
                    macState = CCA_ACK;
                    lastDataPktSrcAddr = src;
                    phy->setRadioState(MiximRadio::RX);
                    changeDisplayColor(GREEN);
                    // reset cca attempt number
                    ccaAttempts = 0;
                    scheduleAt(simTime() + waitCCA, ccaACKTimeout);
                } else {
                    //cout << "receiver no ack" << endl;
                    // schedule for next wakeup time
                    scheduleNextWakeup();
                }
                return;
            }
            break;

        case CCA_ACK:
            if (msg->getKind() == TADMAC_CCA_ACK_TIMEOUT) {
                //cout << "receiver send ack" << endl;
                // Radio is free to send ACK
                debugEV << "State CCA_ACK, message TADMAC_CCA_ACK_TIMEOUT, new state SEND_ACK" << endl;
                changeDisplayColor(YELLOW);
                phy->setRadioState(MiximRadio::TX);
//                sendMacAck();
                macState = SEND_ACK;
                return;
            } else {
                // If CCA attempts don't reach maximum time
                if (ccaAttempts < maxCCAattempts) {
                    //cout << "receiver cca ack attempts" << endl;
                    ccaAttempts++;
                    // Cancel current event
                    cancelEvent(ccaACKTimeout);
                    // reschedule check channel free to send WB
                    scheduleAt(simTime() + waitCCA, ccaACKTimeout);
                    return;
                } else {
                    //cout << "receiver channel busy ack" << endl;
                    // Cancel current event
                    cancelEvent(ccaACKTimeout);
                    // schedule for next wakeup time
                    scheduleNextWakeup();
                    return;
                }
            }
            break;
        case SEND_ACK:
            if (msg->getKind() == TADMAC_SENT_ACK) {
                //cout << "receiver sent ack -> sleep" << endl;
                // schedule for next wakeup time
                scheduleNextWakeup();
                return;
            }
            break;
    }
    opp_error("Undefined event of type %d in state %d (Radio state %d)!",
            msg->getKind(), macState, phy->getRadioState());
}

void TADMacLayer::updateTSR(int nodeId, int value) {
    for (int i = 0; i < TSR_length - 1; i++) {
        TSR_bank[nodeId][i] = TSR_bank[nodeId][i + 1];
    }
    TSR_bank[nodeId][TSR_length - 1] = value;
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
//    cout << "------------" << endl;
    // Move the array TSR to left to store the new value in TSR[TSR_lenth - 1]
    updateTSR(currentNode, (msg == NULL) ? 0 : 1);

    // Calculate X1;
    for (int i = 0; i < TSR_length / 2; i++) {
        if (TSR_bank[currentNode][i] == 1) {
            n11++;
            if (i > 0 && TSR_bank[currentNode][i - 1] == 1) {
                nc11++;
            }
        } else {
            n01++;
            if (i > 0 && TSR_bank[currentNode][i - 1] == 0) {
                nc01++;
            }
        }
    }
    x1 = n01 * nc01 * 2 / TSR_length - n11 * nc11 * 2 / TSR_length;
    //log_tsr << "n01: " << n01 << " | nc01: " << nc01 << " | n11: " << n11 << " | nc11: " << nc11 << " | x1: " << x1 << endl;
    // Calculate X2
    for (int i = TSR_length / 2; i < TSR_length; i++) {
        if (TSR_bank[currentNode][i] == 1) {
            n12++;
            if (TSR_bank[currentNode][i - 1] == 1) {
                nc12++;
            }
        } else {
            n02++;
            if (TSR_bank[currentNode][i - 1] == 0) {
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
            int wbMiss = 0;
            if (msg != NULL) {
                macpkttad_ptr_t mac  = static_cast<macpkttad_ptr_t>(msg);
                idle = double(mac->getIdle()) / 1000.0;
                wbMiss = mac->getWbMiss();
                mac = NULL;
                nodeIdle[currentNode][nodeIndex[currentNode]] = idle;
                nodeIndex[currentNode]++;
            }
            if (nodeIdle[currentNode][0] != 0 && nodeIdle[currentNode][1] != 0) {
                double WUInt_diff = (nodeIdle[currentNode][0] - nodeIdle[currentNode][1]) / 2;
                if (WUInt_diff * 100 != 0) {
                    if (useWBMiss) {
                        nodeWakeupIntervalLock[currentNode] = (nodeWakeupInterval[currentNode] + WUInt_diff) / (wbMiss + 1);
                    } else {
                        nodeWakeupIntervalLock[currentNode] = nodeWakeupInterval[currentNode] + WUInt_diff;
                    }
//                    cout << simTime() << "|" << nodeWakeupInterval[currentNode] << "|" << currentNode << "|" << WUInt_diff<< "|" << wbMiss<<endl;
                    nodeWakeupInterval[currentNode] = (nodeWakeupIntervalLock[currentNode] - idle + sysClock * 2);
                    if (nodeWakeupInterval[currentNode] < 0) {
                        nodeWakeupInterval[currentNode] += nodeWakeupIntervalLock[currentNode];
                        updateTSR(currentNode, 0);
                    }
                    nodeFirstTime[currentNode]++;
                }
                nodeIdle[currentNode][0] = nodeIdle[currentNode][1] = 0;
                nodeIndex[currentNode] = 0;
            }
        } else {
            if (nodeIndex[currentNode] == 1) {
                nodeIndex[currentNode]--;
            }
            if (nodeWakeupIntervalLock[currentNode] * 100 == 0) {
                nodeWakeupInterval[currentNode] += mu * sysClockFactor * sysClock;
                nodeWakeupInterval[currentNode] = round(nodeWakeupInterval[currentNode] * 1000.0) / 1000.0;
                if (nodeWakeupInterval[currentNode] < 0.02) {
                    nodeWakeupInterval[currentNode] = 0.02;
                }
            } else {
                nodeWakeupInterval[currentNode] = nodeWakeupIntervalLock[currentNode];
            }
        }

        if (nodeFirstTime[currentNode] == 2) {
            nodeFirstTime[currentNode]++;
        } else {
            if (nodeFirstTime[currentNode] == 3) {
                nodeWakeupInterval[currentNode] = nodeWakeupIntervalLock[currentNode];
                nodeWakeupIntervalLock[currentNode] = 0;
                nodeFirstTime[currentNode] = 1;
            }
        }
    } else {
        nodeWakeupInterval[currentNode] += mu * sysClockFactor * sysClock;
        nodeWakeupInterval[currentNode] = round(nodeWakeupInterval[currentNode] * 1000.0) / 1000.0;
        if (nodeWakeupInterval[currentNode] < 0.02) {
            nodeWakeupInterval[currentNode] = 0.02;
        }
    }

    nextWakeupTime[currentNode] += nodeWakeupInterval[currentNode];
}

/**
 * Handle TADMAC preambles and received data packets.
 */
void TADMacLayer::handleLowerMsg(cMessage *msg) {
    handleSelfMsg(msg);
    delete msg;
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
    pkt->setWbMiss(wbMiss);
    attachSignal(pkt);
    sendDown(pkt);
    //cout << "sender sent packet to " << receiverAddress << endl;
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
        if ((macState == SEND_WB)
                && (phy->getRadioState() == MiximRadio::TX)) {
            // Call action to send WB packet
            sendWB();
        }
        if ((macState == SEND_ACK)
                && (phy->getRadioState() == MiximRadio::TX)) {
            sendMacAck();
        }
//        // we were waiting for acks, but none came. we switched to TX and now
//        // need to resend data
        if ((macState == SEND_DATA)
                && (phy->getRadioState() == MiximRadio::TX)) {
            sendDataPacket();
        }

    } else {
        debugEV << "control message with wrong kind -- deleting\n";
    }
    delete msg;
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
