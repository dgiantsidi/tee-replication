#pragma once
#include "rpc.h"
#include <stdio.h>

static const std::string kdonna = "129.215.165.54";
static const std::string kmartha = "129.215.165.53";

static constexpr uint16_t kUDPPort = 31850;
static constexpr uint8_t kReqType = 2;
static constexpr size_t kMsgSize = 256 + 64;

static void sm_handler(int local_session, erpc::SmEventType, erpc::SmErrType,
                       void *) {}
