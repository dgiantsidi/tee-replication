#pragma once

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#if !defined(NDEBUG)
#include <fmt/format.h>
#if 0
template<class... Args>
void debug_print(fmt::format_string<Args...> fmt, Args &&... args) {
  fmt::print(fmt, std::forward<Args>(args)...);
}
#else // 0
// NOLINTNEXTLINE(readability-identifier-naming)
#define debug_print(...) fmt::print(__VA_ARGS__)
#endif // 0
#else  // !defined(NDEBUG)
template <class... Args> void debug_print(Args &&...) {}
#endif // !defined(NDEBUG)

#if !defined(LITTLE_ENDIAN)
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
#define LITTLE_ENDIAN __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#else
#define LITTLE_ENDIAN true
#endif
#endif

static constexpr auto length_size_field = sizeof(uint32_t);
static constexpr auto client_base_addr = 30500;
static constexpr auto number_of_connect_attempts = 20;
static constexpr auto gets_per_mille = 200;

template <class... Ts> struct overloaded : Ts... { // NOLINT
  using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

/**
 ** It takes as an argument a ptr to an array of size 4 or bigger and
 ** converts the char array into an integer.
 **/
inline auto convert_byte_array_to_int(char *b) noexcept -> uint32_t {
  if constexpr (LITTLE_ENDIAN) {
#if defined(__GNUC__)
    //   uint32_t res = 0;
    //   memcpy(&res, b, sizeof(res));
    //   return __builtin_bswap32(res);
    //#else  // defined(__GNUC__)
    return (b[0] << 24) | ((b[1] & 0xFF) << 16) | ((b[2] & 0xFF) << 8) |
           (b[3] & 0xFF);
#endif // defined(__GNUC__)
  }
  uint32_t result = 0;
  memcpy(&result, b, sizeof(result));
  return result;
}

/**
 ** It takes as arguments one char[] array of 4 or bigger size and an integer.
 ** It converts the integer into a byte array.
 **/
inline auto convert_int_to_byte_array(char *dst, uint32_t sz) noexcept -> void {
  if constexpr (LITTLE_ENDIAN) {
#if defined(__GNUC__)
    //   auto tmp = __builtin_bswap32(sz);
    //    memcpy(dst, &tmp, sizeof(tmp));
    //#else  // defined(__GNUC__)
    auto tmp = dst;
    tmp[0] = (sz >> 24) & 0xFF;
    tmp[1] = (sz >> 16) & 0xFF;
    tmp[2] = (sz >> 8) & 0xFF;
    tmp[3] = sz & 0xFF;
#endif     // defined(__GNUC__)
  } else { // BIG_ENDIAN
    memcpy(dst, &sz, sizeof(sz));
  }
}

class ErrNo {
  int err_no;

public:
  [[nodiscard]] inline ErrNo() noexcept : err_no(errno) {}
  inline explicit ErrNo(int err_no) noexcept : err_no(err_no) {}

  [[nodiscard]] inline auto msg() const noexcept -> std::string_view {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    return strerror(err_no);
  }

  [[nodiscard]] inline auto get_err_no() const noexcept -> int {
    return err_no;
  }

  [[nodiscard]] inline explicit operator int() const noexcept { return err_no; }
};

[[nodiscard]] auto secure_recv(int fd)
    -> std::pair<size_t, std::unique_ptr<char[]>>;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern hostent *hostip;

auto secure_send(int fd, char *data, size_t len) -> std::optional<size_t>;

/**
 * It constructs the message to be sent.
 * It takes as arguments a destination char ptr, the payload (data to be
 sent)
 * and the payload size.
 * It returns the expected message format at dst ptr;
 *
 *  |<---msg size (4 bytes)--->|<---payload (msg size bytes)--->|
 *
 *
 */
inline void construct_message(char *dst, const char *payload,
                              size_t payload_size) {
  convert_int_to_byte_array(dst, payload_size);
  ::memcpy(dst + length_size_field, payload, payload_size);
}

static void sent_init_connection_request(int port, int sockfd) {

  auto msg_size = sizeof(int);
  auto buf = std::make_unique<char[]>(msg_size);
  ::memcpy(buf.get(), &port, msg_size);
  std::cout << __func__ << " send:" << port << "\n";

  secure_send(sockfd, buf.get(), msg_size);
  std::cout << __func__ << " sent already:" << port << "\n";
}

#pragma once
static int connect_to_the_server(int port, char const * /*hostname*/,
                                 int &sending_sock) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  hostent *he = gethostbyname("localhost");
  hostip;
  auto rep_fd = 0;
  auto sockfd = 0;
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    fmt::print("socket\n");
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
    fmt::print("connect issue\n");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);
  }

  // init the listening socket
  int ret = 1;
  port = client_base_addr;
  // sent_init_connection_request(port, sockfd);
  sending_sock = sockfd;

  int repfd = socket(AF_INET, SOCK_STREAM, 0);
  if (repfd == -1) {
    fmt::print("socket\n");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);
  }

  if (setsockopt(repfd, SOL_SOCKET, SO_REUSEADDR, &ret, sizeof(int)) == -1) {
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

  if (bind(repfd, reinterpret_cast<sockaddr *>(&my_addr), sizeof(sockaddr)) ==
      -1) {
    fmt::print("bind\n");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);
  }
  constexpr int max_backlog = 1024;
  if (listen(repfd, max_backlog) == -1) {
    fmt::print("listen\n");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);
  }

  socklen_t sin_size = sizeof(sockaddr_in);
  fmt::print("[{}] waiting for new connections ..\n", __func__);

  sockaddr_in tmp_addr{};
  auto new_fd = accept4(repfd, reinterpret_cast<sockaddr *>(&tmp_addr),
                        &sin_size, SOCK_CLOEXEC);
  if (new_fd == -1) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    fmt::print("accecpt() failed ..{}\n", std::strerror(errno));
  }

  // fcntl(new_fd, F_SETFL, O_NONBLOCK);
  fmt::print("accept succeeded on socket {} {} ..\n", new_fd, repfd);
  rep_fd = new_fd;
  return rep_fd;
}

static void sent_request(char *request, size_t size, int sockfd) {
  if (auto numbytes = secure_send(sockfd, request, size); !numbytes) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    fmt::print("{}\n", std::strerror(errno));
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);
  }
  // fmt::print("{} sents {} bytes\n", __func__, size);
}

auto recv_ack(auto listening_socket)
    -> std::pair<int, std::unique_ptr<char[]>> {
  auto rep_fd = listening_socket;
  auto [bytecount, buffer] = secure_recv(rep_fd);
  if (buffer == nullptr) {
    fmt::print("ERROR\n");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(2);
  }
  if (bytecount == 0) {
    fmt::print("ERROR\n");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(2);
  }

  // fmt::print("[{}] {} {}\n", __func__, bytecount, buffer.get());
  return std::make_pair(bytecount, std::move(buffer));
}

inline auto destruct_message(char *msg, size_t bytes)
    -> std::optional<uint32_t> {
  if (bytes < 4) {
    fmt::print("{} error\n", __func__);
    return std::nullopt;
  }

  auto actual_msg_size = convert_byte_array_to_int(msg);
  return actual_msg_size;
}
