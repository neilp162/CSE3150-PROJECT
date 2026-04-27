// File: web/src/simulator_wasm.cpp
//
// Browser-facing WebAssembly wrapper for the BGP simulator. The exported
// simulate_target function accepts file contents as strings and returns a CSV
// containing only the routes visible at one target ASN.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using ASN = std::uint32_t;

enum class ReceivedFrom {
    Provider,
    Peer,
    Customer,
    Origin,
};

struct Node {
    ASN asn = 0;
    std::vector<int> providers;
    std::vector<int> customers;
    std::vector<int> peers;
    bool deploys_rov = false;
    int rank = 0;
};

struct Seed {
    ASN seed_asn = 0;
    bool rov_invalid = false;
};

struct PrefixSeeds {
    std::string prefix;
    std::vector<Seed> seeds;
};

struct Route {
    bool present = false;
    ReceivedFrom received_from = ReceivedFrom::Provider;
    ASN next_hop = 0;
    bool rov_invalid = false;
    std::vector<ASN> path;
};

struct CandidateMetrics {
    ReceivedFrom received_from = ReceivedFrom::Provider;
    std::size_t path_length = 0;
    ASN next_hop = 0;
    bool rov_invalid = false;
};

static std::string trim(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }

    return value.substr(first, last - first);
}

static std::string lower_copy(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

static std::vector<std::string> split_pipe_line(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= line.size()) {
        const std::size_t end = line.find('|', start);
        if (end == std::string::npos) {
            fields.push_back(trim(line.substr(start)));
            break;
        }
        fields.push_back(trim(line.substr(start, end - start)));
        start = end + 1;
    }
    return fields;
}

static std::vector<std::string> parse_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                current.push_back('"');
                ++i;
            } else {
                in_quotes = !in_quotes;
            }
        } else if (ch == ',' && !in_quotes) {
            fields.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }

    fields.push_back(trim(current));
    return fields;
}

static bool is_unsigned_integer(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

static ASN parse_asn(const std::string& value, const std::string& context) {
    const std::string cleaned = trim(value);
    if (!is_unsigned_integer(cleaned)) {
        throw std::runtime_error("invalid ASN in " + context + ": " + value);
    }

    const unsigned long long parsed = std::stoull(cleaned);
    if (parsed > std::numeric_limits<ASN>::max()) {
        throw std::runtime_error("ASN out of range in " + context + ": " + value);
    }
    return static_cast<ASN>(parsed);
}

static bool parse_bool(const std::string& value, const std::string& context) {
    const std::string lowered = lower_copy(trim(value));
    if (lowered == "true" || lowered == "1" || lowered == "yes" ||
        lowered == "y") {
        return true;
    }
    if (lowered == "false" || lowered == "0" || lowered == "no" ||
        lowered == "n") {
        return false;
    }
    throw std::runtime_error("invalid boolean in " + context + ": " + value);
}

static int preference_rank(ReceivedFrom relationship) {
    switch (relationship) {
        case ReceivedFrom::Origin:
            return 3;
        case ReceivedFrom::Customer:
            return 2;
        case ReceivedFrom::Peer:
            return 1;
        case ReceivedFrom::Provider:
            return 0;
    }
    return 0;
}

static const char* relationship_name(ReceivedFrom relationship) {
    switch (relationship) {
        case ReceivedFrom::Origin:
            return "origin";
        case ReceivedFrom::Customer:
            return "customer";
        case ReceivedFrom::Peer:
            return "peer";
        case ReceivedFrom::Provider:
            return "provider";
    }
    return "unknown";
}

static bool candidate_better_than_route(const CandidateMetrics& candidate,
                                        const Route& route) {
    if (!route.present) {
        return true;
    }

    const int candidate_preference = preference_rank(candidate.received_from);
    const int route_preference = preference_rank(route.received_from);
    if (candidate_preference != route_preference) {
        return candidate_preference > route_preference;
    }

    if (candidate.path_length != route.path.size()) {
        return candidate.path_length < route.path.size();
    }

    if (candidate.next_hop != route.next_hop) {
        return candidate.next_hop < route.next_hop;
    }

    if (candidate.rov_invalid != route.rov_invalid) {
        return !candidate.rov_invalid;
    }

    return false;
}

static bool route_better_than_route(const Route& left, const Route& right) {
    if (!left.present) {
        return false;
    }

    CandidateMetrics metrics{};
    metrics.received_from = left.received_from;
    metrics.path_length = left.path.size();
    metrics.next_hop = left.next_hop;
    metrics.rov_invalid = left.rov_invalid;
    return candidate_better_than_route(metrics, right);
}

class ASGraph {
public:
    ASGraph() {
        asn_to_index_.reserve(131072);
    }

    int get_or_create_node(ASN asn) {
        const auto existing = asn_to_index_.find(asn);
        if (existing != asn_to_index_.end()) {
            return existing->second;
        }

        const int index = static_cast<int>(nodes_.size());
        Node node{};
        node.asn = asn;
        nodes_.push_back(std::move(node));
        asn_to_index_[asn] = index;
        return index;
    }

    int find_node(ASN asn) const {
        const auto existing = asn_to_index_.find(asn);
        if (existing == asn_to_index_.end()) {
            return -1;
        }
        return existing->second;
    }

    void load_relationships(const std::string& text) {
        std::istringstream in(text);
        std::string line;
        std::size_t line_number = 0;

        while (std::getline(in, line)) {
            ++line_number;
            line = trim(line);
            if (line.empty() || line[0] == '#') {
                continue;
            }

            const std::vector<std::string> fields = split_pipe_line(line);
            if (fields.size() < 3) {
                throw std::runtime_error(
                    "relationship line " + std::to_string(line_number) +
                    " must have at least 3 pipe-separated fields");
            }

            const ASN left_asn = parse_asn(fields[0], "relationships line " +
                                                       std::to_string(line_number));
            const ASN right_asn = parse_asn(fields[1], "relationships line " +
                                                        std::to_string(line_number));
            const int relationship = std::stoi(fields[2]);

            const int left = get_or_create_node(left_asn);
            const int right = get_or_create_node(right_asn);

            if (relationship == -1) {
                nodes_[left].customers.push_back(right);
                nodes_[right].providers.push_back(left);
            } else if (relationship == 0) {
                nodes_[left].peers.push_back(right);
                nodes_[right].peers.push_back(left);
            } else {
                throw std::runtime_error(
                    "unsupported relationship value on line " +
                    std::to_string(line_number) + ": " + fields[2]);
            }
        }
    }

    std::vector<std::vector<int>> build_propagation_ranks() {
        std::vector<int> remaining_customers(nodes_.size(), 0);
        std::deque<int> ready;

        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            remaining_customers[i] =
                static_cast<int>(nodes_[i].customers.size());
            nodes_[i].rank = 0;
            if (remaining_customers[i] == 0) {
                ready.push_back(static_cast<int>(i));
            }
        }

        std::size_t processed = 0;
        int max_rank = 0;
        while (!ready.empty()) {
            const int current = ready.front();
            ready.pop_front();
            ++processed;

            for (const int provider : nodes_[current].providers) {
                nodes_[provider].rank =
                    std::max(nodes_[provider].rank, nodes_[current].rank + 1);
                max_rank = std::max(max_rank, nodes_[provider].rank);

                --remaining_customers[provider];
                if (remaining_customers[provider] == 0) {
                    ready.push_back(provider);
                }
            }
        }

        if (processed != nodes_.size()) {
            throw std::runtime_error(
                "provider/customer cycle detected in AS relationships");
        }

        std::vector<std::vector<int>> ranks(static_cast<std::size_t>(max_rank) + 1);
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            ranks[static_cast<std::size_t>(nodes_[i].rank)].push_back(
                static_cast<int>(i));
        }
        return ranks;
    }

    std::vector<Node>& nodes() {
        return nodes_;
    }

    const std::vector<Node>& nodes() const {
        return nodes_;
    }

private:
    std::vector<Node> nodes_;
    std::unordered_map<ASN, int> asn_to_index_;
};

static std::vector<PrefixSeeds> load_announcements(const std::string& text,
                                                   ASGraph& graph) {
    std::istringstream in(text);
    std::vector<PrefixSeeds> prefixes;
    std::unordered_map<std::string, std::size_t> prefix_to_index;
    prefix_to_index.reserve(128);

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const std::vector<std::string> fields = parse_csv_line(line);
        if (fields.size() < 3) {
            throw std::runtime_error(
                "announcement line " + std::to_string(line_number) +
                " must have seed_asn,prefix,rov_invalid");
        }

        if (line_number == 1 && !is_unsigned_integer(fields[0])) {
            continue;
        }

        const ASN seed_asn =
            parse_asn(fields[0], "announcements line " +
                                     std::to_string(line_number));
        const std::string prefix = trim(fields[1]);
        if (prefix.empty()) {
            throw std::runtime_error("empty prefix on announcements line " +
                                     std::to_string(line_number));
        }
        const bool rov_invalid =
            parse_bool(fields[2], "announcements line " +
                                      std::to_string(line_number));

        graph.get_or_create_node(seed_asn);

        const auto existing = prefix_to_index.find(prefix);
        std::size_t index = 0;
        if (existing == prefix_to_index.end()) {
            index = prefixes.size();
            PrefixSeeds entry{};
            entry.prefix = prefix;
            prefixes.push_back(std::move(entry));
            prefix_to_index[prefix] = index;
        } else {
            index = existing->second;
        }
        prefixes[index].seeds.push_back(Seed{seed_asn, rov_invalid});
    }

    return prefixes;
}

static void load_rov_asns(const std::string& text, ASGraph& graph) {
    std::istringstream in(text);
    std::string line;
    std::size_t line_number = 0;

    while (std::getline(in, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const std::vector<std::string> fields = parse_csv_line(line);
        if (fields.empty() || !is_unsigned_integer(fields[0])) {
            if (line_number == 1) {
                continue;
            }
            throw std::runtime_error("invalid ASN in ROV file on line " +
                                     std::to_string(line_number));
        }

        const ASN asn =
            parse_asn(fields[0], "ROV file line " + std::to_string(line_number));
        graph.nodes()[graph.get_or_create_node(asn)].deploys_rov = true;
    }
}

static bool path_contains_asn(const std::vector<ASN>& path, ASN asn) {
    return std::find(path.begin(), path.end(), asn) != path.end();
}

class Simulator {
public:
    Simulator(const ASGraph& graph,
              const std::vector<std::vector<int>>& ranks,
              int target_node)
        : graph_(graph), ranks_(ranks), target_node_(target_node) {}

    Route simulate_prefix(const PrefixSeeds& prefix) const {
        std::vector<Route> routes(graph_.nodes().size());
        std::vector<Route> pending(graph_.nodes().size());
        std::vector<unsigned char> pending_mark(graph_.nodes().size(), 0);
        std::vector<int> pending_nodes;

        seed_routes(prefix, routes);
        propagate_up(routes, pending, pending_mark, pending_nodes);
        clear_pending(pending, pending_mark, pending_nodes);

        propagate_across_peers(routes, pending, pending_mark, pending_nodes);
        process_all_pending(routes, pending, pending_mark, pending_nodes);

        propagate_down(routes, pending, pending_mark, pending_nodes);
        clear_pending(pending, pending_mark, pending_nodes);

        if (target_node_ < 0 ||
            target_node_ >= static_cast<int>(routes.size())) {
            return Route{};
        }
        return routes[target_node_];
    }

private:
    void seed_routes(const PrefixSeeds& prefix, std::vector<Route>& routes) const {
        for (const Seed& seed : prefix.seeds) {
            const int node = graph_.find_node(seed.seed_asn);
            if (node < 0) {
                continue;
            }

            Route route{};
            route.present = true;
            route.received_from = ReceivedFrom::Origin;
            route.next_hop = seed.seed_asn;
            route.rov_invalid = seed.rov_invalid;
            route.path.push_back(seed.seed_asn);

            if (!routes[node].present ||
                route_better_than_route(route, routes[node])) {
                routes[node] = std::move(route);
            }
        }
    }

    void propagate_up(std::vector<Route>& routes,
                      std::vector<Route>& pending,
                      std::vector<unsigned char>& pending_mark,
                      std::vector<int>& pending_nodes) const {
        for (const std::vector<int>& rank_nodes : ranks_) {
            for (const int node : rank_nodes) {
                process_pending_node(node, routes, pending, pending_mark);
            }

            for (const int node : rank_nodes) {
                if (!routes[node].present) {
                    continue;
                }
                for (const int provider : graph_.nodes()[node].providers) {
                    send_candidate(node,
                                   provider,
                                   ReceivedFrom::Customer,
                                   routes,
                                   pending,
                                   pending_mark,
                                   pending_nodes);
                }
            }
        }
    }

    void propagate_across_peers(std::vector<Route>& routes,
                                std::vector<Route>& pending,
                                std::vector<unsigned char>& pending_mark,
                                std::vector<int>& pending_nodes) const {
        for (std::size_t node = 0; node < graph_.nodes().size(); ++node) {
            if (!routes[node].present) {
                continue;
            }
            for (const int peer : graph_.nodes()[node].peers) {
                send_candidate(static_cast<int>(node),
                               peer,
                               ReceivedFrom::Peer,
                               routes,
                               pending,
                               pending_mark,
                               pending_nodes);
            }
        }
    }

    void propagate_down(std::vector<Route>& routes,
                        std::vector<Route>& pending,
                        std::vector<unsigned char>& pending_mark,
                        std::vector<int>& pending_nodes) const {
        for (std::size_t rank = ranks_.size(); rank > 0; --rank) {
            const std::vector<int>& rank_nodes = ranks_[rank - 1];
            for (const int node : rank_nodes) {
                process_pending_node(node, routes, pending, pending_mark);
            }

            for (const int node : rank_nodes) {
                if (!routes[node].present) {
                    continue;
                }
                for (const int customer : graph_.nodes()[node].customers) {
                    send_candidate(node,
                                   customer,
                                   ReceivedFrom::Provider,
                                   routes,
                                   pending,
                                   pending_mark,
                                   pending_nodes);
                }
            }
        }
    }

    void send_candidate(int sender,
                        int receiver,
                        ReceivedFrom received_from,
                        const std::vector<Route>& routes,
                        std::vector<Route>& pending,
                        std::vector<unsigned char>& pending_mark,
                        std::vector<int>& pending_nodes) const {
        const Route& sender_route = routes[sender];
        if (!sender_route.present) {
            return;
        }

        const Node& receiver_node = graph_.nodes()[receiver];
        if (receiver_node.deploys_rov && sender_route.rov_invalid) {
            return;
        }

        if (path_contains_asn(sender_route.path, receiver_node.asn)) {
            return;
        }

        CandidateMetrics metrics{};
        metrics.received_from = received_from;
        metrics.path_length = sender_route.path.size() + 1;
        metrics.next_hop = graph_.nodes()[sender].asn;
        metrics.rov_invalid = sender_route.rov_invalid;

        if (routes[receiver].present &&
            !candidate_better_than_route(metrics, routes[receiver])) {
            return;
        }
        if (pending[receiver].present &&
            !candidate_better_than_route(metrics, pending[receiver])) {
            return;
        }

        Route candidate{};
        candidate.present = true;
        candidate.received_from = received_from;
        candidate.next_hop = graph_.nodes()[sender].asn;
        candidate.rov_invalid = sender_route.rov_invalid;
        candidate.path.reserve(sender_route.path.size() + 1);
        candidate.path.push_back(receiver_node.asn);
        candidate.path.insert(candidate.path.end(),
                              sender_route.path.begin(),
                              sender_route.path.end());

        pending[receiver] = std::move(candidate);
        if (pending_mark[receiver] == 0) {
            pending_mark[receiver] = 1;
            pending_nodes.push_back(receiver);
        }
    }

    static void process_pending_node(int node,
                                     std::vector<Route>& routes,
                                     std::vector<Route>& pending,
                                     std::vector<unsigned char>& pending_mark) {
        if (!pending[node].present) {
            return;
        }

        if (!routes[node].present ||
            route_better_than_route(pending[node], routes[node])) {
            routes[node] = std::move(pending[node]);
        }

        pending[node].present = false;
        pending[node].path.clear();
        pending_mark[node] = 0;
    }

    static void process_all_pending(std::vector<Route>& routes,
                                    std::vector<Route>& pending,
                                    std::vector<unsigned char>& pending_mark,
                                    std::vector<int>& pending_nodes) {
        for (const int node : pending_nodes) {
            process_pending_node(node, routes, pending, pending_mark);
        }
        pending_nodes.clear();
    }

    static void clear_pending(std::vector<Route>& pending,
                              std::vector<unsigned char>& pending_mark,
                              std::vector<int>& pending_nodes) {
        for (const int node : pending_nodes) {
            pending[node].present = false;
            pending[node].path.clear();
            pending_mark[node] = 0;
        }
        pending_nodes.clear();
    }

    const ASGraph& graph_;
    const std::vector<std::vector<int>>& ranks_;
    int target_node_ = -1;
};

static void write_path(const std::vector<ASN>& path, std::ostream& out) {
    out << '(';
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << path[i];
    }
    if (path.size() == 1) {
        out << ',';
    }
    out << ')';
}

static std::string simulate_target_impl(const std::string& relationships,
                                        const std::string& announcements_csv,
                                        const std::string& rov_asns_csv,
                                        ASN target_asn) {
    ASGraph graph;
    graph.load_relationships(relationships);
    load_rov_asns(rov_asns_csv, graph);
    const std::vector<PrefixSeeds> announcements =
        load_announcements(announcements_csv, graph);

    const std::vector<std::vector<int>> ranks = graph.build_propagation_ranks();
    const int target_node = graph.find_node(target_asn);

    std::ostringstream out;
    out << "target_asn,prefix,as_path,next_hop,received_from,rov_invalid\n";
    if (target_node < 0) {
        return out.str();
    }

    const Simulator simulator(graph, ranks, target_node);
    for (const PrefixSeeds& prefix : announcements) {
        const Route route = simulator.simulate_prefix(prefix);
        if (!route.present) {
            continue;
        }

        out << target_asn << ',' << prefix.prefix << ",\"";
        write_path(route.path, out);
        out << "\"," << route.next_hop << ','
            << relationship_name(route.received_from) << ','
            << (route.rov_invalid ? "true" : "false") << '\n';
    }

    return out.str();
}

static char* copy_result(const std::string& value) {
    char* result = static_cast<char*>(std::malloc(value.size() + 1));
    if (result == nullptr) {
        return nullptr;
    }
    std::memcpy(result, value.c_str(), value.size() + 1);
    return result;
}

extern "C" {

char* simulate_target(const char* relationships,
                      const char* announcements_csv,
                      const char* rov_asns_csv,
                      unsigned int target_asn) {
    try {
        if (relationships == nullptr || announcements_csv == nullptr ||
            rov_asns_csv == nullptr) {
            return copy_result("ERROR: missing input text");
        }

        return copy_result(simulate_target_impl(relationships,
                                                announcements_csv,
                                                rov_asns_csv,
                                                static_cast<ASN>(target_asn)));
    } catch (const std::exception& ex) {
        return copy_result(std::string("ERROR: ") + ex.what());
    }
}

void free_result(char* pointer) {
    std::free(pointer);
}

}
