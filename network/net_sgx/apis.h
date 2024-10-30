#include "authenticator.h"
#include "shared.h"
#include <fmt/printf.h>

class sgx : public authenticator_f {
public:
  authenticator_f::auth_reply get_attestation(int size, const char *data) {
    send_req_to_trusted_service(size, data);
    return recv_attestation();
  };

  bool ver_attestation(int size, const char *data) { // FIXME:
    return true;
  };

  authenticator() {
    hostip = gethostbyname("localhost");
    fd = connect_to_the_server(port, "localhost", sending_fd);

    fmt::print("[{}] connect_to_the_server sending_fd={} fd={}\n", __func__,
               sending_fd, fd);
  }

  ~authenticator() {
    // @dimitra:
    fmt::print("[{}] Close connections.\n", __func__);
  }
};

class sgx_with_persistency : public authenticator_f {
private:
  int persistent_fd = -1;
  void open_file() {
    const char *filename = "sgx_with_persistency_log.txt";

    persistent_fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC);
    if (persistent_fd == -1) {
      fmt::print("[{}] cannot open or create the file\n", __func__);
      exit(128);
    }
  }

  void log_attestation(int size, const char *data) {
    int written = 0;
    int completed_bytes = 0;
    while (completed_bytes < size) {
      written = ::write(persistent_fd, data, size - completed_bytes);
      if (written < 0) {
        fmt::print("[{}] Error while logging\n", __func__);
      }
      completed_bytes += written;
    }
    ::fsync(persistent_fd);
  }

public:
  authenticator_f::auth_reply get_attestation(int size, const char *data) {
    send_req_to_trusted_service(size, data);
    auto res = recv_attestation();
    log_attestation(std::get<0>(res), std::get<1>(reg).get());
    return std::move(res);
  };

  bool ver_attestation(int size, const char *data) { // FIXME:
    return true;
  };

  authenticator() {
    open_file();
    hostip = gethostbyname("localhost");
    fd = connect_to_the_server(port, "localhost", sending_fd);

    fmt::print("[{}] connect_to_the_server sending_fd={} fd={}\n", __func__,
               sending_fd, fd);
  }

  ~authenticator() {
    // @dimitra:
    fmt::print("[{}] Close connections.\n", __func__);
  }
};

class sgx_with_rollback : public authenticator_f {
private:
  int persistent_fd = -1;
  void open_file() {
    const char *filename = "sgx_with_persistency_log.txt";

    persistent_fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC);
    if (persistent_fd == -1) {
      fmt::print("[{}] cannot open or create the file\n", __func__);
      exit(128);
    }
  }

  int get_ROTE_counter() {
    static int cnt = 0;
    std::this_thread::sleep_for(2ms); //@dimitra: from paper
    return cnt++;
  }

  void log_attestation(int size, const char *data) {
    int written = 0;
    int completed_bytes = 0;
    while (completed_bytes < size) {
      written = ::write(persistent_fd, data, size - completed_bytes);
      if (written < 0) {
        fmt::print("[{}] Error while logging\n", __func__);
      }
      completed_bytes += written;
    }
    int [[maybe_unused]] trusted_cnt = get_ROTE_counter();
    ::fsync(persistent_fd);
  }

public:
  authenticator_f::auth_reply get_attestation(int size, const char *data) {
    send_req_to_trusted_service(size, data);
    auto res = recv_attestation();
    log_attestation(std::get<0>(res), std::get<1>(reg).get());
    return std::move(res);
  };

  bool ver_attestation(int size, const char *data) { // FIXME:
    return true;
  };

  authenticator() {
    open_file();
    hostip = gethostbyname("localhost");
    fd = connect_to_the_server(port, "localhost", sending_fd);

    fmt::print("[{}] connect_to_the_server sending_fd={} fd={}\n", __func__,
               sending_fd, fd);
  }

  ~authenticator() {
    // @dimitra:
    fmt::print("[{}] Close connections.\n", __func__);
  }
};

class tnic : public authenticator_f {
private:
  void t_fpga() {
    std::this_thread::sleep_for(50us); //@dimitra: from microbenchmarking
  }

public:
  authenticator_f::auth_reply get_attestation(int size, const char *data) {
    t_fpga();
  };

  bool ver_attestation(int size, const char *data) { // FIXME:
    return true;
  };

  authenticator() {}

  ~authenticator() {}
};
