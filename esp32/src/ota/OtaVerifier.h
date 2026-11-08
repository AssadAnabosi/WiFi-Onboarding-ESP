#pragma once

#include <Update.h>

/**
 * @brief Verifies an update signature against every key this build trusts.
 *
 * Passed to Update.installSignature(); tries each trusted key in turn
 * (release keys, plus the dev key on dev builds).
 */
class TrustedKeyVerifier : public UpdaterVerifyClass
{
public:
  bool verify(SHA2Builder *hash, const void *signature, size_t signatureLen) override;
  int getHashType() const override { return HASH_SHA256; }

  /**
   * @brief ID of the key that verified the last signature, or nullptr.
   */
  const char *matchedKeyId() const { return _matchedKeyId; }

  /**
   * @brief Number of keys this build trusts.
   */
  static size_t trustedKeyCount();

private:
  const char *_matchedKeyId = nullptr;
};
