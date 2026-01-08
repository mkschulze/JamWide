/*
    NINJAM CLAP Plugin - server_list_types.h
    Shared types for public server list
*/

#ifndef SERVER_LIST_TYPES_H
#define SERVER_LIST_TYPES_H

#include <string>

struct ServerListEntry {
    std::string name;
    std::string host;
    int port = 0;
    int users = 0;
    std::string topic;
};

#endif // SERVER_LIST_TYPES_H
