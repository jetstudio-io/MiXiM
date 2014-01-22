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

#include "TADMacLayer.h"

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

        wakeupInterval = hasPar("WUInterval") ? par("WUInterval") : 1;
        waitCCA = hasPar("waitCCA") ? par("waitCCA") : 0.1;
        waitWB = hasPar("waitWB") ? par("waitWB") : 0.3;
        waitACK = hasPar("waitACK") ? par("waitACK") : 0.3;
        waitDATA = hasPar("waitDATA") ? par("waitDATA") : 0.3;
        sysClock = hasPar("sysClock") ? par("sysClock") : 0.001;
        alpha = hasPar("alpha") ? par("alpha") : 0.5;

        queueLength = hasPar("queueLength") ? par("queueLength") : 10;
        animation = hasPar("animation") ? par("animation") : true;
        bitrate = hasPar("bitrate") ? par("bitrate") : 15360.;
        headerLength = hasPar("headerLength") ? par("headerLength") : 10.;
        txPower = hasPar("txPower") ? par("txPower") : 50.;
        useMacAcks = hasPar("useMACAcks") ? par("useMACAcks") : false;
        maxTxAttempts = hasPar("maxTxAttempts") ? par("maxTxAttempts") : 2;
        cout << this->getNode()->getIndex() << "-headerLength: " << headerLength << ", bitrate: " << bitrate
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
            log_wakeupInterval.open("results/wakeupinterval.csv");
        } else {
            log_wb.open("results/wb.csv");
        }
        cout << wakeupInterval << endl;
        wakeupInterval /= 1000;
        cout << wakeupInterval << endl;
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
        //recordScalar("timeSleep", timeSleep);
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
    bool pktAdded = addToQueue(msg);
    if (!pktAdded)
        return;
    // force wakeup now - in sender node, we wake up on demand to transmit data
    if (wakeup->isScheduled() && (macState == SLEEP)) {
        cancelEvent(wakeup);
        scheduleAt(simTime() + dblrand() * 0.1f, wakeup);
    }
}

/**
 * Send wakeup beacon from receiver to sender
 */
void TADMacLayer::sendWB() {
    macpkt_ptr_t wb = new MacPkt();
    wb->setSrcAddr(myMacAddr);
    wb->setDestAddr(LAddress::L2BROADCAST);
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
                cout << this->getNode()->getIndex() << "-State INIT, message TADMAC_START, new state SLEEP" << endl;
                changeDisplayColor(BLACK);
                phy->setRadioState(MiximRadio::SLEEP);
                macState = SLEEP;
                scheduleAt(simTime() + dblrand(), wakeup);
                return;
            }
            break;
        // This node is sleeping & time to wakeup
        case SLEEP:
            if (msg->getKind() == TADMAC_WAKE_UP) {
                cout << this->getNode()->getIndex() << "-State SLEEP, message TADMAC_WAKEUP, new state WAIT_WB, next event call at: " << simTime() + waitWB << endl;
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
                // store the moment to wait WB
                startWaitWB = simTime();
                // reset number resend data
                txAttempts = 0;
                return;
            }
            break;
        // The sender is in state WAIT_WB & receive a message
        case WAIT_WB:
            if (msg->getKind() == TADMAC_WB_TIMEOUT) {
                // we didn't received the WB from receiver -> change back to sleep state & will wake up after wakeupInterval seconds
                cout << this->getNode()->getIndex() << "-State WAIT_WB, message TADMAC_WB_TIMEOUT, new state SLEEP, next event call at: " << start + wakeupInterval << endl;
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
                // log the time wait for WB
                log_wb << simTime() << "," << simTime() - startWaitWB << endl;
                return;
            }
            // duration the WAIT_WB, received the WB message -> change to CCA state & schedule the timeout event
            if (msg->getKind() == TADMAC_WB) {
                nbRxWB++;
                cout << this->getNode()->getIndex() << "-State WAIT_WB, message TADMAC_WB received, new state CCA, next event call at: " << simTime() + waitCCA << endl;
                macState = CCA;
                // Don't need to call the event to handle WB timeout
                cancelEvent (waitWBTimeout);
                // schedule the CCA timeout event
                scheduleAt(simTime() + waitCCA, ccaTimeout);
                // log the time wait for WB
                log_wb << simTime() << "," << simTime() - startWaitWB << endl;
                return;
            }
            break;

        case CCA:
            if (msg->getKind() == TADMAC_CCA_TIMEOUT) {
                // The radio is clean so we can send the data.
                cout << this->getNode()->getIndex() << "-State CCA, message TADMAC_CCA_TIMEOUT, new state SEND_DATA" << endl;
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
            break;

        case SEND_DATA:
            // Finish send data to receiver
            if (msg->getKind() == TADMAC_SENT_DATA) {
                cout << this->getNode()->getIndex() << "-State SEND_DATA, message TADMAC_SENT_DATA, new state WAIT_ACK" << endl;
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
                    cout << this->getNode()->getIndex() << "-State WAIT_ACK, message TADMAC_RESEND_DATA, new state WAIT_WB, next event at: " << simTime() + waitWB << endl;
                    changeDisplayColor(GREEN);
                    phy->setRadioState(MiximRadio::RX);
                    txAttempts++;
                    macState = WAIT_WB;
                    scheduleAt(simTime() + waitWB, waitWBTimeout);
                    // store the moment to wait WB
                    startWaitWB = simTime();
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
                    cout << this->getNode()->getIndex() << "-State WAIT_ACK, message TADMAC_RESEND_DATA, new state SLEEP (resend too much), next event at: " << start + wakeupInterval << endl;
                    scheduleAt(start + wakeupInterval, wakeup);
                    nbMissedAcks++;
                }
                return;
            }
            // received ACK -> change to sleep, schedule next wakeup time
            if (msg->getKind() == TADMAC_RECEIVED_ACK || msg->getKind() == TADMAC_ACK) {
                cout << this->getNode()->getIndex() << "-State WAIT_ACK, message TADMAC_RECEIVED_ACK, new state SLEEP" << endl;
                changeDisplayColor(BLACK);
                phy->setRadioState(MiximRadio::SLEEP);
                macState = SLEEP;
                // schedule the next wakeup event
                if (start + wakeupInterval < simTime()) {
                    start += wakeupInterval * floor((simTime() - start) / wakeupInterval);
                }
                cout << this->getNode()->getIndex() << "-State WAIT_ACK, message TADMAC_RESEND_DATA, new state SLEEP (resend too much), next event at: " << start + wakeupInterval << endl;
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
                cout << this->getNode()->getIndex() << "-State INIT, message TADMAC_START, new state SLEEP" << endl;
                changeDisplayColor(BLACK);
                phy->setRadioState(MiximRadio::SLEEP);
                macState = SLEEP;
                scheduleAt(simTime() + dblrand(), wakeup);
                return;
            }
            break;
        // This node is sleeping & time to wakeup
        case SLEEP:
            if (msg->getKind() == TADMAC_WAKE_UP) {
                cout << this->getNode()->getIndex() << "-State SLEEP, message TADMAC_WAKEUP, new state CCA, next event at:" << simTime() + waitCCA << endl;
                // change icon to green light -> note is wait for sign
                changeDisplayColor(GREEN);
                // set antenna to receiving sign state
                phy->setRadioState(MiximRadio::RX);
                // MAC state is CCA
                macState = CCA_WB;
                // schedule the event wait WB timeout
                start = simTime();
                scheduleAt(start + waitCCA, ccaWBTimeout);
                log_wakeupInterval << start << "," << wakeupInterval << endl;
                return;
            }
            break;
        //
        case CCA_WB:
            if (msg->getKind() == TADMAC_CCA_WB_TIMEOUT) {
                // the radio is free to send WB
                cout << this->getNode()->getIndex() << "-State CCA_WB, message TADMAC_CCA_WB_TIMEOUT, new state SEND_WB" << endl;
                // change icon to black light -> note is inactive
                changeDisplayColor(YELLOW);
                // Change antenna to sleep state
                phy->setRadioState(MiximRadio::TX);
                // Send WB to sender
                sendWB();
                macState = SEND_WB;
                // Don't need to schedule sent wb event here, this event will be call when physical layer finish
                return;
            }
            break;

        case SEND_WB:
            if (msg->getKind() == TADMAC_SENT_WB) {
                // The radio is clean so we can send the data.
                cout << this->getNode()->getIndex() << "-State SEND_WB, message TADMAC_SENT_WB, new state WAIT_DATA, next event at: " << simTime() + waitDATA << endl;
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
                wakeupInterval = getNextInterval(false);
                if (simTime() > start + wakeupInterval) {
                    start += wakeupInterval;
                }
                cout << this->getNode()->getIndex() << "-State WAIT_DATA, message TADMAC_DATA_TIMEOUT, new state SLEEP, next event at: " << start + wakeupInterval << endl;
                // schedule the event wait WB timeout
                scheduleAt(start + wakeupInterval, wakeup);
                return;
            }
            // Receive data -> wait CCA to send ACK
            if (msg->getKind() == TADMAC_DATA) {
                nbRxDataPackets++;
                macpkt_ptr_t            mac  = static_cast<macpkt_ptr_t>(msg);
                const LAddress::L2Type& dest = mac->getDestAddr();
                const LAddress::L2Type& src  = mac->getSrcAddr();
                if ((dest == myMacAddr) || LAddress::isL2Broadcast(dest)) {
                    sendUp(decapsMsg(mac));
                } else {
                    delete msg;
                    msg = NULL;
                    mac = NULL;
                }

                cancelEvent(waitDATATimeout);
                if ((useMacAcks) && (dest == myMacAddr))
                {
                    cout << this->getNode()->getIndex() << "-State WAIT_DATA, message TADMAC_DATA, new state"
                              " CCA_ACK" << endl;
                    macState = CCA_ACK;
                    lastDataPktSrcAddr = src;
                    phy->setRadioState(MiximRadio::RX);
                    changeDisplayColor(GREEN);
                    scheduleAt(simTime() + waitCCA, ccaACKTimeout);
                }
                return;
            }
            break;

        case CCA_ACK:
            if (msg->getKind() == TADMAC_CCA_ACK_TIMEOUT) {
                // Radio is free to send ACK
                cout << this->getNode()->getIndex() << "-State CCA_ACK, message TADMAC_CCA_ACK_TIMEOUT, new state SEND_ACK" << endl;
                changeDisplayColor(YELLOW);
                phy->setRadioState(MiximRadio::TX);
                sendMacAck();
                macState = SEND_ACK;
                return;
            }
            break;
        case SEND_ACK:
            if (msg->getKind() == TADMAC_SENT_ACK) {
                cout << this->getNode()->getIndex() << "-State SEND_ACK, message TADMAC_SENT_ACK, new state SLEEP" << endl;
                // change icon to green light -> note is wait for sign
                changeDisplayColor(BLACK);
                // set antenna to state receive sign
                phy->setRadioState(MiximRadio::SLEEP);
                // MAC state is CCA
                macState = SLEEP;
                // Calculate next wakeup interval
                wakeupInterval = getNextInterval(true);

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
double TADMacLayer::getNextInterval(bool isReceivedData) {
    int n01, n11, nc01, nc11;
    n01 = n11 = nc01 = nc11 = 0;
    int n02, n12, nc02, nc12;
    n02 = n12 = nc02 = nc12 = 0;
    double e = 0;
    double x1, x2;
    x1 = x2 = 0;
    // Move the array TSR to left to store the new value in TSR[TSR_lenth - 1]
    for (int i = 0; i < TSR_length - 1; i++) {
        TSR[i] = TSR[i + 1];
    }
    TSR[TSR_length - 1] = (isReceivedData) ? 1 : 0;
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

    // calculate the traffic weighting
    double mu = alpha * x1 + (1 - alpha) * x2;
    // calculate the correlator
    e = (n01 + n02) - (n11 + n12);
    double nextInterval = wakeupInterval + (mu + e) * sysClock;

    return nextInterval;
}

/**
 * Handle BMAC preambles and received data packets.
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
    macpkt_ptr_t pkt = macQueue.front()->dup();
    lastDataPktDestAddr = pkt->getDestAddr();
    pkt->setKind(TADMAC_DATA);

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
        cout << this->getNode()->getIndex() << "-control message with wrong kind -- deleting\n";
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
        cout << this->getNode()->getIndex() << "-New packet arrived, but queue is FULL, so new packet is"
                " deleted\n";
        msg->setName("MAC ERROR");
        msg->setKind(PACKET_DROPPED);
        sendControlUp(msg);
        droppedPacket.setReason(DroppedPacket::QUEUE);
        emit(BaseLayer::catDroppedPacketSignal, &droppedPacket);
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
    cout << this->getNode()->getIndex() << "-Max queue length: " << queueLength << ", packet put in queue"
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

/*void TADMacLayer::changeMacState(States newState)
 {
 switch (macState)
 {
 case RX:
 timeRX += (simTime() - lastTime);
 break;
 case TX:
 timeTX += (simTime() - lastTime);
 break;
 case SLEEP:
 timeSleep += (simTime() - lastTime);
 break;
 case CCA:
 timeRX += (simTime() - lastTime);
 }
 lastTime = simTime();

 switch (newState)
 {
 case CCA:
 changeDisplayColor(GREEN);
 break;
 case TX:
 changeDisplayColor(BLUE);
 break;
 case SLEEP:
 changeDisplayColor(BLACK);
 break;
 case RX:
 changeDisplayColor(YELLOW);
 break;
 }

 macState = newState;
 }*/
