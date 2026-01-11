/*
    JamWide Plugin - server_list.h
    Public server list fetcher using JNetLib HTTP GET
*/

#ifndef SERVER_LIST_H
#define SERVER_LIST_H

#include <string>
#include <vector>

#include "wdl/jnetlib/httpget.h"
#include "ui/server_list_types.h"

namespace jamwide {

struct ServerListResult {
    std::vector<ServerListEntry> servers;
    std::string error;
};

class ServerListFetcher {
public:
    ServerListFetcher();
    void request(const std::string& url);
    bool in_flight() const;
    bool poll(ServerListResult& result);

private:
    void reset_state();
    bool parse_response(const std::string& data, ServerListResult& result);

    JNL_HTTPGet http_;
    bool active_ = false;
    std::string buffer_;
    std::string url_;
    int reply_code_ = 0;
};

} // namespace jamwide

#endif // SERVER_LIST_H
