#pragma once
#include "authenticator.h"
#include "shared.h"
#include <chrono>
#include <fcntl.h>
#include <fmt/printf.h>

class auth_sgx : public authenticator_f {
public:
  authenticator_f::auth_reply get_attestation(int size, const char *data) {
    send_req_to_trusted_service(size, data);
    return recv_attestation();
  };

  bool ver_attestation(int size, const char *data) { // FIXME:
    return true;
  };

  auth_sgx() {
    hostip = gethostbyname("localhost");
    fd = connect_to_the_server(port, "localhost", sending_fd);

    fmt::print("[{}] connect_to_the_server sending_fd={} fd={}\n", __func__,
               sending_fd, fd);
  }

  ~auth_sgx() {
    // @dimitra:
    fmt::print("[{}] Close connections.\n", __func__);
  }
};

class auth_sgx_with_persistency : public authenticator_f {
private:
  int persistent_fd = -1;
  void open_file() {
    const char *filename = "sgx_with_persistency_log.txt";

    persistent_fd = open(filename, O_CREAT | O_RDWR | O_TRUNC, S_IRWXU);
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
    log_attestation(std::get<0>(res), std::get<1>(res).get());
    return std::move(res);
  };

  bool ver_attestation(int size, const char *data) { // FIXME:
    return true;
  };

  auth_sgx_with_persistency() {
    open_file();
    hostip = gethostbyname("localhost");
    fd = connect_to_the_server(port, "localhost", sending_fd);

    fmt::print("[{}] connect_to_the_server sending_fd={} fd={}\n", __func__,
               sending_fd, fd);
  }

  ~auth_sgx_with_persistency() {
    // @dimitra:
    fmt::print("[{}] Close connections.\n", __func__);
  }
};

class auth_sgx_with_rollback : public authenticator_f {
private:
  int persistent_fd = -1;
  void open_file() {
    const char *filename = "sgx_with_persistency_log.txt";

    persistent_fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU);
    if (persistent_fd == -1) {
      fmt::print("[{}] cannot open or create the file\n", __func__);
      exit(128);
    }
  }

  int get_ROTE_counter() {
    static int cnt = 0;
    {
      using namespace std::literals::chrono_literals;
      std::this_thread::sleep_for(2ms); //@dimitra: from paper
    }
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
    log_attestation(std::get<0>(res), std::get<1>(res).get());
    return std::move(res);
  };

  bool ver_attestation(int size, const char *data) { // FIXME:
    return true;
  };

  auth_sgx_with_rollback() {
    open_file();
    hostip = gethostbyname("localhost");
    fd = connect_to_the_server(port, "localhost", sending_fd);

    fmt::print("[{}] connect_to_the_server sending_fd={} fd={}\n", __func__,
               sending_fd, fd);
  }

  ~auth_sgx_with_rollback() {
    // @dimitra:
    fmt::print("[{}] Close connections.\n", __func__);
  }
};

class auth_tnic : public authenticator_f {
private:
  void t_fpga() {
    {
      using namespace std::literals::chrono_literals;
      std::this_thread::sleep_for(50us); //@dimitra: from microbenchmarking
    }
  }

public:
  authenticator_f::auth_reply get_attestation(int size, const char *data) {
    t_fpga();
    std::unique_ptr<char[]> attested_message = std::make_unique<char[]>(size);
    ::memcpy(attested_message.get(), data, size);
    return authenticator_f::auth_reply(size, std::move(attested_message));
  };

  bool ver_attestation(int size, const char *data) { // FIXME:
    return true;
  };

  auth_tnic() {}

  ~auth_tnic() {}
};
