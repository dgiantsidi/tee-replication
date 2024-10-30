#include "common.hpp"
#include "config.h"
#include "net/shared.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fmt/format.h>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

int reply_socket = -1;
// how many pending connections the queue will hold?
constexpr int backlog = 1024;

struct state {
  explicit state(int _counter, int _cmt_idx)
      : counter(_counter), cmt_idx(_cmt_idx){};
  int counter;
  int cmt_idx;
};

static int send_new_cmt_idx(const state &s, const int send_fd,
                            const int cur_node_id) {
  static int send_cmt_idx = 0;
  auto req_size = sizeof(int) + sizeof(int);
  int node_id = cur_node_id;
  fmt::print("[{}] for req_id={}\n", __func__, s.cmt_idx);
  std::unique_ptr<char[]> tmp =
      std::make_unique<char[]>(req_size + length_size_field);
  std::unique_ptr<char[]> tmp2 =
      std::make_unique<char[]>(req_size + length_size_field);
  ::memcpy(tmp.get(), &(s.cmt_idx), sizeof(int));
  ::memcpy(tmp.get() + sizeof(int), &node_id, sizeof(int));
  construct_message(tmp2.get(), tmp.get(), req_size);

  sent_request(tmp2.get(), sizeof(uint64_t) + length_size_field, send_fd);
  send_cmt_idx = s.cmt_idx;
  return send_cmt_idx;
}

static void iterate_log_and_commit(
    std::unordered_map<int, std::vector<int>> &replication_log, state &s) {
  auto next_cmt = s.cmt_idx + 1;

  for (;;) {
    if (replication_log.find(next_cmt) == replication_log.end()) {
      s.cmt_idx = next_cmt - 1;
      return;
    }
    replication_log.erase(next_cmt);
    next_cmt++;
  }
}

static void receiver(std::unordered_map<int, connection> cluster_info,
                     int cur_node_id) {
  std::unordered_map<int, std::vector<int>> replication_log;
  state s(-1, -1);
  int last_cmt_ack_id = 0;
  int processed_reqs = 0;
  connection &conn = cluster_info[0];
  int recv_fd = conn.listening_socket;
  int send_fd = conn.sending_socket;
  for (;;) {
    if ((last_cmt_ack_id + 1) == nb_requests) {
      fmt::print("[{}] cmt_idx={} replication_log={}\n", __func__, s.cmt_idx,
                 replication_log.size());
      return;
    }
    auto [bytecount, buffer] = secure_recv(recv_fd);
    if (static_cast<int>(bytecount) <= 0) {
      // TODO: do some error handling here
      fmt::print("[{}] error\n", __func__);
      fmt::print("[{}] processed_reqs={}\n", __func__, processed_reqs);
    }
    fmt::print("[{}] bytecount={}\n", __func__, bytecount);
    int req_id, node_id;
    auto extract = [&]() {
      req_id = -1;
      node_id = -1;
      ::memcpy(&req_id, buffer.get(), sizeof(req_id));
      ::memcpy(&node_id, buffer.get() + sizeof(req_id), sizeof(node_id));
    };
    extract();
    fmt::print("[{}] bytecount={} req_id={} node_id={}\n", __func__, bytecount,
               req_id, node_id);

    if ((req_id > s.cmt_idx) &&
        (replication_log.find(req_id) == replication_log.end())) {
      replication_log.insert({req_id, std::vector<int>{}});
      replication_log[req_id].push_back(node_id);
    }
    iterate_log_and_commit(replication_log, s);
    processed_reqs = s.cmt_idx;
    last_cmt_ack_id = send_new_cmt_idx(s, send_fd, cur_node_id);
  }
}

int create_communication_pair(int listening_socket, int node_id) {
  auto *he = hostip;
  fmt::print("{}\n", __PRETTY_FUNCTION__);
  // TODO: port = take the string
  int port = client_base_addr + node_id;

  int sockfd = -1;
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    fmt::print("socket {}\n", std::strerror(errno));
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);
  }
  fmt::print("{} sockfd={}\n", __PRETTY_FUNCTION__, sockfd);

  // connector.s address information
  sockaddr_in their_addr{};
  their_addr.sin_family = AF_INET;
  their_addr.sin_port = htons(port);
  their_addr.sin_addr = *(reinterpret_cast<in_addr *>(he->h_addr));
  // inet_aton("131.159.102.8", &their_addr.sin_addr);
  memset(&(their_addr.sin_zero), 0, sizeof(their_addr.sin_zero));
  fmt::print("{} 3\n", __PRETTY_FUNCTION__);
  bool successful_connection = false;
  for (size_t retry = 0; retry < number_of_connect_attempts; retry++) {
    if (connect(sockfd, reinterpret_cast<sockaddr *>(&their_addr),
                sizeof(struct sockaddr)) == -1) {
      fmt::print("connect {}\n", std::strerror(errno));
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
  reply_socket = sockfd;
  return sockfd;
}

static std::tuple<int, int> create_receiver_connection(int follower_1_port,
                                                       int node_id) {
  fmt::print("{}\n", __PRETTY_FUNCTION__);
  // int port = 18000;
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
  fmt::print("{} waiting for new connections at port={}\n", __func__, port);

  sockaddr_in their_addr{};
  auto new_fd = accept4(sockfd, reinterpret_cast<sockaddr *>(&their_addr),
                        &sin_size, SOCK_CLOEXEC);
  if (new_fd == -1) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    fmt::print("accept() failed .. {}\n", std::strerror(errno));
    exit(1);
  }

  fmt::print("received request from client: {}:{} (fd={})\n",
             inet_ntoa(their_addr.sin_addr), // NOLINT(concurrency-mt-unsafe)
             port, new_fd);
  fcntl(new_fd, F_SETFL, O_NONBLOCK);

  auto fd = create_communication_pair(new_fd, node_id);
  return {new_fd, fd};
}

#if 0
int client(int port) {
  hostip = gethostbyname("localhost");
  auto sending_fd = -1;
  auto fd = connect_to_the_server(port, "localhost", sending_fd);

  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  // sleep(2);
  fmt::print("[{}] connect_to_the_server sending_fd={} fd={}\n", __func__,
             sending_fd, fd);

  return fd;
}
#endif

int main(int args, char *argv[]) {
  hostip = gethostbyname("localhost");
  int port;
  int node_id;
  if (args == 3) {
    fmt::print("./{} node_id={} port={}\n", __func__, argv[1], argv[2]);
    node_id = std::atoi(argv[1]);
    port = std::atoi(argv[2]);
  } else {
    fmt::print("[{}] usage: ./program <node_id> <port>\n");
    exit(1);
  }

  std::unordered_map<int, connection> cluster_info;
  auto [recv_fd, send_fd] = create_receiver_connection(port, node_id);
  cluster_info.insert(std::make_pair(0, connection_t(recv_fd, send_fd)));
  sleep(synthetic_delay_in_s);
  fmt::print(
      "{} connections initialized .. socket-to-send={} socket-to-receive={}\n",
      __func__, send_fd, recv_fd);
  std::vector<std::thread> threads;
  threads.emplace_back(receiver, cluster_info, node_id);

  threads[0].join();
  return 0;
}
