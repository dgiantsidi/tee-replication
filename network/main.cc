#include "authenticators/apis.h"
#include "authenticators/authenticator.h"
#include "common.h"
#include "util/numautils.h"
#include <chrono>
#include <cstring>
#include <fmt/printf.h>
#include <gflags/gflags.h>
#include <signal.h>
#include <thread>

static constexpr int kReqLatency = 1;
static constexpr int PRINT_BATCH = 50000;
static constexpr int numa_node = 0;
enum authenticators {
  SGX = 0,   /* SGX as trusted entity */
  SGX_P = 1, /* SGX w/ persistency */
  SGX_R = 2, /* SGX w/ rollback */
  TNIC = 3   /* T-FPGA (mocked) */
};

DEFINE_uint64(num_server_threads, 1, "Number of threads at the server machine");
DEFINE_uint64(num_client_threads, 1, "Number of threads per client machine");
DEFINE_uint64(window_size, 1, "Outstanding requests per client");
DEFINE_uint64(req_size, 64, "Size of request message in bytes");
DEFINE_uint64(resp_size, 32, "Size of response message in bytes");
DEFINE_uint64(process_id, 0, "Process id");
DEFINE_uint64(instance_id, 0, "Instance id (this is to properly set the RPCs");
DEFINE_uint64(reqs_num, 10, "Number of reqs");
DEFINE_uint64(authenticator_mode, authenticators::TNIC, "Authenticator type");

using rpc_handle = erpc::Rpc<erpc::CTransport>;

struct rpc_buffs {
  erpc::MsgBuffer req;
  erpc::MsgBuffer resp;

  void alloc_req_buf(size_t sz, rpc_handle *rpc) {
    req = rpc->alloc_msg_buffer(sz);
    while (req.buf == nullptr) {
      fmt::print("[{}] failed to allocate memory\n", __func__);
      rpc->run_event_loop(100);
      req = rpc->alloc_msg_buffer(sz);
    }
  }

  void alloc_resp_buf(size_t sz, rpc_handle *rpc) {
    resp = rpc->alloc_msg_buffer(sz);
    while (resp.buf == nullptr) {
      fmt::print("[{}] failed to allocate memory\n", __func__);
      rpc->run_event_loop(100);
      resp = rpc->alloc_msg_buffer(sz);
    }
  }

  ~rpc_buffs() {}
};

class app_context {
public:
  rpc_handle *rpc = nullptr;
  int node_id = 0, thread_id = 0;
  int instance_id = 0; /* instance id used to create the rpcs */
  authenticator_f *auth = nullptr;
  uint64_t enqueued_msgs_cnt = 0;
  std::atomic<uint64_t> received_msgs_cnt = {0};
  std::atomic<uint64_t> sent_acks_cnt = {0};
  /* map of <node_id, session_num> that maintains connections */
  std::unordered_map<int, int> connections;

  /* delete copy/move constructors/assignments
     app_context(const app_context &) = delete;
     app_context(app_context &&) = delete;
     app_context &operator=(const app_context &) = delete;
     app_context &operator=(app_context &&) = delete;
     */
  ~app_context() {
    fmt::print("{} ..\n", __func__);
    delete rpc;
  }
};

static void cont_func(void *context, void *t) {
  auto *ctx = static_cast<app_context *>(context);
  if (ctx == nullptr) {
    fmt::print("{} ctx==nullptr \n", __func__);
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(10000ms);
  }
  auto *tag = static_cast<rpc_buffs *>(t);
  ctx->rpc->free_msg_buffer(tag->req);
  ctx->rpc->free_msg_buffer(tag->resp);
  ctx->received_msgs_cnt.fetch_add(1);

  delete tag;
}

void send_req(int idx, int dest_id, app_context *ctx) {
  // construct message
  constexpr int msg_size = 64;
  auto msg_buf = std::make_unique<uint8_t[]>(msg_size);
  auto mock = [](int sz) -> std::pair<size_t, std::unique_ptr<uint8_t[]>> {
    auto ptr = std::make_unique<uint8_t[]>(sz);
    return std::make_pair(sz, std::move(ptr));
  };

  auto get_attestation =
      [](int sz, uint8_t *data,
         authenticator_f *auth) -> std::pair<size_t, std::unique_ptr<char[]>> {
    auto [attestation_sz, attestation] =
        auth->get_attestation(sz, reinterpret_cast<char *>(data));
    return std::make_pair(attestation_sz, std::move(attestation));
  };

  auto [bytecount, attestation] =
      get_attestation(msg_size, msg_buf.get(), ctx->auth); // mock(256);
  auto to_be_sent_sz = bytecount + msg_size;

  // needs to send
  rpc_buffs *buffs = new rpc_buffs();
  buffs->alloc_req_buf(to_be_sent_sz, ctx->rpc);
  buffs->alloc_resp_buf(kMsgSize, ctx->rpc);

  ::memcpy(buffs->req.buf, attestation.get(), bytecount);
  ::memcpy(buffs->req.buf + bytecount, msg_buf.get(), msg_size);

  // enqueue_req
  ctx->rpc->enqueue_request(ctx->connections[dest_id], kReqLatency, &buffs->req,
                            &buffs->resp, cont_func,
                            reinterpret_cast<void *>(buffs));
  // fmt::print("{} idx={}\n", __func__, idx);
  ctx->enqueued_msgs_cnt++;
}

void poll(app_context *ctx) {
  ctx->rpc->run_event_loop_once();
  if (ctx->enqueued_msgs_cnt % 2 == 0) {
    for (auto i = 0; i < 10; i++)
      ctx->rpc->run_event_loop_once();
  }
  if (ctx->enqueued_msgs_cnt % 2 == 10)
    ctx->rpc->run_event_loop(1);
}

[[maybe_unused]] void poll_once(app_context *ctx) {
  ctx->rpc->run_event_loop(100);
}

void create_session(const std::string &uri, int rem_node_id, app_context *ctx) {
  ctx->rpc->retry_connect_on_invalid_rpc_id = true;
  std::string middle_uri = uri + ":" + std::to_string(kUDPPort);
  fmt::print("[{}] uri={}\n", __PRETTY_FUNCTION__, middle_uri);
  int session_num = ctx->rpc->create_session(middle_uri, ctx->instance_id);
  while (!ctx->rpc->is_connected(session_num)) {
    ctx->rpc->run_event_loop_once();
  }
  fmt::print("[{}] connected to uri={} w/ remote RPC_id={} session_num={} "
             "(rem_node_id={})\n",
             __func__, uri, ctx->instance_id, session_num, rem_node_id);
  ctx->connections.insert({rem_node_id, session_num});
  if (rem_node_id == 1) {
    ctx->rpc->run_event_loop(3000);
  }
}

enum mode { sender = 0, receiver = 1 };

void sender_func(app_context *ctx, const std::string &uri) {
  fmt::print("[{}]\tinstance_id={}\tthread_id={}\turi={}.\n",
             __PRETTY_FUNCTION__, ctx->instance_id, ctx->thread_id, uri);
  create_session(uri, mode::receiver, ctx);
  ctx->rpc->run_event_loop(1000);
  auto start = std::chrono::high_resolution_clock::now();
  for (auto i = 0ULL; i < FLAGS_reqs_num; i++) {
    send_req(i, mode::sender, ctx);
    // dimitra: this is for latency measurements
    while (ctx->received_msgs_cnt.load() < (i + 1)) {
      // fmt::print("{} {}\n", ctx->received_msgs_cnt.load(), (i+1));
      poll(ctx);
    }
  }
  fmt::print("{} polls until the end ...\n", __func__);
  for (;;) {
    ctx->rpc->run_event_loop_once();
    if (ctx->received_msgs_cnt.load() == FLAGS_reqs_num) {
      auto elapsed = std::chrono::high_resolution_clock::now() - start;
      long long time_us =
          std::chrono::duration_cast<std::chrono::microseconds>(elapsed)
              .count();
      auto lat = static_cast<float>(time_us) / (1.0 * FLAGS_reqs_num * 2);
      fmt::print("[{}] received all msgs={} in {} us (latency={})\n", __func__,
                 FLAGS_reqs_num, time_us, lat);
      ctx->rpc->run_event_loop(2000);
      break;
    }
  }
}

void receiver_func(app_context *ctx) {
  fmt::print("[{}]\tinstance_id={}\tthread_id={}.\n", __PRETTY_FUNCTION__,
             ctx->instance_id, ctx->thread_id);
  //  create_session(uri, mode::sender /* node 0 */, ctx);
  // ctx->rpc->run_event_loop(100);

  fmt::print("{} polls until the end ...\n", __func__);
  for (;;) {
    ctx->rpc->run_event_loop_once();
    //   fmt::print("[{}] sent_acks_cnt={}\n", __func__,
    //   ctx->sent_acks_cnt.load());
    if (ctx->sent_acks_cnt.load() == FLAGS_reqs_num) {
      fmt::print("[{}] received all msgs \n", __func__);
      ctx->rpc->run_event_loop(2000);
      break;
    }
  }
}

void proto_func(size_t thread_id, erpc::Nexus *nexus) {
  fmt::print("[{}]\tthread_id={} starts\n", __PRETTY_FUNCTION__, thread_id);
  app_context *ctx = new app_context();
  ctx->instance_id = FLAGS_instance_id;
  ctx->node_id = FLAGS_process_id;
  ctx->rpc = new rpc_handle(nexus, static_cast<void *>(ctx), ctx->instance_id,
                            sm_handler, 0);

  switch (FLAGS_authenticator_mode) {

  case (authenticators::SGX): {
    fmt::print("[{}] authenticators::SGX\n", __func__);
    ctx->auth = new auth_sgx();
    break;
  }
  case (authenticators::SGX_P): {
    fmt::print("[{}] authenticators::SGX_P\n", __func__);
    ctx->auth = new auth_sgx_with_persistency();
    break;
  }
  case (authenticators::SGX_R): {
    fmt::print("[{}] authenticators::SGX_R\n", __func__);
    ctx->auth = new auth_sgx_with_rollback();
    break;
  }
  case (authenticators::TNIC): {
    fmt::print("[{}] authenticators::TNIC\n", __func__);
    ctx->auth = new auth_tnic();
    break;
  }
  default:
    fmt::print("[{}] I shouldn't be here \n", __func__);
  }
  /* give some time before we start */
  using namespace std::chrono_literals;
  std::this_thread::sleep_for(5000ms);
  /* we can start */

  if (FLAGS_process_id == mode::sender) {
    sender_func(ctx, std::string{kmartha});
    delete ctx->auth;
  } else if (FLAGS_process_id == mode::receiver) {
    receiver_func(ctx);
    delete ctx->auth;
  } else {
    fmt::print("[{}] error\n", __PRETTY_FUNCTION__);
  }

  fmt::print("[{}]\tthread_id={} exits.\n", __PRETTY_FUNCTION__, thread_id);
  delete ctx;
}

void ctrl_c_handler(int) {
  fmt::print("{} ctrl-c .. exiting\n", __PRETTY_FUNCTION__);
  exit(1);
}

void req_handler_latency(erpc::ReqHandle *req_handle,
                         void *context /* app_context */) {
  static uint64_t cnt = 0;

  // deserialize the message-req
  uint8_t *recv_data =
      reinterpret_cast<uint8_t *>(req_handle->get_req_msgbuf()->buf);
  size_t recv_data_sz = req_handle->get_req_msgbuf()->get_data_size();
  /*
   * verify the attestation
   */

  if (cnt % PRINT_BATCH == 0) {
    // print batched
    fmt::print("{} count={}\n", __func__, cnt);
  }
  cnt++;

  /* enqueue ack */

  app_context *ctx = reinterpret_cast<app_context *>(context);
  auto &resp = req_handle->pre_resp_msgbuf;
  constexpr int msg_size = 64;
  auto msg_buf = std::make_unique<uint8_t[]>(msg_size);
  auto mock = [](int sz) -> std::pair<size_t, std::unique_ptr<uint8_t[]>> {
    auto ptr = std::make_unique<uint8_t[]>(sz);
    return std::make_pair(sz, std::move(ptr));
  };
  auto get_attestation =
      [](int sz, uint8_t *data,
         authenticator_f *auth) -> std::pair<size_t, std::unique_ptr<char[]>> {
    auto [attestation_sz, attestation] =
        auth->get_attestation(sz, reinterpret_cast<char *>(data));
    return std::make_pair(attestation_sz, std::move(attestation));
  };

  auto [bytecount, attestation] =
      get_attestation(msg_size, msg_buf.get(), ctx->auth); // mock(256);
  // auto [bytecount, attestation] = mock(256);
  auto to_be_sent_sz = bytecount + msg_size;
  /*
   * auto [bytecount, attestation] = attest_with_sgx(std::move(msg_buf));
   * auto to_be_sent_sz = bytecount + msg_size;
   *
   */
  if (ctx == nullptr) {
    fmt::print("{} ctx==nullptr \n", __func__);
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(10000ms);
  }
  if (ctx->rpc == nullptr) {
    fmt::print("ctx->rpc==nullptr\n", __func__);
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(10000ms);
  }
  ctx->rpc->resize_msg_buffer(&resp, to_be_sent_sz);
  ::memcpy(resp.buf, attestation.get(), bytecount);
  ::memcpy(resp.buf + bytecount, msg_buf.get(), msg_size);
  ctx->rpc->enqueue_response(req_handle, &resp);
  ctx->sent_acks_cnt.fetch_add(1);
  // fmt::print("{}\n", ctx->sent_acks_cnt.load());
}

int main(int args, char *argv[]) {
  signal(SIGINT, ctrl_c_handler);
  gflags::ParseCommandLineFlags(&args, &argv, true);

  size_t num_threads = FLAGS_process_id == 0 ? FLAGS_num_server_threads
                                             : FLAGS_num_client_threads;

  std::string server_uri;
  if (FLAGS_process_id == 0) {
    server_uri = kdonna + ":" + std::to_string(kUDPPort);
  } else if (FLAGS_process_id == 1) {
    server_uri = kmartha + ":" + std::to_string(kUDPPort);
  } else {
    fmt::print("[{}] not valid process_id={}\n", __func__, FLAGS_process_id);
  }
  erpc::Nexus nexus(server_uri, 0, 0);
  nexus.register_req_func(kReqLatency, req_handler_latency);

  std::vector<std::thread> threads;
  for (size_t i = 0; i < num_threads; i++) {
    threads.emplace_back(std::thread(proto_func, i, &nexus));
    erpc::bind_to_core(threads[i], numa_node, i);
  }

  for (auto &thread : threads)
    thread.join();

  return 0;
}
