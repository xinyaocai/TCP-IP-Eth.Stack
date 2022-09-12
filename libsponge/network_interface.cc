#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`


using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address),
      _ip_address(ip_address),
      _arp_cache{},
      _ip_timer{},
      _cur_time{0},
      _wait_frames{} {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    EthernetFrame eth;
    eth.header().src = _ethernet_address;
    
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    auto ip2arp = _arp_cache.find(next_hop_ip);
    if (ip2arp != _arp_cache.end()) {
        eth.header().type = EthernetHeader::TYPE_IPv4;
        eth.header().dst = ip2arp->second.second;
        eth.payload() = dgram.serialize();
    } else {
        _wait_frames.insert({next_hop_ip, dgram});
        if (_ip_timer.count(next_hop_ip) && _cur_time - _ip_timer[next_hop_ip] <= 5000)
            return;
        eth.header().type = EthernetHeader::TYPE_ARP;
        eth.header().dst = ETHERNET_BROADCAST;
        ARPMessage msg;
        msg.opcode = ARPMessage::OPCODE_REQUEST;
        msg.sender_ethernet_address = _ethernet_address;
        msg.sender_ip_address = _ip_address.ipv4_numeric();
        msg.target_ip_address = next_hop.ipv4_numeric();
        eth.payload() = msg.serialize();
        _ip_timer[next_hop_ip] = _cur_time;
    }
    _frames_out.push(eth);
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    EthernetHeader header = frame.header();
    if (header.dst != _ethernet_address && header.dst != ETHERNET_BROADCAST)
        return {};
    
    if (header.type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram datagram;
        if (datagram.parse(frame.payload()) == ParseResult::NoError)
            return datagram;
    } else if (header.type == EthernetHeader::TYPE_ARP) {
        ARPMessage msg;
        if (msg.parse(frame.payload()) == ParseResult::NoError) {
            _arp_cache[msg.sender_ip_address] = {_cur_time, msg.sender_ethernet_address};
            if (msg.target_ip_address != _ip_address.ipv4_numeric())
                return {};
            if (msg.opcode == ARPMessage::OPCODE_REQUEST) {
                EthernetFrame reply_frame;
                reply_frame.header().src = _ethernet_address;
                reply_frame.header().dst = frame.header().src;
                reply_frame.header().type = EthernetHeader::TYPE_ARP;
                ARPMessage reply_msg;
                reply_msg.opcode = ARPMessage::OPCODE_REPLY;
                reply_msg.sender_ethernet_address = _ethernet_address;
                reply_msg.sender_ip_address = _ip_address.ipv4_numeric();
                reply_msg.target_ethernet_address = msg.sender_ethernet_address;
                reply_msg.target_ip_address = msg.sender_ip_address;
                reply_frame.payload() = reply_msg.serialize();
                _frames_out.push(reply_frame);
            } else if (msg.opcode == ARPMessage::OPCODE_REPLY) {
                uint32_t dst_ip = msg.sender_ip_address;
                auto range = _wait_frames.equal_range(dst_ip);
                Address addr = Address::from_ipv4_numeric(dst_ip);
                for (auto i = range.first; i != range.second; i++) {
                    send_datagram(i->second, addr);
                }
                _wait_frames.erase(range.first, range.second);
            }
        }
    }
    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    _cur_time += ms_since_last_tick;
    auto i = _arp_cache.begin();
    while (i != _arp_cache.end()) {
        if (i->second.first + 30*1000 < _cur_time) {
            i = _arp_cache.erase(i);
        } else {
            i++;
        }
    }
}
