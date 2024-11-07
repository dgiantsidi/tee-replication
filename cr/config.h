constexpr int nb_requests = 100000;
constexpr int synthetic_delay_in_s = 0;
constexpr int kNodesSize = 2;

constexpr int head_port = 18000;
constexpr int head_id = 0;

constexpr int middle_port = 18002;
constexpr int middle_id = 1;

constexpr int tail_port = 18004;
constexpr int tail_id = 2;

// how many pending connections the queue will hold?
int backlog = 1024;