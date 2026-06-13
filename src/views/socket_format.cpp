#include "views/socket_format.h"
#include "base/string.h"

#include <cstdio>

const char *tcp_state_name(const int state) {
  switch (state) {
  case eTcpState_ESTABLISHED:
    return "ESTABLISHED";
  case eTcpState_SYN_SENT:
    return "SYN_SENT";
  case eTcpState_SYN_RECV:
    return "SYN_RECV";
  case eTcpState_FIN_WAIT1:
    return "FIN_WAIT1";
  case eTcpState_FIN_WAIT2:
    return "FIN_WAIT2";
  case eTcpState_TIME_WAIT:
    return "TIME_WAIT";
  case eTcpState_CLOSE:
    return "CLOSE";
  case eTcpState_CLOSE_WAIT:
    return "CLOSE_WAIT";
  case eTcpState_LAST_ACK:
    return "LAST_ACK";
  case eTcpState_LISTEN:
    return "LISTEN";
  case eTcpState_CLOSING:
    return "CLOSING";
  default:
    return "UNKNOWN";
  }
}

const char *socket_state_name(const SocketProtocol protocol,
                              const TcpState state) {
  if (is_tcp(protocol)) {
    return tcp_state_name(state);
  }
  // The kernel reports UDP sockets as ESTABLISHED (connected) or CLOSE
  // (unconnected); show the latter as UNCONN like ss does.
  return state == eTcpState_ESTABLISHED ? "ESTABLISHED" : "UNCONN";
}

const char *protocol_name(const int protocol) {
  switch (protocol) {
  case eSocketProtocol_TCP:
    return "TCP";
  case eSocketProtocol_UDP:
    return "UDP";
  case eSocketProtocol_TCP6:
    return "TCP6";
  case eSocketProtocol_UDP6:
    return "UDP6";
  default:
    return "???";
  }
}

bool is_tcp(const SocketProtocol protocol) {
  return protocol == eSocketProtocol_TCP || protocol == eSocketProtocol_TCP6;
}

String format_ipv4(BumpArena &arena, const unsigned int ip,
                   const unsigned short port) {
  return String::sprintf(arena, "%u.%u.%u.%u:%u", (ip >> 0) & 0xFF,
                         (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF,
                         port);
}

String format_ipv6(BumpArena &arena, const unsigned char *ip,
                   const unsigned short port) {
  // IPv4-mapped IPv6 (::ffff:x.x.x.x)
  bool is_v4_mapped = true;
  for (int i = 0; i < 10; ++i) {
    if (ip[i] != 0) {
      is_v4_mapped = false;
      break;
    }
  }
  if (is_v4_mapped && ip[10] == 0xFF && ip[11] == 0xFF) {
    return String::sprintf(arena, "[::ffff:%u.%u.%u.%u]:%u", ip[12], ip[13],
                           ip[14], ip[15], port);
  }

  // Loopback (::1)
  bool is_loopback = true;
  for (int i = 0; i < 15; ++i) {
    if (ip[i] != 0) {
      is_loopback = false;
      break;
    }
  }
  if (is_loopback && ip[15] == 1) {
    return String::sprintf(arena, "[::1]:%u", port);
  }

  // All zeros (::)
  bool is_any = true;
  for (int i = 0; i < 16; ++i) {
    if (ip[i] != 0) {
      is_any = false;
      break;
    }
  }
  if (is_any) {
    return String::sprintf(arena, "[::]:%u", port);
  }

  return String::sprintf(
      arena,
      "[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%"
      "02x%02x]:%u",
      ip[0], ip[1], ip[2], ip[3], ip[4], ip[5], ip[6], ip[7], ip[8], ip[9],
      ip[10], ip[11], ip[12], ip[13], ip[14], ip[15], port);
}

String format_address(BumpArena &arena, const SocketEntry &sock,
                      const bool local) {
  const bool is_ipv6 = (sock.protocol == eSocketProtocol_TCP6 ||
                        sock.protocol == eSocketProtocol_UDP6);
  if (is_ipv6) {
    return format_ipv6(arena, local ? sock.local_ip6 : sock.remote_ip6,
                       local ? sock.local_port : sock.remote_port);
  }
  return format_ipv4(arena, local ? sock.local_ip : sock.remote_ip,
                     local ? sock.local_port : sock.remote_port);
}
