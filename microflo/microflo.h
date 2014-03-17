/* MicroFlo - Flow-Based Programming for microcontrollers
 * Copyright (c) 2013 Jon Nordby <jononor@gmail.com>
 * MicroFlo may be freely distributed under the MIT license
 */

#ifndef MICROFLO_H
#define MICROFLO_H

#include <stdint.h>

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include "components.h"
#include "commandformat.h"

const int MICROFLO_MAX_PORTS = 255;

#ifdef MICROFLO_NODE_LIMIT
const int MICROFLO_MAX_NODES = MICROFLO_NODE_LIMIT;
#else
const int MICROFLO_MAX_NODES = 50;
#endif



#ifdef MICROFLO_MESSAGE_LIMIT
const int MICROFLO_MAX_MESSAGES = MICROFLO_MESSAGE_LIMIT;
#else
const int MICROFLO_MAX_MESSAGES = 50;
#endif

#define MICROFLO_DEBUG(handler, level, code) \
do { \
    if (handler) { \
        handler->emitDebug(level, code); \
    } \
} while(0)

namespace MicroFlo {
    typedef uint8_t NodeId;
    typedef int8_t PortId;
#ifdef STELLARIS
    typedef long PinId;
#else
    typedef int PinId;
#endif
}

namespace Components {
    class SubGraph;
    class DummyComponent;
}

// Packet
// TODO: implement a proper variant type, or type erasure
// XXX: should setup & ticks really be IPs??
class Packet {

public:
    Packet(): msg(MsgVoid) {}
    Packet(bool b): msg(MsgBoolean) { data.boolean = b; }
    Packet(char c): msg(MsgAscii) { data.ch = c; }
    Packet(unsigned char by): msg(MsgByte) { data.byte = by; }
    Packet(long l): msg(MsgInteger) { data.lng = l; }
    Packet(float f): msg(MsgFloat) { data.flt = f; }
    Packet(Msg m): msg(m) {}

    Msg type() const { return msg; }
    bool isValid() const { return msg > MsgInvalid && msg < MsgMaxDefined; }

    bool isSetup() const { return msg == MsgSetup; }
    bool isTick() const { return msg == MsgTick; }
    bool isSpecial() const { return isSetup() || isTick(); }

    bool isVoid() const { return msg == MsgVoid; }
    bool isStartBracket() const { return msg == MsgBracketStart; }
    bool isEndBracket() const { return msg == MsgBracketEnd; }

    bool isData() const { return isValid() && !isSpecial(); }
    bool isBool() const { return msg == MsgBoolean; }
    bool isByte() const { return msg == MsgByte; }
    bool isAscii() const { return msg == MsgAscii; }
    bool isInteger() const { return msg == MsgInteger; } // TODO: make into a long or long long
    bool isFloat() const { return msg == MsgFloat; }
    bool isNumber() const { return isInteger() || isFloat(); }

    bool asBool() const ;
    float asFloat() const ;
    long asInteger() const ;
    char asAscii() const ;
    unsigned char asByte() const ;

    bool operator==(const Packet& rhs) const;

private:
    union PacketData {
        bool boolean;
        char ch;
        unsigned char byte;
        long lng;
        float flt;
    } data;
    enum Msg msg;
};

// Network

class Component;

struct Message {
    Component *target;
    MicroFlo::PortId targetPort;
    Packet pkg;
};

class NetworkNotificationHandler;
class IO;

class Network {
#ifdef HOST_BUILD
    friend class JavaScriptNetwork;
#endif

public:
    static const MicroFlo::NodeId firstNodeId = 1; // 0=reserved: no-parent-node
    enum State {
        Invalid = -1,
        Stopped,
        Running
    };
public:
    Network(IO *io);

    void reset();
    void start();

    MicroFlo::NodeId addNode(Component *node, MicroFlo::NodeId parentId);
    void connect(Component *src, MicroFlo::PortId srcPort,
                 Component *target, MicroFlo::PortId targetPort);
    void connect(MicroFlo::NodeId srcId, MicroFlo::PortId srcPort,
                 MicroFlo::NodeId targetId, MicroFlo::PortId targetPort);
    void connectSubgraph(bool isOutput,
                         MicroFlo::NodeId subgraphNode, MicroFlo::PortId subgraphPort,
                         MicroFlo::NodeId childNode, MicroFlo::PortId childPort);

    void sendMessage(Component *target, MicroFlo::PortId targetPort, const Packet &pkg,
                     Component *sender=0, MicroFlo::PortId senderPort=-1);
    void sendMessage(MicroFlo::NodeId targetId, MicroFlo::PortId targetPort, const Packet &pkg);

    void subscribeToPort(MicroFlo::NodeId nodeId, MicroFlo::PortId portId, bool enable);

    void setNotificationHandler(NetworkNotificationHandler *handler);

    void runTick();

    void emitDebug(DebugLevel level, DebugId id);
    void setDebugLevel(DebugLevel level);

private:
    void runSetup();
    void deliverMessages(int firstIndex, int lastIndex);
    void processMessages();

private:
    Component *nodes[MICROFLO_MAX_NODES];
    MicroFlo::NodeId lastAddedNodeIndex;
    Message messages[MICROFLO_MAX_MESSAGES];
    int messageWriteIndex;
    int messageReadIndex;
    NetworkNotificationHandler *notificationHandler;
    IO *io;
    State state;
    DebugLevel debugLevel;
};

class DebugHandler {
public:
    virtual void emitDebug(DebugLevel level, DebugId id) = 0;
    virtual void debugChanged(DebugLevel level) = 0;
};

class NetworkNotificationHandler : public DebugHandler {
public:
    virtual void packetSent(int index, Message m, Component *sender, MicroFlo::PortId senderPort) = 0;
    virtual void packetDelivered(int index, Message m) = 0;

    virtual void nodeAdded(Component *c, MicroFlo::NodeId parentId) = 0;
    virtual void nodesConnected(Component *src, MicroFlo::PortId srcPort,
                                Component *target, MicroFlo::PortId targetPort) = 0;
    virtual void networkStateChanged(Network::State s) = 0;
    virtual void subgraphConnected(bool isOutput,
                                   MicroFlo::NodeId subgraphNode, MicroFlo::PortId subgraphPort,
                                   MicroFlo::NodeId childNode, MicroFlo::PortId childPort) = 0;

    virtual void portSubscriptionChanged(MicroFlo::NodeId nodeId, MicroFlo::PortId portId, bool enable) = 0;
};

struct Connection {
    Component *target;
    MicroFlo::PortId targetPort;
    bool subscribed;
};


// IO interface for components
// Used to move the sideeffects of I/O components out of the component,
// to allow different target implementations, and to let tests inject mocks

typedef void (*IOInterruptFunction)(void *user);

class IO {
    friend class Network; // for setting up debug
protected:
    DebugHandler *debug;
public:
    virtual ~IO() {}

    // Serial
    virtual void SerialBegin(int serialDevice, int baudrate) = 0;
    virtual long SerialDataAvailable(int serialDevice) = 0;
    virtual unsigned char SerialRead(int serialDevice) = 0;
    virtual void SerialWrite(int serialDevice, unsigned char b) = 0;

    // Pin config
    enum PinMode {
        InputPin,
        OutputPin
    };
    enum PullupMode {
        PullNone,
        PullUp
    };
    virtual void PinSetMode(MicroFlo::PinId pin, PinMode mode) = 0;
    virtual void PinSetPullup(MicroFlo::PinId pin, PullupMode mode) = 0;
	virtual void SPISetMode() = 0;
    // Digital
    virtual void DigitalWrite(MicroFlo::PinId pin, bool val) = 0;
    virtual bool DigitalRead(MicroFlo::PinId pin) = 0;

    // Analog
    // Values should be [0..1023], for now
    virtual long AnalogRead(MicroFlo::PinId pin) = 0;

    // Pwm
    // [0..100]
    virtual void PwmWrite(MicroFlo::PinId pin, long dutyPercent) = 0;

    // Timer
    virtual long TimerCurrentMs() = 0;
    virtual long TimerCurrentMicros() { return TimerCurrentMs()*1000; }

    // Interrupts
    struct Interrupt {
        enum Mode {
            OnLow,
            OnHigh,
            OnChange,
            OnRisingEdge,
            OnFallingEdge
        };
    };

    // XXX: user responsible for mapping pin number to interrupt number
    virtual void AttachExternalInterrupt(int interrupt, IO::Interrupt::Mode mode,
                                         IOInterruptFunction func, void *user) = 0;
};

// Component
// TODO: add a way of doing subgraphs as components, both programatically and using .fbp format
// IDEA: a decentral way of declaring component introspection data. JSON embedded in comment?
class Component {
    friend class Network;
    friend class Components::DummyComponent;
    friend class Components::SubGraph;
public:
    static Component *create(ComponentId id);

    Component(Connection *outPorts, int ports) : connections(outPorts), nPorts(ports) {}
    virtual ~Component() {}
    virtual void process(Packet in, MicroFlo::PortId port) = 0;

    MicroFlo::NodeId id() const { return nodeId; }
    int component() const { return componentId; }

protected:
    void send(Packet out, MicroFlo::PortId port=0);
    IO *io;
private:
    void setParent(int parentId) { parentNodeId = parentId; }
    void connect(MicroFlo::PortId outPort, Component *target, MicroFlo::PortId targetPort);
    void setNetwork(Network *net, int n, IO *io);
private:
    Connection *connections; // one per output port
    int nPorts;

    Network *network;
    MicroFlo::NodeId nodeId; // identifier in the network
    int componentId; // what type of component this is
    MicroFlo::NodeId parentNodeId; // if <0, a top-level component, else subcomponent
};

#define MICROFLO_SUBGRAPH_MAXPORTS 10

namespace Components {

class SubGraph : public Component {
    friend class ::Network;
public:
    SubGraph();
    virtual ~SubGraph() {}

    // Implements Component
    virtual void process(Packet in, MicroFlo::PortId port);

    void connectInport(MicroFlo::PortId inPort, Component *child, MicroFlo::PortId childInPort);
    void connectOutport(MicroFlo::PortId outPort, Component *child, MicroFlo::PortId childOutPort);
private:
    Connection inputConnections[MICROFLO_SUBGRAPH_MAXPORTS];
    Connection outputConnections[MICROFLO_SUBGRAPH_MAXPORTS];
};

}


// Graph format
#include <stddef.h>

const size_t MICROFLO_CMD_SIZE = 1 + 7; // cmd + payload

class HostTransport;

class HostCommunication : public NetworkNotificationHandler {
public:
    HostCommunication();
    void setup(Network *net, HostTransport *t);

    void parseByte(char b);

    // Implements NetworkNotificationHandler
    virtual void packetSent(int index, Message m, Component *sender, MicroFlo::PortId senderPort);
    virtual void packetDelivered(int index, Message m);
    virtual void nodeAdded(Component *c, MicroFlo::NodeId parentId);
    virtual void nodesConnected(Component *src, MicroFlo::PortId srcPort,
                                Component *target, MicroFlo::PortId targetPort);
    virtual void networkStateChanged(Network::State s);
    virtual void emitDebug(DebugLevel level, DebugId id);
    virtual void debugChanged(DebugLevel level);
    virtual void portSubscriptionChanged(MicroFlo::NodeId nodeId, MicroFlo::PortId portId, bool enable);
    virtual void subgraphConnected(bool isOutput, MicroFlo::NodeId subgraphNode,
                                   MicroFlo::PortId subgraphPort, MicroFlo::NodeId childNode, MicroFlo::PortId childPort);

private:
    void parseCmd();
private:
    enum State {
        Invalid = -1,
        ParseHeader,
        ParseCmd,
        LookForHeader
    };

    Network *network;
    HostTransport *transport;
    uint8_t currentByte;
    unsigned char buffer[MICROFLO_CMD_SIZE];
    enum State state;
};


class HostTransport {
public:
    virtual void setup(IO *i, HostCommunication *c) = 0;
    virtual void runTick() = 0;

    virtual void sendCommandByte(uint8_t b) = 0;
    void padCommandWithNArguments(int arguments);
};

class NullHostTransport : public HostTransport {
public:
    // implements HostTransport
    virtual void setup(IO *i, HostCommunication *c) { ; }
    virtual void runTick() { ; }
    virtual void sendCommandByte(uint8_t b) { ; }
private:
};

class SerialHostTransport : public HostTransport {
public:
    SerialHostTransport(uint8_t serialPort, int baudRate);

    // implements HostTransport
    virtual void setup(IO *i, HostCommunication *c);
    virtual void runTick();
    virtual void sendCommandByte(uint8_t b);

private:
    IO *io;
    HostCommunication *controller;
    int8_t serialPort;
    int serialBaudrate;
};

#endif // MICROFLO_H
