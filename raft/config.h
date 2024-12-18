constexpr int nb_requests = 100000;
constexpr int synthetic_delay_in_s = 0;
constexpr int kNodesSize = 2;

constexpr int follower_1_port = 18000;
constexpr int follower_1_id = 1;

constexpr int follower_2_port = 18002;
constexpr int follower_2_id = 2;

// how many pending connections the queue will hold?
int backlog = 1024;

constexpr std::string follower_ip_1 = "131.159.102.20";
constexpr std::string follower_ip_2 = "131.159.102.22";
constexpr std::string leader_ip = "131.159.102.25";
