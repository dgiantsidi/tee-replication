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

static int send_ack_to_leader(const state &s, const int send_fd,
                              const int cur_node_id) {
  auto req_size = sizeof(int) + sizeof(int);
  int node_id = cur_node_id;
  std::unique_ptr<uint8_t[]> tmp_data = std::make_unique<uint8_t[]>(req_size);
  ::memcpy(tmp_data.get(), &(s.cmt_idx), sizeof(int));
  ::memcpy(tmp_data.get() + sizeof(int), &node_id, sizeof(int));
    fmt::print("[{}] for req_id={}\n", __func__, s.cmt_idx);

  auto [v_data, size] =
      authenticator_hmac_t::generate_attested_msg(reinterpret_cast<char*>(tmp_data.get()), req_size);
  fmt::print("[{}] for req_id={}mm2\n", __func__, s.cmt_idx);

  std::unique_ptr<char[]> tmp =
      std::make_unique<char[]>(size + length_size_field);
  const uint8_t *ptr_to_data = v_data.data() + _hmac_size;
    fmt::print("[{}] for req_id={} 3\n", __func__, s.cmt_idx);

  construct_message(tmp.get(), reinterpret_cast<const char *>(ptr_to_data),
                    size-_hmac_size);
    fmt::print("[{}] for req_id={} 4\n", __func__, s.cmt_idx);

  sent_request(tmp.get(), size + length_size_field, send_fd);
      fmt::print("[{}] for req_id={} 4\n", __func__, s.cmt_idx);

  return 1;
}

static void receiver(std::unordered_map<int, connection> cluster_info,
                     int cur_node_id) {

  connection &conn_head =
      cluster_info[head_id]; // middle is only connected to the leader
  connection &conn_middle =
      cluster_info[middle_id]; // middle is only connected to the tail
  int recv_fd = conn_middle.listening_socket;
  int send_fd = conn_head.sending_socket;
  fmt::print("{}-->\n", __func__);
  state s(-1, -1);

  for (;;) {

    auto [bytecount, buffer] = secure_recv(recv_fd);
    authenticator_hmac_t::print_buf(reinterpret_cast<char *>(buffer.get()),
                                    bytecount, __func__);
    if (static_cast<int>(bytecount) <= 0) {
      // TODO: do some error handling here
      fmt::print("[{}] error, s.cmt_idx={}\n", __func__, s.cmt_idx);
    }
    fmt::print("[{}] bytecount={}\n", __func__, bytecount);
    char *ptr_to_data =
        authenticator_hmac_t::verify_attested_msg(buffer.get(), bytecount);
    if (!ptr_to_data) {
      fmt::print("[{}] error authenticating the message\n", __func__);
    }
    
    int req_id, node_id;
    auto extract = [&]() {
      req_id = -1;
      node_id = -1;
     // ::memcpy(&req_id, ptr_to_data, sizeof(req_id));
    //  ::memcpy(&node_id, ptr_to_data + sizeof(req_id), sizeof(node_id));
    };
    extract();

    if (req_id == (s.cmt_idx + 1)) {
      s.cmt_idx++;
    } else {
      fmt::print("{} error in req_id={} (expected s.cmt_idx+1={})\n", __func__,
                 req_id, (s.cmt_idx + 1));
    }
    //fmt::print("{} req_id={}\n", __func__, req_id);
    send_ack_to_leader(s, send_fd, cur_node_id);
  }
}

int create_communication_pair(int node_id) {
  auto *he = hostip;

  int port = client_base_addr + node_id;

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
  // inet_aton("131.159.102.8", &their_addr.sin_addr);
  memset(&(their_addr.sin_zero), 0, sizeof(their_addr.sin_zero));

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
  return sockfd;
}

static std::tuple<int, int> create_receiver_connection(int follower_1_port,
                                                       int node_id) {
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

  auto send_fd = create_communication_pair(node_id);
  return {recv_fd, send_fd};
}

using PortId = int;
using NodeId = int;
static std::tuple<PortId, NodeId> parse_args(int args, char *argv[]) {
  int port, node_id;
  if (args == 3) {
    fmt::print("./{} node_id={} port={}\n", __func__, argv[1], argv[2]);
    node_id = std::atoi(argv[1]);
    port = std::atoi(argv[2]);
  } else {
    fmt::print("[{}] usage: ./program <node_id> <port>\n", __func__);
    exit(1);
  }
  return {port, node_id};
}

static int connect_to_the_server_one_sided_connection(int port, char const * /*hostname*/,
                                 int server_id) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  hostent *he = gethostbyname("localhost");
  fmt::print("{} localhost at port={}\n", __func__, port);
  hostip;

  auto sockfd = 0;
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    fmt::print("socket err={}\n", std::strerror(errno));
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);
  }

  // connector.s address information
  sockaddr_in their_addr{};
  their_addr.sin_family = AF_INET;
  their_addr.sin_port = htons(port);
  their_addr.sin_addr = *(reinterpret_cast<in_addr *>(he->h_addr));
  memset(&(their_addr.sin_zero), 0, sizeof(their_addr.sin_zero));

  if (connect(sockfd, reinterpret_cast<sockaddr *>(&their_addr),
              sizeof(struct sockaddr)) == -1) {
    fmt::print("connect issue err={}\n", std::strerror(errno));
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);
  }
  return sockfd;
                                 }

std::tuple<int, int> client(int port, int follower_id) {
  hostip = gethostbyname("localhost");
  auto sending_fd = -1;
  auto fd = connect_to_the_server(port, "localhost", sending_fd, follower_id);

  fmt::print("[{}] connect_to_the_server sending_fd={} fd={}\n", __func__,
             sending_fd, fd);

  return {sending_fd, fd};
}

int main(int args, char *argv[]) {
  hostip = gethostbyname("localhost");
  auto [port, node_id] = parse_args(args, argv);

  std::unordered_map<int, connection> cluster_info;
  auto [recv_fd, send_fd] = create_receiver_connection(port, node_id);
  cluster_info.insert(
      std::make_pair(middle_id, connection_t(recv_fd, send_fd)));
#if 1
  auto sending_socket_head =
      connect_to_the_server_one_sided_connection(head_port, "localhost",
                                 head_id);
  fmt::print("{} head={} at port={}\n", __func__, sending_socket_head,
             tail_port);

  cluster_info.insert(std::make_pair(
      head_id, connection_t(-1, sending_socket_head)));
#endif
  fmt::print(
      "{} connections initialized .. socket-to-send={} socket-to-receive={}\n",
      __func__, send_fd, recv_fd);
      
  std::vector<std::thread> threads;
  threads.emplace_back(receiver, cluster_info, node_id);

  threads[0].join();
  return 0;
}
