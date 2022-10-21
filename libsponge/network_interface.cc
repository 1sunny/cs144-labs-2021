#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
        : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

template <typename T>
void NetworkInterface::send_datagram(EthernetAddress dst, const T &data, uint16_t type){
    EthernetFrame frame;
    EthernetHeader &header = frame.header();
    header.type = type;
    header.src = _ethernet_address;
    header.dst = dst;
    frame.payload() = data.serialize();
    _frames_out.push(frame);
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    if (!_address_map.count(next_hop_ip)){
        _dgram_queue[next_hop_ip].push(dgram);
        if (!_last_sent_arp.count(next_hop_ip) || (_tick - _last_sent_arp[next_hop_ip]) > 5000){
            ARPMessage arp;
            arp.opcode = ARPMessage::OPCODE_REQUEST;
            arp.sender_ethernet_address = _ethernet_address;
            arp.sender_ip_address = _ip_address.ipv4_numeric();
            // arp.target_ethernet_address = ETHERNET_BROADCAST;
            arp.target_ip_address = next_hop_ip;
            send_datagram(ETHERNET_BROADCAST, arp, EthernetHeader::TYPE_ARP);
            _last_sent_arp[next_hop_ip] = _tick;
        }
    }else{
        send_datagram(_address_map[next_hop_ip], dgram, EthernetHeader::TYPE_IPv4);
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    const EthernetHeader &header = frame.header();
    if (header.dst != ETHERNET_BROADCAST && header.dst != _ethernet_address){
        return {};
    }
    if (header.type == header.TYPE_IPv4){
        InternetDatagram dgram;
        ParseResult result = dgram.parse(frame.payload());
        if (result == ParseResult::NoError){
            return dgram;
        }
    }else if(header.type == header.TYPE_ARP) {
        ARPMessage rev_arp;
        ParseResult result = rev_arp.parse(frame.payload());
        if (result != ParseResult::NoError) {
            return {};
        }
        // Learn mappings from both requests and replies
        uint32_t sender_ip = rev_arp.sender_ip_address;
        _address_map[sender_ip] = rev_arp.sender_ethernet_address;
        _last_rev_arp[sender_ip] = _tick;
        // for me ?
        if (rev_arp.target_ip_address == _ip_address.ipv4_numeric()) {
            if (rev_arp.opcode == ARPMessage::OPCODE_REQUEST) {
                // send rev_arp reply
                ARPMessage reply_arp;
                reply_arp.opcode = ARPMessage::OPCODE_REPLY;
                reply_arp.sender_ethernet_address = _ethernet_address;
                reply_arp.sender_ip_address = _ip_address.ipv4_numeric();
                reply_arp.target_ethernet_address = rev_arp.sender_ethernet_address;
                reply_arp.target_ip_address = sender_ip;
                send_datagram(_address_map[sender_ip], reply_arp, EthernetHeader::TYPE_ARP);
            } else if (rev_arp.opcode == ARPMessage::OPCODE_REPLY) {
                std::queue<InternetDatagram> &q = _dgram_queue[sender_ip];
                while (!q.empty()) {
                    send_datagram(_address_map[sender_ip], q.front(), EthernetHeader::TYPE_IPv4);
                    q.pop();
                }
            }
        }
    }
    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    _tick += ms_since_last_tick;
    for (auto it = _address_map.begin(); it != _address_map.end();){
        auto nxt = it;
        nxt++;
        if (_tick - _last_rev_arp[it->first] > 30 * 1000){
            _address_map.erase(it);
        }
        it = nxt;
    }
}
