#ifndef INCLUDE_DHT_HPP
#define INCLUDE_DHT_HPP

#include <array>
#include <random>
#include <chrono>
#include <vector>
#include "asio.hpp"

using asio::awaitable;
using asio::ip::udp;

struct NodeId{
    std::array<uint8_t, 20> id;

    static NodeId random() {
        NodeId n;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        for (auto& byte : n.id) {
            byte = dis(gen);
        }
        return n;
    }
    
    static NodeId from_bytes(const uint8_t* data) {
        NodeId n;
        std::memcpy(n.id.data(), data, 20);
        return n;
    }
    
    std::string to_string() const {
        return std::string(reinterpret_cast<const char*>(id.data()), 20);
    }

    NodeId distance(const NodeId& other) const {
        NodeId result;
        for (size_t i = 0; i < 20; i++) {
            result.id[i] = id[i] ^ other.id[i];
        }
        return result;
    }
};

struct DHTNode {
    NodeId id;
    udp::endpoint endpoint;
    std::chrono::steady_clock::time_point last_seen;
    bool is_good = false;
    
    DHTNode() = default;
    DHTNode(NodeId _id, udp::endpoint _ep) 
        : id(_id), endpoint(_ep), last_seen(std::chrono::steady_clock::now()) {}
    
    bool is_stale() const {
        auto now = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::minutes>(now - last_seen);
        return diff.count() > 15;
    }
    
    void mark_good() {
        is_good = true;
        last_seen = std::chrono::steady_clock::now();
    }
};
inline std::string encode_compact_node_info(const DHTNode& node) {
    std::string result;
    result.append(node.id.to_string());
    
    auto addr = node.endpoint.address().to_v4().to_bytes();
    result.append(reinterpret_cast<const char*>(addr.data()), 4);
    
    uint16_t port = htons(node.endpoint.port());
    result.append(reinterpret_cast<const char*>(&port), 2);
    
    return result;
}

inline DHTNode decode_compact_node_info(const uint8_t* data) {
    DHTNode node;
    node.id = NodeId::from_bytes(data);
    
    asio::ip::address_v4::bytes_type addr_bytes;
    std::memcpy(addr_bytes.data(), data + 20, 4);
    
    uint16_t port;
    std::memcpy(&port, data + 24, 2);
    port = ntohs(port);
    
    node.endpoint = udp::endpoint(asio::ip::address_v4(addr_bytes), port);
    return node;
}

struct RoutingTable{
    std::vector<DHTNode> known_nodes;
};

#endif