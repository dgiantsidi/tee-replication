#include <fmt/printf.h>

struct msg {
  static constexpr size_t key_sz = 8;
  static constexpr size_t value_sz = 40;
  struct header {
    uint32_t src_node;
    uint32_t dest_node;
    uint32_t seq_idx;
    uint32_t msg_size;
  };
  // msg format
  header hdr;
  uint8_t key[key_sz];
  uint8_t value[value_sz];
};

struct cmt_msg {
  struct header {
    uint32_t src_node;
    uint32_t dest_node;
    uint32_t seq_idx;
    uint32_t msg_size;
  };
  // msg format
  header hdr;
  uint64_t s_cmt_idx;
  uint64_t e_cmt_idx;
};

struct msg_manager {
  static constexpr size_t batch_count = 16;
  msg_manager() {
    buffer = std::make_unique<uint8_t[]>(batch_count * sizeof(msg));
    fmt::print("sizeof(msg)={}\n", sizeof(msg));
  };

  bool enqueue_req(uint8_t *buf, size_t buf_sz) {
    if (buf_sz != sizeof(msg))
      fmt::print("[{}] buf_sz != sizeof(msg)\n", __PRETTY_FUNCTION__);
    if (cur_idx < batch_count) {
      ::memcpy(buffer.get() + cur_idx * sizeof(msg), buf, buf_sz);
      cur_idx++;
      return true;
    } else
      return false;
  }

  uint8_t *serialize_batch() { return buffer.get(); }

  void empty_buff() {
    // fmt::print("{}\n", __func__);
    cur_idx = 0;
  }

  static std::unique_ptr<uint8_t[]> deserialize(uint8_t *buf, size_t buf_sz) {
    if (buf_sz != sizeof(msg) * batch_count)
      fmt::print("[{}] buf_sz != batch_count*sizeof(msg)\n",
                 __PRETTY_FUNCTION__);
    auto data = std::make_unique<uint8_t[]>(buf_sz);
    ::memcpy(data.get(), buf, buf_sz);
    return std::move(data);
  }

  static void print_batched(std::unique_ptr<uint8_t[]> msgs, size_t sz) {
    int min_idx = -1, max_idx = -1;
    for (auto i = 0ULL; i < batch_count; i++) {
      auto msg_data = std::make_unique<msg>();
      ::memcpy(reinterpret_cast<uint8_t *>(msg_data.get()),
               msgs.get() + i * sizeof(msg), sizeof(msg::header));
      if (i == 0)
        min_idx = msg_data->hdr.seq_idx;
      if (i == (batch_count - 1))
        max_idx = msg_data->hdr.seq_idx;
    }
    fmt::print("[{}] min_idx={}\tmax_idx={}\n", __PRETTY_FUNCTION__, min_idx,
               max_idx);
  }

  static std::vector<int> parse_indexes(std::unique_ptr<uint8_t[]> msgs,
                                        size_t sz) {
    int min_idx = -1, max_idx = -1, prev_idx = -1;
    for (auto i = 0ULL; i < batch_count; i++) {
      auto msg_data = std::make_unique<msg>();
      ::memcpy(reinterpret_cast<uint8_t *>(msg_data.get()),
               msgs.get() + i * sizeof(msg), sizeof(msg::header));
      if (i == 0) {
        min_idx = msg_data->hdr.seq_idx;
        prev_idx =
            min_idx - 1; // I will increase it later on for this iteration
      } else if (i == (batch_count - 1))
        max_idx = msg_data->hdr.seq_idx;
      else {
        if ((prev_idx + 1) != msg_data->hdr.seq_idx)
          fmt::print("[{}] an error happenned here .. expected={}\treal={}\n",
                     __func__, (prev_idx + 1), msg_data->hdr.seq_idx);
      }
      prev_idx++;
    }
    return std::vector<int>{min_idx, max_idx};
  }

  std::unique_ptr<uint8_t[]> buffer;
  uint32_t cur_idx = 0;
};
