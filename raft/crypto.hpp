#pragma once
#include <cstring>
#include <iostream>
#include <memory>
#include <openssl/hmac.h>
#include <tuple>
#include <vector>

static constexpr size_t _hmac_size = EVP_MAX_MD_SIZE / 2;
std::tuple<std::vector<unsigned char>, size_t>
hmac_sha256(const unsigned char *data, size_t sz) {
  std::string key = {
      "LElxWRonjVkCoArWzZqliiSEtmlbaCfZaGkrSweWJKQkgQsyrBUpSusAcPcGDfFh"};
  unsigned int len = EVP_MAX_MD_SIZE;
  std::vector<unsigned char> digest(len);

  HMAC_CTX *ctx = HMAC_CTX_new();
  HMAC_CTX_reset(ctx);
  // std::cout << __PRETTY_FUNCTION__ << "key.size()=" << key.size() << "\n";
  HMAC_Init_ex(ctx, key.data(), key.size(), EVP_sha256(), NULL);
  HMAC_Update(ctx, data, sz);
  HMAC_Final(ctx, digest.data(), &len);
  // std::cout << "len=" << len << " " << digest.size() << "\n";
  HMAC_CTX_free(ctx);

  return std::make_pair(digest, len);
}
