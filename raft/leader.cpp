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
#include "common.hpp"
#include <mutex>

constexpr std::string_view usage =
    "usage: ./leader <ip_follower_1> <port_1> <ip_follower_2> <port_2>";

// NOLINTNEXTLINE(cert-err58-cpp, concurrency-mt-unsafe,
// cppcoreguidelines-avoid-non-const-global-variables)
// hostent *hostip = gethostbyname("localhost");

int reply_socket = -1;
// how many pending connections the queue will hold?
constexpr int backlog = 1024;

struct state {
  explicit state(int _counter, int _cmt_idx)
      : counter(_counter), cmt_idx(_cmt_idx){ last_cmt_node = -1;};
  int counter;
  int cmt_idx;
  int last_cmt_node;
};


static void
gen_and_send_request( std::unordered_map<int, connection>& cluster_info, int req_id) {
  for (auto& connection : cluster_info) {
    auto req_size = sizeof(int) + sizeof(int);
    int node_id = connection.first; //dst node
    // TODO: also send the leader node
    std::unique_ptr<char[]> tmp = std::make_unique<char[]>(req_size+length_size_field);
    std::unique_ptr<char[]> tmp2 = std::make_unique<char[]>(req_size+length_size_field);
    ::memcpy(tmp.get(), &req_id, sizeof(int));
    ::memcpy(tmp.get()+sizeof(int), &node_id, sizeof(int));
    construct_message(tmp2.get(), tmp.get(), req_size);
    int send_fd = connection.second.sending_socket;
    sent_request(tmp2.get(), sizeof(uint64_t)+length_size_field, send_fd); 
  }
  sleep(synthetic_delay_in_s);
}

static void sender(std::unordered_map<int, connection> cluster_info) {
  for (auto i = 0ULL; i < nb_requests; i++) {
    gen_and_send_request(cluster_info, i);
  }
}
template <typename Value>
std::ostream& operator<<(std::ostream& os, const std::vector<Value>& vec) {
    os << "[ ";
    for (auto& elem :vec) {
      os << elem << " ";
    }
    os << "]\n";
}

template <typename Key, typename Value>
std::ostream& operator<<(std::ostream& os, const std::unordered_map<Key, Value>& map) {
    os << "{";
    bool first = true;
    for (const auto& pair : map) {
        if (!first) {
            os << ", ";
        }
        os << pair.first << ": " << pair.second;
        first = false;
    }
    os << "}";
    return os;
}

static void
cleanup_log(std::unordered_map<int, std::vector<int>> &replication_log,
            int &last_cleanup, state& s) {
  auto &starting_cmt = last_cleanup;
  for (;;) {
  fmt::print("[{}] for last_cleanup={}\n", __func__, last_cleanup);
  
    if (replication_log.find(starting_cmt) == replication_log.end()) {
      std::cout << __PRETTY_FUNCTION__ << " " << replication_log << "\n";
      return;
    }
    if (replication_log[starting_cmt].size() == kNodesSize) {
      replication_log.erase(starting_cmt);
      starting_cmt++;
    } 
    else if (starting_cmt <= s.cmt_idx) {
      replication_log.erase(starting_cmt);
      starting_cmt++;
    }
    else {
      fmt::print("[{}] replication_log[{}].size()={}\n", __func__, starting_cmt, replication_log.size());
      std::cout << __PRETTY_FUNCTION__ << " " << replication_log << "\n";
      return;
    }
  }
}

static void iterate_log_and_commit(
    std::unordered_map<int, std::vector<int>> &replication_log, state &s) {
  auto next_cmt = s.cmt_idx + 1;
  auto& prev_node = s.last_cmt_node;
  for (;;) {
    if (replication_log.find(next_cmt) == replication_log.end()) {
      s.cmt_idx = next_cmt - 1;
      return;
    }
    if (replication_log[next_cmt].size() == kNodesSize) {
      fmt::print("[{}] committed for cmt={}\n", __func__, next_cmt);

      next_cmt++;
      continue;
    }
    if ((prev_node == -1) || (prev_node == replication_log[next_cmt][0])) {
      prev_node = replication_log[next_cmt][0];
      fmt::print("[{}] committed for cmt={}\n", __func__, next_cmt);
      next_cmt++;
    } else {
      s.cmt_idx = next_cmt - 1;
      return;
    }
  }
}

std::mutex mtx;
static void receiver(std::unordered_map<int, connection> cluster_info, int follower_id) {
  std::unordered_map<int, std::vector<int>> replication_log;
  int last_cleanup = 0;
  state s(-1, -1);
  bool check_for_cleanup = false;
  int processed_reqs = 0;
  connection& conn = cluster_info[follower_id];
  int recv_fd = conn.listening_socket;
  for (;;) {
    if ((s.cmt_idx+1) == nb_requests) {
          std::unique_lock<std::mutex> l(mtx);

            cleanup_log(replication_log, last_cleanup, s);

      fmt::print("[{}] cmt_idx={} replication_log={}\n", __func__, s.cmt_idx, replication_log.size());

      return;
    }
  
    auto [bytecount, buffer] = secure_recv(recv_fd);
    if (static_cast<int>(bytecount) <= 0) {
      // TODO: do some error handling here
      fmt::print("[{}] error\n", __func__);
      fmt::print("[{}] cmt_idx={} replication_log={}\n", __func__, s.cmt_idx, replication_log.size());
      return;

    }
    int ack_id, node_id;
    auto extract = [&]() {
      ack_id = -1;
      node_id = -1;
      ::memcpy(&ack_id, buffer.get(), sizeof(ack_id));
      ::memcpy(&node_id, buffer.get() + sizeof(ack_id), sizeof(node_id));
    };
    extract();
    fmt::print("[{}] bytecount={} req_id/cmt_idx={} node_id={}\n", __func__, bytecount, ack_id, node_id);

    std::unique_lock<std::mutex> l(mtx);
    if ((ack_id > s.cmt_idx) &&
        (replication_log.find(ack_id) == replication_log.end())) {
      replication_log.insert({ack_id, std::vector<int>{}});
      replication_log[ack_id].push_back(node_id);
    } else {
      replication_log[ack_id].push_back(node_id);
      // cleanup_log(replication_log, last_cleanup);
      check_for_cleanup = true;
    }
    
    iterate_log_and_commit(replication_log, s);
    if (kNodesSize == 2 || check_for_cleanup) {
            cleanup_log(replication_log, last_cleanup, s);
      check_for_cleanup = false;
    }
    
  }
}

void create_communication_pair(int listening_socket) {
  auto *he = hostip;
  fmt::print("{} ...\n", __PRETTY_FUNCTION__);
  // TODO: port = take the string
  int port = 18001;

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
  reply_socket = sockfd;
}

static int create_receiver_connection() {
  int port = 18001;
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
    fmt::print("socket\n");
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
    fmt::print("leader cannot bind to port={}\n", port);
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);
  }

  if (listen(sockfd, backlog) == -1) {
    fmt::print("listen\n");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);
  }

  socklen_t sin_size = sizeof(sockaddr_in);
  fmt::print("waiting for new connections ..\n");
  sockaddr_in their_addr{};

  auto new_fd = accept4(sockfd, reinterpret_cast<sockaddr *>(&their_addr),
                        &sin_size, SOCK_CLOEXEC);
  if (new_fd == -1) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    fmt::print("accept() failed ..{}\n", std::strerror(errno));
    exit(1);
  }

  fmt::print("received request from client: {}:{}\n",
             inet_ntoa(their_addr.sin_addr), // NOLINT(concurrency-mt-unsafe)
             port);
  fcntl(new_fd, F_SETFL, O_NONBLOCK);
  //create_communication_pair(new_fd);
  return new_fd;
}


std::tuple<int, int> client(int port, int follower_id) {
  hostip = gethostbyname("localhost");
  auto sending_fd = -1;
  auto fd = connect_to_the_server(port, "localhost", sending_fd, follower_id);

  fmt::print("[{}] connect_to_the_server sending_fd={} fd={}\n", __func__,
             sending_fd, fd);

  return {sending_fd, fd};
}



int main(void) {
  auto follower_1_port = 18000;
  auto [sending_socket_f1, listening_socket_f1] = client(follower_1_port, follower_1_id);
  fmt::print("{} follower_1={} at port={}\n", __func__, sending_socket_f1, follower_1_port);

  std::unordered_map<int, connection> cluster_info;
  cluster_info.insert(std::make_pair(1, connection_t(listening_socket_f1, sending_socket_f1)));

  fmt::print("[{}] connection w/ follower_1 initialized\n", __func__);
  auto [sending_socket_f2, listening_socket_f2] = client(follower_2_port, follower_2_id);
  fmt::print("{} follower_2={} at port={}\n", __func__, sending_socket_f2, follower_2_port);

  cluster_info.insert(std::make_pair(2, connection_t(listening_socket_f2, sending_socket_f2)));

  fmt::print("[{}] connection w/ follower_2 initialized\n", __func__);

  std::vector<std::thread> threads;
  threads.emplace_back(sender, cluster_info);
  threads.emplace_back(receiver, cluster_info, follower_1_id);
  threads.emplace_back(receiver, cluster_info, follower_2_id);

  threads[0].join();
  threads[1].join();
  threads[2].join();
  return 0;
}
