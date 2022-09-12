#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";
    _r_tbl.push_back({route_prefix, prefix_length, next_hop, interface_num});
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    if (dgram.header().ttl == 0)
        return;

    if (_r_tbl.size() == 0)
        return;

    auto selected_route = _r_tbl.end();
    for (auto it = _r_tbl.begin(); it != _r_tbl.end(); it++) {
        auto entry = *it;
        uint8_t prefix_len = std::get<1>(entry);
        uint32_t prefix = prefix_len == 0 ? 0 : std::get<0>(entry) >> (32-prefix_len);
        uint32_t dst_prefix = prefix_len == 0 ? 0 : dgram.header().dst >> (32-prefix_len);
        if (prefix == dst_prefix && (selected_route == _r_tbl.end() || prefix_len > std::get<1>(*selected_route)))
            selected_route = it;
    }

    if (selected_route == _r_tbl.end())
        return;

    dgram.header().ttl--;
    if (dgram.header().ttl == 0)
        return;

    Address next_hop = std::get<2>(*selected_route).has_value() ? std::get<2>(*selected_route).value() : Address::from_ipv4_numeric(dgram.header().dst);
    interface(std::get<3>(*selected_route)).send_datagram(dgram, next_hop);
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
