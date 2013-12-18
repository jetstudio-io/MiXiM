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

#ifndef TADMACLAYER_H_
#define TADMACLAYER_H_

#include <string>
#include <sstream>
#include <vector>
#include <list>

#include "MiXiMDefs.h"
#include "BaseMacLayer.h"
#include <DroppedPacket.h>

class MacPkt;

/**
 * @brief Implementation of B-MAC (called also Berkeley MAC, Low Power
 * Listening or LPL).
 *
 * The protocol works as follows: each node is allowed to sleep for
 * slotDuration. After waking up, it first checks the channel for ongoing
 * transmissions.
 * If a transmission is catched (a preamble is received), the node stays awake
 * for at most slotDuration and waits for the actual data packet.
 * If a node wants to send a packet, it first sends preambles for at least
 * slotDuration, thus waking up all nodes in its transmission radius and
 * then sends out the data packet. If a mac-level ack is required, then the
 * receiver sends the ack immediately after receiving the packet (no preambles)
 * and the sender waits for some time more before going back to sleep.
 *
 * B-MAC is designed for low traffic, low power communication in WSN and is one
 * of the most widely used protocols (e.g. it is part of TinyOS).
 * The finite state machine of the protocol is given in the below figure:
 *
 * \image html BMACFSM.png "B-MAC Layer - finite state machine"
 *
 * A paper describing this implementation can be found at:
 * http://www.omnet-workshop.org/2011/uploads/slides/OMNeT_WS2011_S5_C1_Foerster.pdf
 *
 * @class TADMacLayer
 * @ingroup macLayer
 * @author Anna Foerster
 *
 */
class MIXIM_API TADMacLayer: public BaseMacLayer {
private:
    /** @brief Copy constructor is not allowed.
     */
    TADMacLayer(const TADMacLayer&);
    /** @brief Assignment operator is not allowed.
     */
    TADMacLayer& operator=(const TADMacLayer&);

public:
    TADMacLayer() :
            BaseMacLayer(), macQueue(), nbTxDataPackets(0), nbTxWB(0), nbRxDataPackets(0),
                    nbRxWB(0), nbMissedAcks(0), nbRecvdAcks(0), nbDroppedDataPackets(0),
                    nbTxAcks(0), macState(INIT), startTADMAC(NULL), wakeup(NULL),
                    waitWBTimeout(NULL), receivedWB(NULL), ccaTimeout(NULL), sentData(NULL),
                    resendData(NULL), receivedACK(NULL), ccaWBTimeout(NULL),
                    sentWB(NULL), waitDATATimeout(NULL), receivedDATA(NULL),
                    ccaACKTimeout(NULL), sentACK(NULL),
                    lastDataPktSrcAddr(), lastDataPktDestAddr(),
                    txAttempts(0), droppedPacket(), nicId(-1), queueLength(0), animation(false),
                    bitrate(0), checkInterval(0), txPower(0),
                    useMacAcks(0), maxTxAttempts(0), stats(false),
                    TSR_length(16), sysClock(0.05), initialWakeupInterval(1), wakeupInterval(1), alpha(0.5)
    {}
    virtual ~TADMacLayer();

    /** @brief Initialization of the module and some variables*/
    virtual void initialize(int);

    /** @brief Delete all dynamically allocated objects of the module*/
    virtual void finish();

    /** @brief Handle messages from lower layer */
    virtual void handleLowerMsg(cMessage*);

    /** @brief Handle messages from upper layer */
    virtual void handleUpperMsg(cMessage*);

    /** @brief Handle self message */
    virtual void handleSelfMsg(cMessage*);

    /** @brief Handle self messages such as timers used by sender */
    virtual void handleSelfMsgSender(cMessage*);

    /** @brief Handle self messages such as timers used by receiver */
    virtual void handleSelfMsgReceiver(cMessage*);

    /** @brief Handle control messages from lower layer */
    virtual void handleLowerControl(cMessage *msg);

protected:
    typedef std::list<macpkt_ptr_t> MacQueue;

    /** @brief A queue to store packets from upper layer in case another
     packet is still waiting for transmission.*/
    MacQueue macQueue;

    /** @name Different tracked statistics.*/
    /*@{*/
    long nbTxDataPackets;
    long nbTxWB;
    long nbRxDataPackets;
    long nbRxWB;
    long nbMissedAcks;
    long nbRecvdAcks;
    long nbDroppedDataPackets;
    long nbTxAcks;
    /*@}*/

    // Note type
    enum ROLES {
        RECEIVER,
        SENDER,
        TRANSMITER
    };
    ROLES role;

    int TSR[16];
    int TSR_length;
    /** @brief store the moment wakeup, will be used to calculate the rest time */
    simtime_t start;
    double sysClock;
    double alpha;

    /** @brief MAC states
     *
     *  The MAC states help to keep track what the MAC is actually
     *  trying to do.
     *  INIT        -- node has just started and its status is unclear
     *  SLEEP       -- node sleeps, but accepts packets from the network layer
     *  CCA         -- Clear Channel Assessment - MAC checks
     *         whether medium is busy
     *  SEND_WB     -- receiver node sends wake up beacon to sender
     *  WAIT_DATA   -- node has received at least one preamble from another node
     *  				and wiats for the actual data packet
     *  SEND_DATA   -- node has sent enough preambles and sends the actual data
     *  				packet
     *  WAIT_TX_DATA_OVER -- node waits until the data packet sending is ready
     *  WAIT_ACK -- node has sent the data packet and waits for ack from the
     *  			   receiving node
     *  SEND_ACK -- node send an ACK back to the sender
     *  WAIT_ACK_TX -- node waits until the transmission of the ack packet is
     *  				  over
     */
    enum States {
        INIT,	        //0
        SLEEP,	        //1
        // The stages for sender
        WAIT_WB,        //2
        CCA,            //3
        SEND_DATA,      //4
        WAIT_ACK,		//5
        // The stages for receiver
        CCA_WB,         //6
        SEND_WB,        //7
        WAIT_DATA,      //8
        CCA_ACK,        //9
        SEND_ACK,       //10
    };
    /** @brief The current state of the protocol */
    States macState;

    /** @brief Types of messages (self messages and packets) the node can process **/
    enum TYPES {
        TADMAC_START,           //0
        TADMAC_WAKE_UP,         //1
        // The messages used by sender
        TADMAC_WB_TIMEOUT,      //2
        TADMAC_RECEIVED_WB,     //3
        TADMAC_CCA_TIMEOUT,     //4
        TADMAC_SENT_DATA,       //5
        TADMAC_RESEND_DATA,     //6
        TADMAC_RECEIVED_ACK,    //7
        // The message used by receiver
        TADMAC_CCA_WB_TIMEOUT,  //8
        TADMAC_SENT_WB,         //9
        TADMAC_DATA_TIMEOUT,    //10
        TADMAC_RECEIVED_DATA,   //11
        TADMAC_CCA_ACK_TIMEOUT, //12
        TADMAC_SENT_ACK,        //13
        // The message used to transmit between the node
        TADMAC_WB,              //14
        TADMAC_DATA,            //15
        TADMAC_ACK              //16
    };

    // The messages used as events
    cMessage *startTADMAC; // call to start protocol TADMAC
    cMessage *wakeup;
    // The messages events used for sender
    cMessage *waitWBTimeout;
    cMessage *receivedWB;
    cMessage *ccaTimeout;
    cMessage *sentData;
    cMessage *resendData;
    cMessage *receivedACK;
    // The messages events used for receiver
    cMessage *ccaWBTimeout;
    cMessage *sentWB;
    cMessage *waitDATATimeout;
    cMessage *receivedDATA;
    cMessage *ccaACKTimeout;
    cMessage *sentACK;

    /** @name Help variables for the acknowledgment process. */
    /*@{*/
    LAddress::L2Type lastDataPktSrcAddr;
    LAddress::L2Type lastDataPktDestAddr;
    int txAttempts;
    /*@}*/

    /** @brief Inspect reasons for dropped packets */
    DroppedPacket droppedPacket;

    /** @brief publish dropped packets nic wide */
    int nicId;

    /** @brief The maximum length of the queue */
    unsigned int queueLength;
    /** @brief Animate (colorize) the nodes.
     *
     * The color of the node reflects its basic status (not the exact state!)
     * BLACK - node is sleeping
     * GREEN - node is receiving
     * YELLOW - node is sending
     */
    bool animation;
    /** @brief The duration of waiting stage. */
    double waitDuration;
    /* @brief Initialized value for receiver wakeup interval */
    double initialWakeupInterval;
    double wakeupInterval;
    /** @brief The bitrate of transmission */
    double bitrate;
    /** @brief The duration of CCA */
    double checkInterval;
    /** @brief Transmission power of the node */
    double txPower;
    /** @brief Use MAC level acks or not */
    bool useMacAcks;
    /** @brief Maximum transmission attempts per data packet, when ACKs are
     * used */
    int maxTxAttempts;
    /** @brief Gather stats at the end of the simulation */
    bool stats;

    /** @brief Possible colors of the node for animation */
    enum STAGE_COLOR {
        GREEN = 1, BLUE = 2, RED = 3, BLACK = 4, YELLOW = 5
    };

    /** @brief Internal function to change the color of the node */
    void changeDisplayColor(STAGE_COLOR color);

    /** @brief Internal function to send the first packet in the queue */
    void sendDataPacket();

    /** @brief Internal function to send an ACK */
    void sendMacAck();

    /** @brief Internal function to send one WB */
    void sendWB();

    /** @brief Internal function to attach a signal to the packet */
    void attachSignal(macpkt_ptr_t macPkt);

    /** @brief Internal function to add a new packet from upper to the queue */
    bool addToQueue(cMessage * msg);

    /** @brief Calculate the next wakeup interval*/
    double getNextInterval(bool isReceivedData);
};

#endif /* TADMACLAYER_H_ */
