/*
    NINJAM CLAP Plugin - server_list.cpp
    Public server list fetcher implementation
*/

#include "server_list.h"
#include "wdl/jsonparse.h"

#include <cstdlib>

namespace ninjam {

namespace {

int parse_int(const char* value, int fallback) {
    if (!value || !value[0]) {
        return fallback;
    }
    char* end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (end == value) {
        return fallback;
    }
    return static_cast<int>(parsed);
}

const char* get_string_or_empty(const wdl_json_element* elem,
                                const char* name) {
    if (!elem) return "";
    const char* value = elem->get_string_by_name(name, true);
    return value ? value : "";
}

const wdl_json_element* get_list_root(const wdl_json_element* root) {
    if (!root) return nullptr;
    if (root->is_array()) return root;
    if (root->is_object()) {
        const wdl_json_element* servers = root->get_item_by_name("servers");
        if (servers && servers->is_array()) return servers;
    }
    return nullptr;
}

int count_array_items(const wdl_json_element* elem) {
    if (!elem || !elem->is_array()) {
        return 0;
    }
    int count = 0;
    while (elem->enum_item(count)) {
        ++count;
    }
    return count;
}

int parse_user_count(const wdl_json_element* item) {
    if (!item) return 0;

    const char* count_value = item->get_string_by_name("user_count", true);
    if (count_value && count_value[0]) {
        return parse_int(count_value, 0);
    }

    const char* users_value = item->get_string_by_name("users", true);
    if (users_value && users_value[0]) {
        return parse_int(users_value, 0);
    }

    const wdl_json_element* users_elem = item->get_item_by_name("users");
    if (users_elem && users_elem->is_array()) {
        return count_array_items(users_elem);
    }

    return 0;
}

} // namespace

ServerListFetcher::ServerListFetcher() = default;

void ServerListFetcher::reset_state() {
    buffer_.clear();
    reply_code_ = 0;
    active_ = false;
}

void ServerListFetcher::request(const std::string& url) {
    reset_state();
    url_ = url;
    if (url_.empty()) {
        return;
    }
    http_.addheader("User-Agent: NINJAM-CLAP");
    http_.addheader("Accept: application/json");
    http_.connect(url_.c_str());
    active_ = true;
}

bool ServerListFetcher::in_flight() const {
    return active_;
}

bool ServerListFetcher::poll(ServerListResult& result) {
    if (!active_) {
        return false;
    }

    int st = http_.run();
    if (st < 0) {
        result.servers.clear();
        result.error = http_.geterrorstr();
        reset_state();
        return true;
    }

    if (http_.get_status() >= 2) {
        int available = 0;
        while ((available = http_.bytes_available()) > 0) {
            char buf[4096];
            if (available > static_cast<int>(sizeof(buf))) {
                available = static_cast<int>(sizeof(buf));
            }
            const int read = http_.get_bytes(buf, available);
            if (read > 0) {
                buffer_.append(buf, static_cast<size_t>(read));
            } else {
                break;
            }
        }
    }

    if (st == 1) {
        reply_code_ = http_.getreplycode();
        if (reply_code_ < 200 || reply_code_ >= 300) {
            result.servers.clear();
            result.error = http_.getreply();
            reset_state();
            return true;
        }

        result.servers.clear();
        result.error.clear();
        const bool ok = parse_response(buffer_, result);
        if (!ok && result.error.empty()) {
            result.error = "Failed to parse server list";
        }
        reset_state();
        return true;
    }

    return false;
}

bool ServerListFetcher::parse_response(const std::string& data,
                                       ServerListResult& result) {
    wdl_json_parser parser;
    wdl_json_element* root = parser.parse(data.c_str(),
                                          static_cast<int>(data.size()));
    if (!root) {
        result.error = parser.m_err ? parser.m_err : "JSON parse error";
        return false;
    }

    const wdl_json_element* list = get_list_root(root);
    if (!list) {
        result.error = "Server list not found";
        delete root;
        return false;
    }

    for (int i = 0; ; ++i) {
        const wdl_json_element* item = list->enum_item(i);
        if (!item) break;
        if (!item->is_object()) continue;

        ServerListEntry entry;
        entry.name = get_string_or_empty(item, "name");
        entry.host = get_string_or_empty(item, "host");
        if (entry.host.empty()) {
            entry.host = get_string_or_empty(item, "ip");
        }
        if (entry.host.empty()) {
            entry.host = get_string_or_empty(item, "address");
        }

        const char* port_value = item->get_string_by_name("port", true);
        entry.port = parse_int(port_value, 0);
        if (entry.port == 0) {
            const char* port_alt = item->get_string_by_name("portnum", true);
            entry.port = parse_int(port_alt, 0);
        }

        entry.users = parse_user_count(item);

        entry.topic = get_string_or_empty(item, "topic");

        if (!entry.host.empty()) {
            result.servers.push_back(std::move(entry));
        }
    }

    delete root;
    return true;
}

} // namespace ninjam
