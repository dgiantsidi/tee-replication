#include "common.hpp"
#include "config.h"
#include "net/shared.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fmt/format.h>
#include <iostream>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <time.h>
#include <tuple>
#include <unistd.h>
#include <unordered_map>
#include <vector>

constexpr std::string_view usage =
    "usage: ./head <ip_follower_1> <port_1> <ip_follower_2> <port_2>";

// NOLINTNEXTLINE(cert-err58-cpp, concurrency-mt-unsafe,
// cppcoreguidelines-avoid-non-const-global-variables)
// hostent *hostip = gethostbyname("localhost");

static void
gen_and_send_request(std::unordered_map<int, connection> &cluster_info,
                     int req_id) {

  auto req_size = sizeof(int) + sizeof(int);
  int node_id = middle_id; // dst node

  // TODO: also send the leader node
  std::unique_ptr<char[]> tmp_data = std::make_unique<char[]>(req_size);
  ::memcpy(tmp_data.get(), &req_id, sizeof(int));
  ::memcpy(tmp_data.get() + sizeof(int), &node_id, sizeof(int));
  authenticator_hmac_t::print_buf(tmp_data.get(), req_size, __func__);

  auto [v_data, size] =
      authenticator_hmac_t::generate_attested_msg(tmp_data.get(), req_size);
  authenticator_hmac_t::print_buf(reinterpret_cast<char *>(v_data.data()), size,
                                  __func__);

  std::unique_ptr<char[]> tmp =
      std::make_unique<char[]>(size + length_size_field);

  const uint8_t *ptr_to_data = v_data.data();
  construct_message(tmp.get(), reinterpret_cast<const char *>(ptr_to_data),
                    size);
  authenticator_hmac_t::print_buf(tmp.get(), size + length_size_field,
                                  __func__);
  authenticator_hmac_t::print_buf(tmp.get() + length_size_field + _hmac_size,
                                  req_size, __func__);
  int send_fd = cluster_info[middle_id].sending_socket;
  sent_request(tmp.get(), size + length_size_field, send_fd);
  sleep(synthetic_delay_in_s);
}

static void sender(std::unordered_map<int, connection> cluster_info) {
  for (auto i = 0ULL; i < nb_requests; i++) {
    gen_and_send_request(cluster_info, i);
  }
}

static void
// cleanup_log(std::unordered_map<int, std::vector<int>> &replication_log,
//            int &last_cleanup, state &s)
cleanup_log(in_mem_log_t &raft_log, int &last_cleanup, state &s) {
  auto &starting_cmt = last_cleanup;
  for (;;) {
    // fmt::print("[{}] for last_cleanup={}\n", __func__, last_cleanup);

    if (!raft_log.has_key(starting_cmt)) {
      raft_log.print();
      return;
    }
    if (raft_log.size_at_key(starting_cmt) == kNodesSize) {
      raft_log.remove(starting_cmt);
      starting_cmt++;
    } else if (starting_cmt <= s.cmt_idx) {
      raft_log.remove(starting_cmt);
      starting_cmt++;
    } else {
      fmt::print("[{}] replication_log[{}].size()={}\n", __func__, starting_cmt,
                 raft_log.size());
      raft_log.print();
      return;
    }
  }
}

static void iterate_log_and_commit(in_mem_log_t &raft_log, state &s) {
  auto next_cmt = s.cmt_idx + 1;
  raft_log.print();
  auto &prev_node = s.last_cmt_node;
  for (;;) {
    if (!raft_log.has_key(next_cmt)) {
      s.cmt_idx = next_cmt - 1;
      return;
    }
    if (raft_log.size_at_key(next_cmt) == kNodesSize) {
      // fmt::print("[{}] committed for cmt={}\n", __func__, next_cmt);
      next_cmt++;
      continue;
    }
    if ((prev_node == -1) || (prev_node == raft_log.get_node_at(next_cmt))) {
      prev_node = raft_log.get_node_at(next_cmt);
#if 0
      fmt::print("[{}] committed for cmt={} w/ prev_node={}\n", __func__,
                 next_cmt, prev_node);
#endif
      next_cmt++;
    } else {
      s.cmt_idx = next_cmt - 1;
      return;
    }
  }
}

static void receiver_finale(std::unordered_map<int, connection> cluster_info,
                            int head_id) {

  state s(-1, -1);

  connection &conn = cluster_info[head_id];
  int recv_fd = conn.listening_socket;
  for (;;) {
    if ((s.cmt_idx + 1) == nb_requests) {
      fmt::print("[{}] cmt_idx={} finish experiment!\n", __func__, s.cmt_idx);
      return;
    }
    // fmt::print("{}@socket={}->cmt_idx={}\n", __func__, recv_fd, s.cmt_idx);

    auto [bytecount, buffer] = secure_recv(recv_fd);
    // fmt::print("{}->from socket={} received {} bytes\n", __func__, recv_fd,
    // bytecount);
    if (static_cast<int>(bytecount) <= 0) {
      // TODO: do some error handling here
      fmt::print("[{}] error\n", __func__);
      fmt::print("[{}] cmt_idx={}\n", __func__, s.cmt_idx);
      return;
    }
    char *ptr_to_data =
        authenticator_hmac_t::verify_attested_msg(buffer.get(), bytecount);
    if (!ptr_to_data) {
      fmt::print("[{}] error authenticating the message\n", __func__);
    }
    int ack_id, node_id;
    auto extract = [&]() {
      ack_id = -1;
      node_id = -1;
      ::memcpy(&ack_id, ptr_to_data, sizeof(ack_id));
      ::memcpy(&node_id, ptr_to_data + sizeof(ack_id), sizeof(node_id));
    };
    extract();

    s.cmt_idx = ack_id;
#if 0
    fmt::print("[{}] bytecount={} req_id/cmt_idx={} node_id={}\n", __func__,
               bytecount, ack_id, node_id);
#endif
  }
}

static void receiver(std::unordered_map<int, connection> cluster_info,
                     int follower_id) {
  std::unordered_map<int, std::vector<int>> replication_log;
  in_mem_log_t raft_log;
  int last_cleanup = 0;
  state s(-1, -1);

  connection &conn = cluster_info[follower_id];
  int recv_fd = conn.listening_socket;
  for (;;) {
    if ((s.cmt_idx + 1) == nb_requests) {

      cleanup_log(raft_log, last_cleanup, s);

      fmt::print("[{}] cmt_idx={} replication_log={}\n", __func__, s.cmt_idx,
                 raft_log.size());

      return;
    }

    auto [bytecount, buffer] = secure_recv(recv_fd);
    char *ptr_to_data =
        authenticator_hmac_t::verify_attested_msg(buffer.get(), bytecount);
    if (!ptr_to_data) {
      fmt::print("[{}] error authenticating the message\n", __func__);
    }
    if (static_cast<int>(bytecount) <= 0) {
      // TODO: do some error handling here
      fmt::print("[{}] error\n", __func__);
      fmt::print("[{}] cmt_idx={} replication_log={}\n", __func__, s.cmt_idx,
                 replication_log.size());
      return;
    }
    int ack_id, node_id;
    auto extract = [&]() {
      ack_id = -1;
      node_id = -1;
      ::memcpy(&ack_id, ptr_to_data, sizeof(ack_id));
      ::memcpy(&node_id, ptr_to_data + sizeof(ack_id), sizeof(node_id));
    };
    extract();
#if 0
    fmt::print("[{}] bytecount={} req_id/cmt_idx={} node_id={}\n", __func__,
               bytecount, ack_id, node_id);
#endif

    if ((ack_id > s.cmt_idx) && !raft_log.has_key(ack_id)) {
      raft_log.insert(ack_id, node_id);
    } else {
      raft_log.append(ack_id, node_id);
    }

    iterate_log_and_commit(raft_log, s);
    cleanup_log(raft_log, last_cleanup, s);
  }
}

int create_communication_pair(int listening_socket, const char* node_ip) {
  auto *he = hostip;
  fmt::print("{} ...\n", __PRETTY_FUNCTION__);
  // TODO: port = take the string
  int port = 18001; // this is specific to head?!!

  int sockfd = -1;
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    fmt::print("socket {}\n", std::strerror(errno));
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);
  }

  // connector.s address information
  sockaddr_in their_addr{};
  their_addr.sin_family = AF_INET;
  their_addr.sin_port = htons(port);
  their_addr.sin_addr = *(reinterpret_cast<in_addr *>(he->h_addr));
  inet_aton(node_ip, &their_addr.sin_addr);
  memset(&(their_addr.sin_zero), 0, sizeof(their_addr.sin_zero));

  bool successful_connection = false;
  for (size_t retry = 0; retry < number_of_connect_attempts; retry++) {
    if (connect(sockfd, reinterpret_cast<sockaddr *>(&their_addr),
                sizeof(struct sockaddr)) == -1) {
      //   	fmt::print("connect {}\n", std::strerror(errno));
      // NOLINTNEXTLINE(concurrency-mt-unsafe)
      sleep(1);
    } else {
      successful_connection = true;
      break;
    }
  }
  if (!successful_connection) {
    fmt::print("[{}] could not connect to client after {} attempts ..\n",
               __func__, number_of_connect_attempts);
    exit(1);
  }
  fmt::print("{} {}\n", listening_socket, sockfd);
  return sockfd;
}

static std::tuple<int, int>
create_receiver_connection(int follower_1_port, const char* ip, int node_id, const char* node_ip = nullptr,
                           bool biderectional = true) {
  int port = follower_1_port;

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
    fmt::print("socket err={}\n", std::strerror(errno));
    exit(1);
  }

  int ret = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &ret, sizeof(int)) == -1) {
    fmt::print("setsockopt\n");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);
  }

  sockaddr_in my_addr{};
  my_addr.sin_family = AF_INET;         // host byte order
  my_addr.sin_port = htons(port);       // short, network byte order
  my_addr.sin_addr.s_addr = INADDR_ANY; // automatically fill with my IP
  inet_aton(ip, &my_addr.sin_addr);
				
  memset(&(my_addr.sin_zero), 0,
         sizeof(my_addr.sin_zero)); // zero the rest of the struct

  if (bind(sockfd, reinterpret_cast<sockaddr *>(&my_addr), sizeof(sockaddr)) ==
      -1) {
    fmt::print("bind\n");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);
  }

  if (listen(sockfd, backlog) == -1) {
    fmt::print("listen\n");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);
  }

  socklen_t sin_size = sizeof(sockaddr_in);
  fmt::print("[{}] waiting for new connections at port={}\n", __func__, port);

  sockaddr_in their_addr{};
  auto recv_fd = accept4(sockfd, reinterpret_cast<sockaddr *>(&their_addr),
                         &sin_size, SOCK_CLOEXEC);
  if (recv_fd == -1) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    fmt::print("accept() failed .. {}\n", std::strerror(errno));
    exit(1);
  }

  fmt::print("received request from client: {}:{} (fd={})\n",
             inet_ntoa(their_addr.sin_addr), // NOLINT(concurrency-mt-unsafe)
             port, recv_fd);
  fcntl(recv_fd, F_SETFL, O_NONBLOCK);
  int send_fd;
  if (biderectional)
    send_fd = create_communication_pair(node_id, node_ip);
  else
    send_fd = -1;
  return {recv_fd, send_fd};
}

std::tuple<int, int> client(int port, int follower_id, const char* follower_ip, const char* my_ip) {
  hostip = gethostbyname("localhost");
  auto sending_fd = -1;
  auto fd = connect_to_the_server(port, follower_ip, sending_fd, follower_id, my_ip);

  fmt::print("[{}] connect_to_the_server sending_fd={} fd={}\n", __func__,
             sending_fd, fd);

  return {sending_fd, fd};
}

int main(void) {
  std::unordered_map<int, connection> cluster_info;

  auto [sending_socket_middle, listening_socket_middle] =
      client(middle_port, middle_id, middle_ip.c_str(), head_ip.c_str());

  fmt::print("{} middle={} at {}:{}\n", __func__, sending_socket_middle, middle_ip,
             middle_port);

  cluster_info.insert(std::make_pair(
      middle_id, connection_t(listening_socket_middle, sending_socket_middle)));

  fmt::print("[{}] connection w/ middle initialized\n", __func__);
#if 1

  auto [recv_fd, send_fd] =
      create_receiver_connection(head_port, head_ip.c_str(), head_id, nullptr, false);
  fmt::print("{} socket-to-tail={} at {}:{}\n", __func__, send_fd, head_ip,
             head_port);
  cluster_info.insert(std::make_pair(head_id, connection_t(recv_fd, -1)));
#endif

  fmt::print("[{}] connection w/ tail initialized\n", __func__);

  std::vector<std::thread> threads;
  threads.emplace_back(sender, cluster_info);
  threads.emplace_back(receiver_finale, cluster_info, head_id);
  //  threads.emplace_back(receiver, cluster_info, tail_id);

  threads[0].join();
  threads[1].join();
  //  threads[2].join();
  return 0;
}
