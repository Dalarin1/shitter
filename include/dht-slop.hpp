#ifndef INCLUDE_DHT_HPP_
#define INCLUDE_DHT_HPP_
#include "asio.hpp"
#include "bencode.hpp"
#include "torrent.hpp"
#include <random>
#include <deque>

using asio::ip::udp;

struct NodeID {
    std::array<uint8_t, 20> id;
    
    NodeID() { id.fill(0); }
    
    static NodeID random() {
        NodeID n;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        for (auto& byte : n.id) {
            byte = dis(gen);
        }
        return n;
    }
    
    static NodeID from_bytes(const uint8_t* data) {
        NodeID n;
        std::memcpy(n.id.data(), data, 20);
        return n;
    }
    
    std::string to_string() const {
        return std::string(reinterpret_cast<const char*>(id.data()), 20);
    }
    
    // XOR distance
    NodeID distance(const NodeID& other) const {
        NodeID result;
        for (size_t i = 0; i < 20; i++) {
            result.id[i] = id[i] ^ other.id[i];
        }
        return result;
    }
    
    bool operator<(const NodeID& other) const {
        return std::memcmp(id.data(), other.id.data(), 20) < 0;
    }
    
    bool operator==(const NodeID& other) const {
        return std::memcmp(id.data(), other.id.data(), 20) == 0;
    }
};

struct DHTNode {
    NodeID id;
    asio::ip::udp::endpoint endpoint;
    std::chrono::steady_clock::time_point last_seen;
    bool is_good = false;
    
    DHTNode() = default;
    DHTNode(NodeID _id, udp::endpoint _ep) 
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

// Компактное представление узла (26 байт)
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
    node.id = NodeID::from_bytes(data);
    
    asio::ip::address_v4::bytes_type addr_bytes;
    std::memcpy(addr_bytes.data(), data + 20, 4);
    
    uint16_t port;
    std::memcpy(&port, data + 24, 2);
    port = ntohs(port);
    
    node.endpoint = udp::endpoint(asio::ip::address_v4(addr_bytes), port);
    return node;
}

// ==================== Routing Table ====================

struct RoutingBucket {
    static constexpr size_t K = 8; // K ближайших узлов
    std::vector<DHTNode> nodes;
    std::chrono::steady_clock::time_point last_changed;
    
    RoutingBucket() : last_changed(std::chrono::steady_clock::now()) {}
    
    bool can_add() const { return nodes.size() < K; }
    
    void add_node(const DHTNode& node) {
        // Проверяем, нет ли уже этого узла
        auto it = std::find_if(nodes.begin(), nodes.end(), 
            [&](const DHTNode& n) { return n.id == node.id; });
        
        if (it != nodes.end()) {
            it->mark_good();
            return;
        }
        
        if (can_add()) {
            nodes.push_back(node);
            last_changed = std::chrono::steady_clock::now();
        } else {
            // Заменяем плохие узлы
            auto bad = std::find_if(nodes.begin(), nodes.end(),
                [](const DHTNode& n) { return !n.is_good || n.is_stale(); });
            
            if (bad != nodes.end()) {
                *bad = node;
                last_changed = std::chrono::steady_clock::now();
            }
        }
    }
    
    std::vector<DHTNode> get_closest(const NodeID& target, size_t count) const {
        std::vector<std::pair<NodeID, DHTNode>> with_distance;
        for (const auto& node : nodes) {
            if (node.is_good && !node.is_stale()) {
                with_distance.push_back({target.distance(node.id), node});
            }
        }
        
        std::sort(with_distance.begin(), with_distance.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
        
        std::vector<DHTNode> result;
        for (size_t i = 0; i < std::min(count, with_distance.size()); i++) {
            result.push_back(with_distance[i].second);
        }
        return result;
    }
};

class RoutingTable {
    NodeID my_id;
    std::vector<RoutingBucket> buckets;
    
public:
    RoutingTable(NodeID id) : my_id(id) {
        buckets.resize(160); // 160 бит = 160 возможных префиксов
    }
    
    void add_node(const DHTNode& node) {
        if (node.id == my_id) return;
        
        int bucket_index = get_bucket_index(node.id);
        buckets[bucket_index].add_node(node);
    }
    
    std::vector<DHTNode> get_closest_nodes(const NodeID& target, size_t count) const {
        std::vector<std::pair<NodeID, DHTNode>> all_nodes;
        
        for (const auto& bucket : buckets) {
            for (const auto& node : bucket.nodes) {
                if (node.is_good && !node.is_stale()) {
                    all_nodes.push_back({target.distance(node.id), node});
                }
            }
        }
        
        std::sort(all_nodes.begin(), all_nodes.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
        
        std::vector<DHTNode> result;
        for (size_t i = 0; i < std::min(count, all_nodes.size()); i++) {
            result.push_back(all_nodes[i].second);
        }
        return result;
    }
    
    size_t node_count() const {
        size_t count = 0;
        for (const auto& bucket : buckets) {
            count += bucket.nodes.size();
        }
        return count;
    }
    
private:
    int get_bucket_index(const NodeID& node_id) const {
        NodeID dist = my_id.distance(node_id);
        
        // Находим первый ненулевой бит
        for (int i = 0; i < 20; i++) {
            if (dist.id[i] != 0) {
                int leading_zeros = __builtin_clz(dist.id[i]) - 24; // clz для 32-бит, нам нужно для 8
                return i * 8 + leading_zeros;
            }
        }
        return 159;
    }
};

// ==================== DHT Client ====================

class DHTClient {
    asio::io_context& ioc;
    udp::socket socket;
    NodeID my_id;
    RoutingTable routing_table;
    
    std::mt19937 rng;
    std::map<std::string, std::function<void(const BEN_Dict&)>> pending_queries;
    std::array<uint8_t, 65536> recv_buffer;
    udp::endpoint remote_endpoint;
    
public:
    DHTClient(asio::io_context& _ioc, uint16_t port = 6881)
        : ioc(_ioc), 
          socket(_ioc, udp::endpoint(udp::v4(), port)),
          my_id(NodeID::random()),
          routing_table(my_id),
          rng(std::random_device{}()) {
        
        start_receive();
    }
    
    // Bootstrap с известными узлами
    awaitable<void> bootstrap(const std::vector<std::string>& bootstrap_nodes) {
        for (const auto& node_str : bootstrap_nodes) {
            // Парсим "host:port"
            size_t colon = node_str.find(':');
            if (colon == std::string::npos) continue;
            
            std::string host = node_str.substr(0, colon);
            uint16_t port = std::stoi(node_str.substr(colon + 1));
            
            udp::resolver resolver(ioc);
            auto results = co_await resolver.async_resolve(
                udp::v4(), host, std::to_string(port), asio::use_awaitable);
            
            if (!results.empty()) {
                udp::endpoint ep = *results.begin();
                co_await ping(ep);
            }
        }
        
        // Находим узлы близкие к нашему ID
        co_await find_node(my_id);
    }
    
    // Основной метод: найти пиров для infohash
    awaitable<std::vector<Peer>> find_peers(const std::array<uint8_t, 20>& infohash) {
        std::vector<Peer> peers;
        NodeID target = NodeID::from_bytes(infohash.data());
        
        // Получаем ближайшие узлы из routing table
        auto closest = routing_table.get_closest_nodes(target, 8);
        
        if (closest.empty()) {
            std::cerr << "DHT: No nodes in routing table!\n";
            co_return peers;
        }
        
        std::set<NodeID> queried;
        std::deque<DHTNode> to_query(closest.begin(), closest.end());
        
        // Итеративный поиск
        while (!to_query.empty() && peers.size() < 50) {
            auto node = to_query.front();
            to_query.pop_front();
            
            if (queried.count(node.id)) continue;
            queried.insert(node.id);
            
            std::cout << "DHT: Querying node " << node.endpoint << " for peers\n";
            
            auto [found_peers, found_nodes, token] = co_await get_peers(node.endpoint, infohash);
            
            // Добавляем найденных пиров
            peers.insert(peers.end(), found_peers.begin(), found_peers.end());
            
            // Добавляем новые узлы для опроса
            for (const auto& new_node : found_nodes) {
                if (!queried.count(new_node.id)) {
                    // Вставляем по порядку близости к target
                    auto dist = target.distance(new_node.id);
                    auto it = std::find_if(to_query.begin(), to_query.end(),
                        [&](const DHTNode& n) {
                            return dist < target.distance(n.id);
                        });
                    to_query.insert(it, new_node);
                    routing_table.add_node(new_node);
                }
            }
            
            // Объявляем себя узлам с токеном
            if (!token.empty() && !found_nodes.empty()) {
                co_await announce_peer(node.endpoint, infohash, token, 6881);
            }
        }
        
        std::cout << "DHT: Found " << peers.size() << " peers total\n";
        co_return peers;
    }
    
private:
    void start_receive() {
        socket.async_receive_from(
            asio::buffer(recv_buffer), remote_endpoint,
            [this](const asio::error_code& ec, size_t bytes) {
                if (!ec) {
                    handle_message(bytes);
                    start_receive();
                }
            });
    }
    
    void handle_message(size_t bytes) {
        try {
            std::string data(recv_buffer.begin(), recv_buffer.begin() + bytes);
            std::istringstream datastream(data);
            auto decoded = read_bencode(datastream);
            auto dict = decoded.get_dict();
            
            // Проверяем тип сообщения
            auto y_it = dict.find("y");
            if (y_it == dict.end()) return;
            
            std::string msg_type = y_it->second.get_str();
            
            if (msg_type == "r") {
                // Response
                handle_response(dict);
            } else if (msg_type == "q") {
                // Query - можем обрабатывать запросы от других узлов
                handle_query(dict);
            } else if (msg_type == "e") {
                // Error
                auto e_it = dict.find("e");
                if (e_it != dict.end()) {
                    auto err_list = (e_it->second.get_list());
                    int64_t code = (err_list[0].get_int());
                    std::string msg = (err_list[1].get_str());
                    std::cerr << "DHT Error " << code << ": " << msg << "\n";
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "DHT: Failed to parse message: " << e.what() << "\n";
        }
    }
    
    void handle_response(const BEN_Dict& dict) {
        auto t_it = dict.find("t");
        if (t_it == dict.end()) return;
        
        std::string txn_id = (t_it->second.get_str());
        
        auto it = pending_queries.find(txn_id);
        if (it != pending_queries.end()) {
            it->second(dict);
            pending_queries.erase(it);
        }
    }
    
    void handle_query(const BEN_Dict& dict) {
        // Можем отвечать на ping, find_node и т.д.
        // Упрощенная реализация - просто игнорируем
    }
    
    std::string generate_transaction_id() {
        uint16_t id = rng();
        return std::string(reinterpret_cast<char*>(&id), 2);
    }
    
    awaitable<void> ping(const udp::endpoint& ep) {
        std::string txn_id = generate_transaction_id();
        
        std::string query = bencode_dict({
            {"t", bencode_string(txn_id)},
            {"y", bencode_string("q")},
            {"q", bencode_string("ping")},
            {"a", bencode_dict({
                {"id", bencode_string(my_id.to_string())}
            })}
        });
        
        bool got_response = false;
        pending_queries[txn_id] = [&](const BEN_Dict& response) {
            got_response = true;
            
            auto r_it = response.find("r");
            if (r_it != response.end()) {
                auto r_dict = (r_it->second.get_dict());
                auto id_it = r_dict.find("id");
                if (id_it != r_dict.end()) {
                    std::string node_id_str = (id_it->second.get_str());
                    NodeID node_id = NodeID::from_bytes(
                        reinterpret_cast<const uint8_t*>(node_id_str.data()));
                    
                    DHTNode node(node_id, ep);
                    node.mark_good();
                    routing_table.add_node(node);
                }
            }
        };
        
        co_await socket.async_send_to(asio::buffer(query), ep, asio::use_awaitable);
        
        // Ждем ответ с таймаутом
        asio::steady_timer timer(ioc, std::chrono::seconds(5));
        co_await timer.async_wait(asio::use_awaitable);
        
        pending_queries.erase(txn_id);
    }
    
    awaitable<void> find_node(const NodeID& target) {
        auto closest = routing_table.get_closest_nodes(target, 3);
        
        for (const auto& node : closest) {
            std::string txn_id = generate_transaction_id();
            
            std::string query = bencode_dict({
                {"t", bencode_string(txn_id)},
                {"y", bencode_string("q")},
                {"q", bencode_string("find_node")},
                {"a", bencode_dict({
                    {"id", bencode_string(my_id.to_string())},
                    {"target", bencode_string(target.to_string())}
                })}
            });
            
            pending_queries[txn_id] = [this](const BEN_Dict& response) {
                auto r_it = response.find("r");
                if (r_it != response.end()) {
                    auto r_dict = (r_it->second.get_dict());
                    auto nodes_it = r_dict.find("nodes");
                    if (nodes_it != r_dict.end()) {
                        std::string nodes_str = (nodes_it->second.get_str());
                        parse_compact_nodes(nodes_str);
                    }
                }
            };
            
            co_await socket.async_send_to(asio::buffer(query), node.endpoint, asio::use_awaitable);
        }
        
        asio::steady_timer timer(ioc, std::chrono::seconds(5));
        co_await timer.async_wait(asio::use_awaitable);
    }
    
    awaitable<std::tuple<std::vector<Peer>, std::vector<DHTNode>, std::string>>
    get_peers(const udp::endpoint& ep, const std::array<uint8_t, 20>& infohash) {
        std::string txn_id = generate_transaction_id();
        
        std::string info_hash_str(reinterpret_cast<const char*>(infohash.data()), 20);
        
        std::string query = bencode_dict({
            {"t", bencode_string(txn_id)},
            {"y", bencode_string("q")},
            {"q", bencode_string("get_peers")},
            {"a", bencode_dict({
                {"id", bencode_string(my_id.to_string())},
                {"info_hash", bencode_string(info_hash_str)}
            })}
        });
        
        std::vector<Peer> peers;
        std::vector<DHTNode> nodes;
        std::string token;
        bool got_response = false;
        
        pending_queries[txn_id] = [&](const BEN_Dict& response) {
            got_response = true;
            
            auto r_it = response.find("r");
            if (r_it == response.end()) return;
            
            auto r_dict = (r_it->second.get_dict());
            
            // Получаем token
            auto token_it = r_dict.find("token");
            if (token_it != r_dict.end()) {
                token = (token_it->second.get_str());
            }
            
            // Получаем пиров
            auto values_it = r_dict.find("values");
            if (values_it != r_dict.end()) {
                auto values_list = (values_it->second.get_list());
                for (const auto& val : values_list) {
                    std::string peer_str = (val.get_str());
                    if (peer_str.size() == 6) {
                        Peer p;
                        std::memcpy(p.ip, peer_str.data(), 4);
                        std::memcpy(&p.port, peer_str.data() + 4, 2);
                        p.port = ntohs(p.port);
                        peers.push_back(p);
                    }
                }
            }
            
            // Получаем узлы
            auto nodes_it = r_dict.find("nodes");
            if (nodes_it != r_dict.end()) {
                std::string nodes_str = (nodes_it->second.get_str());
                nodes = parse_compact_nodes(nodes_str);
            }
        };
        
        co_await socket.async_send_to(asio::buffer(query), ep, asio::use_awaitable);
        
        asio::steady_timer timer(ioc, std::chrono::seconds(5));
        co_await timer.async_wait(asio::use_awaitable);
        
        pending_queries.erase(txn_id);
        
        co_return std::make_tuple(peers, nodes, token);
    }
    
    awaitable<void> announce_peer(const udp::endpoint& ep, 
                                   const std::array<uint8_t, 20>& infohash,
                                   const std::string& token, uint16_t port) {
        std::string txn_id = generate_transaction_id();
        std::string info_hash_str(reinterpret_cast<const char*>(infohash.data()), 20);
        
        std::string query = bencode_dict({
            {"t", bencode_string(txn_id)},
            {"y", bencode_string("q")},
            {"q", bencode_string("announce_peer")},
            {"a", bencode_dict({
                {"id", bencode_string(my_id.to_string())},
                {"implied_port", bencode_int(1)},
                {"info_hash", bencode_string(info_hash_str)},
                {"port", bencode_int(port)},
                {"token", bencode_string(token)}
            })}
        });
        
        co_await socket.async_send_to(asio::buffer(query), ep, asio::use_awaitable);
    }
    
    std::vector<DHTNode> parse_compact_nodes(const std::string& nodes_str) {
        std::vector<DHTNode> result;
        
        for (size_t i = 0; i + 26 <= nodes_str.size(); i += 26) {
            auto node = decode_compact_node_info(
                reinterpret_cast<const uint8_t*>(nodes_str.data() + i));
            node.mark_good();
            routing_table.add_node(node);
            result.push_back(node);
        }
        
        return result;
    }
};

#endif // INCLUDE_DHT_HPP_