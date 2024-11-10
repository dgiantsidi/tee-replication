constexpr int nb_requests = 100000;
constexpr int synthetic_delay_in_s = 0;
constexpr int kNodesSize = 2;

constexpr int head_port = 18000;
constexpr int head_id = 0;

constexpr int middle_port = 19000;
constexpr int middle_id = 1;

constexpr int tail_port = 20000;
constexpr int tail_id = 2;

// how many pending connections the queue will hold?
int backlog = 1024;

constexpr std::string tail_ip = "131.159.102.20";
constexpr std::string middle_ip = "131.159.102.22";
constexpr std::string head_ip = "131.159.102.25";

