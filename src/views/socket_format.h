#pragma once

#include "base/base.h"
#include "base/string.h"
#include "sources/process_stat.h"

const char *tcp_state_name(int state);
const char *socket_state_name(SocketProtocol protocol, TcpState state);
const char *protocol_name(int protocol);
bool is_tcp(SocketProtocol protocol);

String format_ipv4(BumpArena &arena, unsigned int ip, unsigned short port);
String format_ipv6(BumpArena &arena, const unsigned char *ip,
                   unsigned short port);
String format_address(BumpArena &arena, const SocketEntry &sock, bool local);
