#pragma once

#include "sources/process_stat.h"

#include <cstddef>

const char *tcp_state_name(int state);
const char *protocol_name(int protocol);
bool is_tcp(SocketProtocol protocol);

void format_ipv4(char *buf, size_t buf_size, unsigned int ip,
                 unsigned short port);
void format_ipv6(char *buf, size_t buf_size, const unsigned char *ip,
                 unsigned short port);
void format_address(char *buf, size_t buf_size, const SocketEntry &sock,
                    bool local);
