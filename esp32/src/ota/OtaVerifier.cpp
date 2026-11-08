#include "OtaVerifier.h"
#include "OtaKeys.h"

bool TrustedKeyVerifier::verify(SHA2Builder *hash, const void *signature, size_t signatureLen)
{
  _matchedKeyId = nullptr;
  for (size_t i = 0; i < OTA_TRUSTED_KEY_COUNT; i++)
  {
    const OtaPublicKey &key = OTA_TRUSTED_KEYS[i];
    UpdaterECDSAVerifier verifier(key.der, key.length, HASH_SHA256);
    if (verifier.verify(hash, signature, signatureLen))
    {
      _matchedKeyId = key.id;
      return true;
    }
  }
  return false;
}

size_t TrustedKeyVerifier::trustedKeyCount()
{
  return OTA_TRUSTED_KEY_COUNT;
}
