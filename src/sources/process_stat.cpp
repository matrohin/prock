#include "process_stat.h"

#include "sync.h"
#include "tracy/Tracy.hpp"

#include <algorithm>
#include <dirent.h>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

// Query all TCP/UDP sockets via netlink SOCK_DIAG
// Returns array sorted by inode for binary search
Array<SocketEntry> query_sockets_netlink(BumpArena &arena) {
  GrowingArray<SocketEntry> result = {};
  size_t wasted = 0;

  const int fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_SOCK_DIAG);
  if (fd < 0) {
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
      request.req.idiag_ext |= (1 << (INET_DIAG_INFO - 1));
    }

    if (send(fd, &request, sizeof(request), 0) < 0) {
      continue;
    }

    bool done = false;
    char buf[16384];
    while (!done) {
      ssize_t len = recv(fd, buf, sizeof(buf), 0);
      if (len <= 0) break;

      for (nlmsghdr *h = reinterpret_cast<nlmsghdr *>(buf); NLMSG_OK(h, len);
           h = NLMSG_NEXT(h, len)) {
        if (h->nlmsg_type == NLMSG_DONE || h->nlmsg_type == NLMSG_ERROR) {
          done = true;
          break;
        }

        inet_diag_msg *diag = static_cast<inet_diag_msg *>(NLMSG_DATA(h));
        const unsigned long inode = diag->idiag_inode;
        if (inode == 0) continue;

        SocketEntry *entry = result.emplace_back(arena, wasted);
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
    }
  }

  close(fd);

  // Sort by inode for binary search
  std::sort(result.begin(), result.end(),
            [](const SocketEntry &a, const SocketEntry &b) {
              return a.inode < b.inode;
            });

  return result.to_array();
}

// Check if socket is on loopback (127.x.x.x or ::1)
static bool is_loopback_socket(const SocketEntry &socket) {
  // IPv4: Check if either end is 127.x.x.x
  if (socket.protocol == eSocketProtocol_TCP ||
      socket.protocol == eSocketProtocol_UDP) {
    const uint32_t local_ip = socket.local_ip;
    const uint32_t remote_ip = socket.remote_ip;
    // 127.0.0.0/8 in network byte order (little-endian x86): first byte == 127
    if ((local_ip & 0xff) == 127 || (remote_ip & 0xff) == 127) {
      return true;
    }
  }

  // IPv6: Check if either end is ::1 or ::ffff:127.x.x.x (IPv4-mapped loopback)
  if (socket.protocol == eSocketProtocol_TCP6 ||
      socket.protocol == eSocketProtocol_UDP6) {
    static constexpr uint8_t ipv6_loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                                  0, 0, 0, 0, 0, 0, 0, 1};
    static constexpr uint8_t ipv4_mapped_prefix[12] = {0, 0, 0, 0, 0,    0,
                                                       0, 0, 0, 0, 0xff, 0xff};

    // Pure IPv6 loopback (::1)
    if (memcmp(socket.local_ip6, ipv6_loopback, 16) == 0 ||
        memcmp(socket.remote_ip6, ipv6_loopback, 16) == 0) {
      return true;
    }

    // IPv4-mapped loopback (::ffff:127.x.x.x)
    if (memcmp(socket.local_ip6, ipv4_mapped_prefix, 12) == 0 &&
        socket.local_ip6[12] == 127) {
      return true;
    }
    if (memcmp(socket.remote_ip6, ipv4_mapped_prefix, 12) == 0 &&
        socket.remote_ip6[12] == 127) {
      return true;
    }
  }

  return false;
}

// Read socket inodes owned by a process from /proc/[pid]/fd/
static void read_process_socket_inodes(const int pid,
                                       GrowingArray<unsigned long> &out,
                                       BumpArena &arena) {
  char fd_path[64];
  snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd", pid);

  DIR *fd_dir = opendir(fd_path);
  if (!fd_dir) return;

  size_t wasted = 0;
  char link_buf[128];
  while (dirent *entry = readdir(fd_dir)) {
    if (entry->d_type != DT_LNK) continue;

    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", fd_path, entry->d_name);

    const ssize_t link_len =
        readlink(full_path, link_buf, sizeof(link_buf) - 1);
    if (link_len <= 0) continue;
    link_buf[link_len] = '\0';

    // Check for socket:[inode] pattern
    if (strncmp(link_buf, "socket:[", 8) == 0) {
      const unsigned long inode = strtoul(link_buf + 8, nullptr, 10);
      if (inode > 0) {
        *out.emplace_back(arena, wasted) = inode;
      }
    }
  }
  closedir(fd_dir);
}

// Read stat for a thread (or process) given explicit paths
static bool read_thread_stat(const int tid, const char *stat_path,
                             const char *statm_path, const char *comm_path,
                             BumpArena &arena, ProcessStat *out) {
  ProcessStat &stat = *out;
  stat.pid = tid;
  stat.comm = "";
  stat.io_read_bytes = 0;
  stat.io_write_bytes = 0;
  stat.net_recv_bytes = 0;
  stat.net_send_bytes = 0;

  FILE *stat_file = fopen(stat_path, "r");
  FILE *statm_file = fopen(statm_path, "r");
  FILE *comm_file = fopen(comm_path, "r");
  if (!stat_file || !statm_file || !comm_file) {
    if (stat_file) fclose(stat_file);
    if (statm_file) fclose(statm_file);
    if (comm_file) fclose(comm_file);
    return false;
  }

  char stat_buf[512];
  char statm_buf[128];

  if (!fgets(stat_buf, sizeof(stat_buf), stat_file)) {
    fclose(comm_file);
    fclose(statm_file);
    fclose(stat_file);
    return false;
  }
  if (!fgets(statm_buf, sizeof(statm_buf), statm_file)) {
    fclose(comm_file);
    fclose(statm_file);
    fclose(stat_file);
    return false;
  }
  char comm_buf[64];
  if (fgets(comm_buf, sizeof(comm_buf), comm_file)) {
    size_t len = strlen(comm_buf);
    if (len > 0 && comm_buf[len - 1] == '\n') {
      --len;
    }
    stat.comm = arena.alloc_string_copy(comm_buf, len);
  }

  fclose(comm_file);
  fclose(statm_file);
  fclose(stat_file);

  char *after_comm = strrchr(stat_buf, ')');
  if (!after_comm) {
    return false;
  }

  sscanf(after_comm + 1,
         " %c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d "
         "%*d %*d %ld %*d %*u %lu",
         &stat.state, &stat.ppid, &stat.utime, &stat.stime, &stat.num_threads,
         &stat.vsize);

  sscanf(statm_buf, "%*u %lu", &stat.statm_resident);

  return true;
}

static bool read_process(const int pid, BumpArena &arena, ProcessStat *out) {
  constexpr size_t PATH_BUF_SIZE = 64;

  char stat_filename[PATH_BUF_SIZE];
  snprintf(stat_filename, PATH_BUF_SIZE, "/proc/%d/stat", pid);

  char statm_filename[PATH_BUF_SIZE];
  snprintf(statm_filename, PATH_BUF_SIZE, "/proc/%d/statm", pid);

  char comm_filename[PATH_BUF_SIZE];
  snprintf(comm_filename, PATH_BUF_SIZE, "/proc/%d/comm", pid);

  char io_filename[PATH_BUF_SIZE];
  snprintf(io_filename, PATH_BUF_SIZE, "/proc/%d/io", pid);

  ProcessStat &stat = *out;
  stat.pid = pid;
  stat.comm = "";
  stat.cmdline = "";
  stat.io_read_bytes = 0;
  stat.io_write_bytes = 0;
  stat.net_recv_bytes = 0;
  stat.net_send_bytes = 0;

  FILE *stat_file = fopen(stat_filename, "r");
  FILE *statm_file = fopen(statm_filename, "r");
  FILE *comm_file = fopen(comm_filename, "r");
  if (!stat_file || !statm_file || !comm_file) {
    if (stat_file) fclose(stat_file);
    if (statm_file) fclose(statm_file);
    if (comm_file) fclose(comm_file);
    return false;
  }

  // Read all files into buffers
  char stat_buf[512];
  char statm_buf[128];

  if (!fgets(stat_buf, sizeof(stat_buf), stat_file)) {
    fclose(comm_file);
    fclose(statm_file);
    fclose(stat_file);
    return false;
  }
  if (!fgets(statm_buf, sizeof(statm_buf), statm_file)) {
    fclose(comm_file);
    fclose(statm_file);
    fclose(stat_file);
    return false;
  }
  char comm_buf[64];
  if (fgets(comm_buf, sizeof(comm_buf), comm_file)) {
    // Strip trailing newline
    size_t len = strlen(comm_buf);
    if (len > 0 && comm_buf[len - 1] == '\n') {
      --len;
    }
    stat.comm = arena.alloc_string_copy(comm_buf, len);
  }

  fclose(comm_file);
  fclose(statm_file);
  fclose(stat_file);

  // Find last ')' - comm can contain unbalanced parens
  const char *after_comm = strrchr(stat_buf, ')');
  if (!after_comm) {
    return false;
  }

  sscanf(after_comm + 1,
         " %c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d "
         "%*d %*d %ld %*d %*u %lu",
         &stat.state, &stat.ppid, &stat.utime, &stat.stime, &stat.num_threads,
         &stat.vsize);

  sscanf(statm_buf, "%*u %lu", &stat.statm_resident);

  // Read /proc/[pid]/io (may fail due to permissions, that's OK)
  FILE *io_file = fopen(io_filename, "r");
  if (io_file) {
    char io_line[128];
    while (fgets(io_line, sizeof(io_line), io_file)) {
      char key[32];
      ulonglong value;
      if (sscanf(io_line, "%31[^:]: %llu", key, &value) == 2) {
        if (strcmp(key, "read_bytes") == 0) {
          stat.io_read_bytes = value;
        } else if (strcmp(key, "write_bytes") == 0) {
          stat.io_write_bytes = value;
        }
      }
    }
    fclose(io_file);
  }

  // Read /proc/[pid]/cmdline (may fail or be empty, that's OK)
  char cmdline_filename[PATH_BUF_SIZE];
  snprintf(cmdline_filename, PATH_BUF_SIZE, "/proc/%d/cmdline", pid);
  FILE *cmdline_file = fopen(cmdline_filename, "r");
  if (cmdline_file) {
    char cmdline_buf[4096];
    const size_t nread =
        fread(cmdline_buf, 1, sizeof(cmdline_buf) - 1, cmdline_file);
    fclose(cmdline_file);
    if (nread > 0) {
      // Replace null bytes between args with spaces
      for (size_t i = 0; i < nread; ++i) {
        if (cmdline_buf[i] == '\0') cmdline_buf[i] = ' ';
      }
      // Trim trailing space (was the final null)
      size_t len = nread;
      while (len > 0 && cmdline_buf[len - 1] == ' ') --len;
      stat.cmdline = arena.alloc_string_copy(cmdline_buf, len);
    }
  }

  return true;
}

static Array<ProcessStat> read_all_processes(BumpArena &result_arena) {
  ZoneScoped;
  DIR *proc_dir = opendir("/proc");
  if (!proc_dir) {
    printf("Couldn't get a process list");
    return {};
  }

  LinkedList<long> pids = {};
  while (true) {
    const dirent *dir = readdir(proc_dir);
    if (!dir) {
      break;
    }

    const char *name = dir->d_name;
    char *str_end = nullptr;
    const long parsed_pid = strtol(name, &str_end, 10);
    if (parsed_pid == 0 || parsed_pid == LONG_MAX || parsed_pid == LONG_MIN) {
      continue;
    }
    *pids.emplace_front(result_arena) = parsed_pid;
  }

  Array<ProcessStat> result =
      Array<ProcessStat>::create(result_arena, pids.size);
  const LinkedNode<long> *it = pids.head;
  ProcessStat *it_result = result.data;
  while (it) {
    if (read_process(it->value, result_arena, it_result)) {
      ++it_result;
    }
    it = it->next;
  }
  result.size = it_result - result.data;

  closedir(proc_dir);

  std::sort(result.data, result.data + result.size,
            [](const ProcessStat &left, const ProcessStat &right) {
              return left.pid < right.pid;
            });

  // Query socket stats from netlink and distribute to processes
  const Array<SocketEntry> socket_stats = query_sockets_netlink(result_arena);
  if (socket_stats.size > 0) {
    GrowingArray<unsigned long> inodes = {};
    for (size_t i = 0; i < result.size; ++i) {
      ProcessStat &stat = result.data[i];
      inodes.shrink_to(0);
      read_process_socket_inodes(stat.pid, inodes, result_arena);

      ulonglong total_recv = 0;
      ulonglong total_send = 0;
      for (size_t j = 0; j < inodes.size(); ++j) {
        const unsigned long inode = inodes.data()[j];
        const size_t found_inode = bin_search_exact(
            socket_stats.size,
            [&socket_stats](const size_t mid) {
              return socket_stats.data[mid].inode;
            },
            inode);
        if (found_inode < socket_stats.size) {
          const SocketEntry &socket = socket_stats.data[found_inode];
          // Skip loopback sockets to match /proc/net/dev behavior
          if (is_loopback_socket(socket)) {
            continue;
          }
          total_recv += socket.bytes_received;
          total_send += socket.bytes_sent;
        }
      }
      stat.net_recv_bytes = total_recv;
      stat.net_send_bytes = total_send;
    }
  }

  return result;
}

// Reads /proc/stat for system-wide CPU stats
// Returns array where [0] = total, [1..n] = per-core
static Array<CpuCoreStat> read_cpu_stats(BumpArena &arena) {
  ZoneScoped;
  FILE *stat_file = fopen("/proc/stat", "r");
  if (!stat_file) {
    return {};
  }

  // Count CPU lines first (total + per-core)
  int num_cpus = 0;
  char line[256];
  while (fgets(line, sizeof(line), stat_file)) {
    if (strncmp(line, "cpu", 3) == 0 &&
        (line[3] == ' ' || (line[3] >= '0' && line[3] <= '9'))) {
      ++num_cpus;
    } else if (num_cpus > 0) {
      break; // CPU lines are at the top, stop when we hit non-CPU lines
    }
  }

  rewind(stat_file);

  Array<CpuCoreStat> result = Array<CpuCoreStat>::create(arena, num_cpus);
  int idx = 0;
  while (fgets(line, sizeof(line), stat_file) && idx < num_cpus) {
    if (strncmp(line, "cpu", 3) != 0) {
      continue;
    }
    // Skip "cpu" or "cpuN" prefix
    char *p = line + 3;
    while (*p && *p != ' ')
      ++p;

    CpuCoreStat &stat = result.data[idx];
    sscanf(p, "%lu %lu %lu %lu %lu %lu %lu", &stat.user, &stat.nice,
           &stat.system, &stat.idle, &stat.iowait, &stat.irq, &stat.softirq);
    ++idx;
  }

  fclose(stat_file);
  return result;
}

// Reads /proc/diskstats for system-wide disk I/O stats
// Aggregates all block devices (skips partitions by looking at device naming)
static DiskIoStat read_disk_io_stats() {
  ZoneScoped;
  FILE *diskstats_file = fopen("/proc/diskstats", "r");
  if (!diskstats_file) {
    return {};
  }

  DiskIoStat result = {};
  char line[256];
  while (fgets(line, sizeof(line), diskstats_file)) {
    int major, minor;
    char device[64];
    ulonglong reads_completed, reads_merged, sectors_read, ms_reading;
    ulonglong writes_completed, writes_merged, sectors_written, ms_writing;

    int parsed =
        sscanf(line, "%d %d %63s %llu %llu %llu %llu %llu %llu %llu %llu",
               &major, &minor, device, &reads_completed, &reads_merged,
               &sectors_read, &ms_reading, &writes_completed, &writes_merged,
               &sectors_written, &ms_writing);
    if (parsed < 11) {
      continue;
    }

    // Skip partitions (devices ending with a digit after letters like sda1,
    // nvme0n1p1) Include: sda, sdb, nvme0n1, vda, etc. Skip: sda1, sda2,
    // nvme0n1p1, loop0, ram0, etc.
    size_t len = strlen(device);
    if (len == 0) continue;

    // Skip loop and ram devices
    if (strncmp(device, "loop", 4) == 0 || strncmp(device, "ram", 3) == 0) {
      continue;
    }

    // For nvme devices: include nvme0n1, skip nvme0n1p1
    // For sd/vd devices: include sda, skip sda1
    bool is_partition = false;
    if (strncmp(device, "nvme", 4) == 0) {
      // NVMe partitions have 'p' followed by digit
      const char *p = strrchr(device, 'p');
      if (p && p > device + 4 && p[1] >= '0' && p[1] <= '9') {
        is_partition = true;
      }
    } else {
      // Traditional devices: partitions end with digit
      char last = device[len - 1];
      if (last >= '0' && last <= '9') {
        is_partition = true;
      }
    }

    if (is_partition) {
      continue;
    }

    result.sectors_read += sectors_read;
    result.sectors_written += sectors_written;
  }

  fclose(diskstats_file);
  return result;
}

// Reads /proc/net/dev for system-wide network I/O stats
// Aggregates all network interfaces except loopback
static NetIoStat read_net_io_stats() {
  ZoneScoped;
  FILE *netdev_file = fopen("/proc/net/dev", "r");
  if (!netdev_file) {
    return {};
  }

  NetIoStat result = {};
  char line[512];
  // Skip first two header lines
  if (!fgets(line, sizeof(line), netdev_file) ||
      !fgets(line, sizeof(line), netdev_file)) {
    fclose(netdev_file);
    return {};
  }

  while (fgets(line, sizeof(line), netdev_file)) {
    char interface[64];
    ulonglong rx_bytes, rx_packets, rx_errs, rx_drop, rx_fifo, rx_frame;
    ulonglong rx_compressed, rx_multicast;
    ulonglong tx_bytes, tx_packets, tx_errs, tx_drop, tx_fifo, tx_colls;
    ulonglong tx_carrier, tx_compressed;

    // Format: "iface: rx_bytes rx_packets ... tx_bytes tx_packets ..."
    int parsed = sscanf(
        line,
        " %63[^:]: %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu "
        "%llu %llu %llu %llu",
        interface, &rx_bytes, &rx_packets, &rx_errs, &rx_drop, &rx_fifo,
        &rx_frame, &rx_compressed, &rx_multicast, &tx_bytes, &tx_packets,
        &tx_errs, &tx_drop, &tx_fifo, &tx_colls, &tx_carrier, &tx_compressed);
    if (parsed < 10) {
      continue;
    }

    // Skip loopback interface
    if (strncmp(interface, "lo", 2) == 0 && interface[2] == '\0') {
      continue;
    }

    result.bytes_received += rx_bytes;
    result.bytes_transmitted += tx_bytes;
  }

  fclose(netdev_file);
  return result;
}

// Reads /proc/meminfo for system-wide memory stats
// Values are in kB (as reported by /proc/meminfo)
// Read all threads for a process from /proc/[pid]/task/
static Array<ProcessStat> read_process_threads(const int pid,
                                               BumpArena &arena) {
  ZoneScoped;
  char task_path[64];
  snprintf(task_path, sizeof(task_path), "/proc/%d/task", pid);

  DIR *task_dir = opendir(task_path);
  if (!task_dir) {
    return {};
  }

  LinkedList<int> tids = {};
  while (dirent *entry = readdir(task_dir)) {
    if (entry->d_type != DT_DIR) continue;

    char *end = nullptr;
    long tid = strtol(entry->d_name, &end, 10);
    if (tid <= 0 || *end != '\0') continue;

    *(tids.emplace_front(arena)) = static_cast<int>(tid);
  }
  closedir(task_dir);

  Array<ProcessStat> result = Array<ProcessStat>::create(arena, tids.size);
  const LinkedNode<int> *it = tids.head;
  ProcessStat *it_result = result.data;

  while (it) {
    const int tid = it->value;

    char stat_path[128];
    char statm_path[128];
    char comm_path[128];
    snprintf(stat_path, sizeof(stat_path), "/proc/%d/task/%d/stat", pid, tid);
    snprintf(statm_path, sizeof(statm_path), "/proc/%d/statm",
             pid); // statm is shared across threads
    snprintf(comm_path, sizeof(comm_path), "/proc/%d/task/%d/comm", pid, tid);

    if (read_thread_stat(tid, stat_path, statm_path, comm_path, arena,
                         it_result)) {
      ++it_result;
    }
    it = it->next;
  }
  result.size = it_result - result.data;

  // Sort by TID
  std::sort(
      result.data, result.data + result.size,
      [](const ProcessStat &a, const ProcessStat &b) { return a.pid < b.pid; });

  return result;
}

// Read threads for all watched PIDs using the gathering thread's local array
static Array<ThreadSnapshot>
read_watched_threads(const GrowingArray<int> &watched_pids, BumpArena &arena) {
  ZoneScoped;
  if (watched_pids.size() == 0) {
    return {};
  }

  Array<ThreadSnapshot> result =
      Array<ThreadSnapshot>::create(arena, watched_pids.size());
  for (size_t i = 0; i < watched_pids.size(); ++i) {
    result.data[i].pid = watched_pids.data()[i];
    result.data[i].threads = read_process_threads(watched_pids.data()[i], arena);
  }

  return result;
}

static MemInfo read_mem_info() {
  ZoneScoped;
  FILE *meminfo_file = fopen("/proc/meminfo", "r");
  if (!meminfo_file) {
    return {};
  }

  MemInfo result = {};
  char line[256];
  while (fgets(line, sizeof(line), meminfo_file)) {
    char key[64];
    ulong value;
    // Format: "FieldName:       value kB"
    if (sscanf(line, "%63[^:]: %lu kB", key, &value) == 2) {
      if (strcmp(key, "MemTotal") == 0) {
        result.mem_total = value;
      } else if (strcmp(key, "MemFree") == 0) {
        result.mem_free = value;
      } else if (strcmp(key, "MemAvailable") == 0) {
        result.mem_available = value;
      } else if (strcmp(key, "Buffers") == 0) {
        result.buffers = value;
      } else if (strcmp(key, "Cached") == 0) {
        result.cached = value;
      } else if (strcmp(key, "SwapTotal") == 0) {
        result.swap_total = value;
      } else if (strcmp(key, "SwapFree") == 0) {
        result.swap_free = value;
      }
    }
  }

  fclose(meminfo_file);
  return result;
}

void gather(GatheringState &state, Sync &sync) {
  const float period_secs = sync.update_period.load();
  {
    std::unique_lock<std::mutex> lock(sync.quit_mutex);
    if (period_secs <= 0.0f) {
      // Paused: wait until quit or period changes
      sync.quit_cv.wait(lock, [&sync] {
        return sync.quit.load() || sync.update_period.load() > 0.0f;
      });
    } else {
      sync.quit_cv.wait_until(lock, state.last_update + Seconds{period_secs});
    }
  }
  if (sync.quit.load()) {
    return;
  }

  // Drain watch/unwatch queues to update local watched PIDs
  int pid = 0;
  while (sync.thread_watch_queue.pop(pid)) {
    // Check if already watched
    bool found = false;
    for (size_t i = 0; i < state.watched_pids.size(); ++i) {
      if (state.watched_pids.data()[i] == pid) {
        found = true;
        break;
      }
    }
    if (!found) {
      *state.watched_pids.emplace_back(state.watched_pids_arena) = pid;
    }
  }
  while (sync.thread_unwatch_queue.pop(pid)) {
    for (size_t i = 0; i < state.watched_pids.size(); ++i) {
      if (state.watched_pids.data()[i] == pid) {
        // Swap with last element and shrink
        state.watched_pids.data()[i] =
            state.watched_pids.data()[state.watched_pids.size() - 1];
        state.watched_pids.shrink_to(state.watched_pids.size() - 1);
        break;
      }
    }
  }

  ZoneScoped;
  BumpArena arena = BumpArena::create();
  const auto process_stats = read_all_processes(arena);
  const auto cpu_stats = read_cpu_stats(arena);
  const auto mem_info = read_mem_info();
  const auto disk_io_stats = read_disk_io_stats();
  const auto net_io_stats = read_net_io_stats();
  const auto thread_snapshots = read_watched_threads(state.watched_pids, arena);

  state.last_update = SteadyClock::now();
  const SystemTimePoint system_now = SystemClock::now();
  const bool pushed = sync.update_queue.push(UpdateSnapshot{
      arena, process_stats, cpu_stats, mem_info, disk_io_stats, net_io_stats,
      thread_snapshots, state.last_update, system_now});
  if (!pushed) {
    arena.destroy();
  }
}
