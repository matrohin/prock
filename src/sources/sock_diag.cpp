#include "sock_diag.h"

#include "base/containers.h"

#include <cerrno>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>

#include <algorithm>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// Query all TCP/UDP sockets via netlink SOCK_DIAG
// Returns array sorted by inode for binary search
Array<SocketEntry> sock_diag_query(BumpArena &arena, int &out_errno) {
  ZoneScoped;
  GrowingArray<SocketEntry> result = {};
  int first_errno = 0;

  const int fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_SOCK_DIAG);
  if (fd < 0) {
    out_errno = errno;
    return {};
  }

  // Query for TCP and UDP sockets (AF_INET and AF_INET6)
  struct ProtocolQuery {
    int family;
    int protocol;
    SocketProtocol socket_protocol;
  };
  constexpr ProtocolQuery queries[] = {
      {AF_INET, IPPROTO_TCP, eSocketProtocol_TCP},
      {AF_INET, IPPROTO_UDP, eSocketProtocol_UDP},
      {AF_INET6, IPPROTO_TCP, eSocketProtocol_TCP6},
      {AF_INET6, IPPROTO_UDP, eSocketProtocol_UDP6},
  };

  for (const auto &q : queries) {
    struct {
      nlmsghdr nlh;
      inet_diag_req_v2 req;
    } request = {};

    request.nlh.nlmsg_len = sizeof(request);
    request.nlh.nlmsg_type = SOCK_DIAG_BY_FAMILY;
    request.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.req.sdiag_family = static_cast<__u8>(q.family);
    request.req.sdiag_protocol = static_cast<__u8>(q.protocol);
    request.req.idiag_states = ~0U; // All states
    // Request TCP_INFO for byte counts (only meaningful for TCP)
    if (q.protocol == IPPROTO_TCP) {
      request.req.idiag_ext |= 1 << (INET_DIAG_INFO - 1);
    }

    if (send(fd, &request, sizeof(request), 0) < 0) {
      if (first_errno == 0) first_errno = errno;
      continue;
    }

    bool done = false;
    char buf[16384];
    while (!done) {
      ssize_t len = recv(fd, buf, sizeof(buf), 0);
      if (len <= 0) {
        if (len < 0 && first_errno == 0) first_errno = errno;
        break;
      }

// NLMSG_NEXT/RTA_NEXT walk buffers the kernel guarantees are aligned.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
      for (nlmsghdr *h = reinterpret_cast<nlmsghdr *>(buf); NLMSG_OK(h, len);
           h = NLMSG_NEXT(h, len)) {
        if (h->nlmsg_type == NLMSG_DONE) {
          done = true;
          break;
        }
        if (h->nlmsg_type == NLMSG_ERROR) {
          // E.g. ENOENT when the kernel has no inet_diag handler for this
          // family/protocol (module missing or unloadable).
          const nlmsgerr *err = static_cast<const nlmsgerr *>(NLMSG_DATA(h));
          if (err->error != 0 && first_errno == 0) first_errno = -err->error;
          done = true;
          break;
        }

        inet_diag_msg *diag = static_cast<inet_diag_msg *>(NLMSG_DATA(h));
        const unsigned long inode = diag->idiag_inode;
        if (inode == 0) continue;

        SocketEntry *entry = result.emplace_back(arena);
        entry->inode = inode;
        entry->protocol = q.socket_protocol;
        entry->state = static_cast<TcpState>(diag->idiag_state);
        entry->tx_queue = diag->idiag_wqueue;
        entry->rx_queue = diag->idiag_rqueue;
        entry->bytes_received = 0;
        entry->bytes_sent = 0;

        // Extract addresses and ports
        entry->local_port = ntohs(diag->id.idiag_sport);
        entry->remote_port = ntohs(diag->id.idiag_dport);

        if (q.family == AF_INET) {
          entry->local_ip = diag->id.idiag_src[0];
          entry->remote_ip = diag->id.idiag_dst[0];
          memset(entry->local_ip6, 0, sizeof(entry->local_ip6));
          memset(entry->remote_ip6, 0, sizeof(entry->remote_ip6));
        } else {
          entry->local_ip = 0;
          entry->remote_ip = 0;
          memcpy(entry->local_ip6, diag->id.idiag_src, 16);
          memcpy(entry->remote_ip6, diag->id.idiag_dst, 16);
        }

        // Parse response attributes for TCP_INFO (byte counts)
        if (q.protocol == IPPROTO_TCP) {
          unsigned int rta_len = h->nlmsg_len - NLMSG_LENGTH(sizeof(*diag));
          for (rtattr *attr = reinterpret_cast<rtattr *>(diag + 1);
               RTA_OK(attr, rta_len); attr = RTA_NEXT(attr, rta_len)) {
            if (attr->rta_type == INET_DIAG_INFO) {
              const tcp_info *info = static_cast<tcp_info *>(RTA_DATA(attr));
              entry->bytes_received = info->tcpi_bytes_received;
              entry->bytes_sent = info->tcpi_bytes_acked;
            }
          }
        }
      }
#pragma GCC diagnostic pop
    }
  }

  close(fd);

  if (result.size() == 0 && first_errno != 0) out_errno = first_errno;

  // Sort by inode for binary search
  std::sort(result.begin(), result.end(),
            [](const SocketEntry &a, const SocketEntry &b) {
              return a.inode < b.inode;
            });

  return result.to_array();
}
