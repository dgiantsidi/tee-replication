

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