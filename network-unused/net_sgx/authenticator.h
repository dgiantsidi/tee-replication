#include "shared.h"
#include <fmt/printf.h>

/**
 * This objects are used by the protocol (1 obj for each replica).
 */

class authenticator_f {
protected:
  const int port = 18000;
  hostent *hostip = nullptr;
  int sending_fd = -1;
  int fd = -1;

public:
  virtual ~authenticator_f() = default;
  authenticator_f() = default;

  // no copy/move semantics
  authenticator_f(const authenticator_f &) = delete;
  authenticator_f(authenticator_f &&) = delete;
  void operator=(authenticator_f &) = delete;
  void operator=(authenticator_f &&) = delete;

  using auth_reply = std::pair<int, std::unique_ptr<char[]>>; // [size, msg]
  virtual auth_reply get_attestation(int msg_size, const char *data){};

  virtual bool verify_attestation(int msg_size, const char *data){};

  void send_req_to_trusted_service(int size, const char *data) {
    auto sending_sz = size + length_size_field;
    auto buff = std::make_unique<char[]>(sending_sz);

    construct_message(buff.get(), data, size);
    sent_request(buff.get(), sending_sz, sending_fd);
  };

  auth_reply recv_attestation() {
    auto res = recv_ack(fd);
    auto bytecount = std::get<0>(res);
    auto a = std::move(std::get<1>(res));

    return std::make_pair(bytecount, std::move(a));
  };
};
