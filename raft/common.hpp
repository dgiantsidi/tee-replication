#include "crypto.hpp"
#include <fmt/printf.h>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <vector>

template <typename Value>
std::ostream &operator<<(std::ostream &os, const std::vector<Value> &vec) {
  os << "[ ";
  for (auto &elem : vec) {
    os << elem << " ";
  }
  os << "]";
}

template <typename Key, typename Value>
std::ostream &operator<<(std::ostream &os,
                         const std::unordered_map<Key, Value> &map) {
  os << "{";
  for (const auto &pair : map) {
    os << pair.first << ": " << pair.second;
    os << ",   ";
  }
  os << "}\n";
  return os;
}

struct connection_t {
  int listening_socket;
  int sending_socket;
  connection_t(int _listening_socket, int _sending_socket)
      : listening_socket(_listening_socket), sending_socket(_sending_socket){};
  connection_t() : listening_socket(-1), sending_socket(-1){};
  connection_t(const connection_t &other) {
    listening_socket = other.listening_socket;
    sending_socket = other.sending_socket;
  }
  connection_t(connection_t &&other) {
    listening_socket = other.listening_socket;
    sending_socket = other.sending_socket;
  }
  connection_t &operator=(const connection_t &other) {
    listening_socket = other.listening_socket;
    sending_socket = other.sending_socket;
    return *this;
  }
  connection_t &operator=(connection_t &&other) {
    listening_socket = other.listening_socket;
    sending_socket = other.sending_socket;
    return *this;
  }
};

using connection = connection_t;

struct state {
  explicit state(int _counter, int _cmt_idx)
      : counter(_counter), cmt_idx(_cmt_idx) {
    last_cmt_node = -1;
  };
  int counter;
  int cmt_idx;
  int last_cmt_node; // unused from followers
};

struct in_mem_log_t {
  in_mem_log_t() = default;
  std::unordered_map<int, std::vector<int>> replication_log;
  std::mutex mtx;
  in_mem_log_t(const in_mem_log_t &other) = delete;
  in_mem_log_t(in_mem_log_t &&other) = delete;

  bool has_key(int key) {
    std::unique_lock<std::mutex> tmp_log(mtx);
    return !(replication_log.find(key) == replication_log.end());
  }

  uint64_t size() {
    std::unique_lock<std::mutex> tmp_log(mtx);
    return reinterpret_cast<uint64_t>(replication_log.size());
  }

  uint64_t size_at_key(int key) {
    std::unique_lock<std::mutex> tmp_log(mtx);
    // Requirement: key must exist
    return reinterpret_cast<uint64_t>(replication_log[key].size());
  }

  int get_node_at(int key) {
    std::unique_lock<std::mutex> tmp_log(mtx);
    return replication_log[key][0];
  }

  void print() {
#if 0
    std::unique_lock<std::mutex> tmp_log(mtx);
    for (auto &pair : replication_log) {
      fmt::print("{} ", pair.first);
      for (auto &elem : pair.second) {
        fmt::print(" {}", elem);
      }
      fmt::print("\n");
    }
#endif

    // std::cout << __func__ << " " << replication_log << "\n";
  }

  void insert(int key, int value) {
    std::unique_lock<std::mutex> tmp_log(mtx);
    replication_log.insert({key, std::vector<int>{}});
    replication_log[key].push_back(value);
  }

  void append(int key, int value) {
    std::unique_lock<std::mutex> tmp_log(mtx);
    replication_log[key].push_back(value);
  }

  void remove(int key) {
    std::unique_lock<std::mutex> tmp_log(mtx);
    replication_log.erase(key);
  }
};

using in_mem_log = in_mem_log_t;

struct authenticator_hmac_t {
  static std::tuple<std::vector<unsigned char>, size_t>
  generate_attested_msg(const char *payload, size_t payload_sz) {
    auto [hmac_data, hmac_sz] =
        hmac_sha256(reinterpret_cast<const uint8_t *>(payload), payload_sz);
    std::vector<unsigned char> attested_msg(hmac_sz + payload_sz);

    print_buf(reinterpret_cast<char *>(hmac_data.data()), hmac_sz, __func__);
    ::memcpy(attested_msg.data(), hmac_data.data(), hmac_sz);
    ::memcpy(attested_msg.data() + hmac_sz, payload, payload_sz);
    return std::make_pair(attested_msg, hmac_sz + payload_sz);
  }

  static void print_buf(char *msg, size_t sz, const char *funcname) {
#if 0
    fmt::print("{}: [ ", funcname);
    for (auto i = 0ULL; i < sz; i++) 
      fmt::print("{} ", (int)(msg[i]));
    fmt::print("]\n");
#endif
  }

  static void print_buf(unsigned char *msg, size_t sz, const char *funcname) {
#if 0
    fmt::print("{}: [ ", funcname);
    for (auto i = 0ULL; i < sz; i++) 
      fmt::print("{} ", (int)(msg[i]));
    fmt::print("]\n");
#endif
  }

  static char *verify_attested_msg(char *attested_msg, size_t attested_msg_sz) {
    auto [calc_attestation, size] = generate_attested_msg(
        (attested_msg + _hmac_size), attested_msg_sz - _hmac_size);
    if (::memcmp(attested_msg, calc_attestation.data(), _hmac_size) == 0)
      return attested_msg + _hmac_size;
    else {
      fmt::print("{} verification failed\n", __func__);
    }
    return attested_msg + _hmac_size;
  }
};
