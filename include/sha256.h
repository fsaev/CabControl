#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

class Sha256 {
public:
  static constexpr size_t DIGEST_SIZE = 32;
  static constexpr size_t BLOCK_SIZE = 64;

  Sha256() { reset(); }
  void reset();
  void update(const uint8_t *data, size_t len);
  void finish(uint8_t digest[DIGEST_SIZE]);

private:
  void transform(const uint8_t block[BLOCK_SIZE]);

  uint32_t m_state[8];
  uint64_t m_bit_count;
  uint8_t m_buffer[BLOCK_SIZE];
  size_t m_buffer_len;
};

void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data,
                 size_t data_len, uint8_t mac[Sha256::DIGEST_SIZE]);

// Comparison time depends only on len, not on where the buffers differ.
bool constant_time_equal(const uint8_t *a, const uint8_t *b, size_t len);

#endif
