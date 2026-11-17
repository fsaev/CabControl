#include "ota_core.h"
#include "sha256.h"

#include <string.h>

namespace {

uint32_t read_u32_le(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

void write_u32_le(uint8_t *p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value);
  p[1] = static_cast<uint8_t>(value >> 8);
  p[2] = static_cast<uint8_t>(value >> 16);
  p[3] = static_cast<uint8_t>(value >> 24);
}

int compare_versions(const OtaCore::Version &a, const OtaCore::Version &b) {
  if (a.major != b.major) {
    return a.major < b.major ? -1 : 1;
  }
  if (a.minor != b.minor) {
    return a.minor < b.minor ? -1 : 1;
  }
  if (a.patch != b.patch) {
    return a.patch < b.patch ? -1 : 1;
  }
  return 0;
}

const uint8_t *find_bytes(const uint8_t *haystack, size_t haystack_len,
                          const uint8_t *needle, size_t needle_len) {
  if (needle_len == 0 || haystack_len < needle_len) {
    return nullptr;
  }
  for (size_t i = 0; i <= haystack_len - needle_len; i++) {
    if (memcmp(haystack + i, needle, needle_len) == 0) {
      return haystack + i;
    }
  }
  return nullptr;
}

} // namespace

OtaCore::OtaCore(const uint8_t *secret, size_t secret_len, Version current,
                 const uint8_t *marker_magic, const char *dir, RandomFn random)
    : m_secret(secret), m_secret_len(secret_len), m_current(current),
      m_marker_magic(marker_magic), m_random(random) {
  snprintf(m_tmp_path, sizeof(m_tmp_path), "%s/UPDATE.TMP", dir);
  snprintf(m_bin_path, sizeof(m_bin_path), "%s/UPDATE.BIN", dir);
}

void OtaCore::new_nonce() { m_random(m_nonce, sizeof(m_nonce)); }

void OtaCore::close_file() {
  if (m_file != nullptr) {
    fclose(m_file);
    m_file = nullptr;
  }
}

void OtaCore::remove_files() {
  close_file();
  remove(m_tmp_path);
  remove(m_bin_path);
}

void OtaCore::reset() {
  m_begin_len = 0;
  if (m_state != State::READY) {
    if (m_state == State::RECEIVING || m_state == State::VERIFYING) {
      remove_files();
    }
    m_state = State::IDLE;
    m_error = Error::NONE;
    m_received = 0;
    m_total = 0;
  }
  new_nonce();
}

void OtaCore::fail(Error error) {
  if (m_state == State::READY) {
    return;
  }
  remove_files();
  m_state = State::ERROR;
  m_error = error;
}

void OtaCore::handle_control(const uint8_t *data, size_t len) {
  if (len == 0) {
    fail(Error::BAD_COMMAND);
    return;
  }
  if (m_state == State::READY || m_state == State::VERIFYING) {
    return;
  }
  switch (static_cast<Command>(data[0])) {
  case Command::BEGIN:
    begin_fragment(data, len);
    break;
  case Command::COMMIT:
    commit();
    break;
  case Command::ABORT:
    fail(Error::ABORTED);
    break;
  default:
    fail(Error::BAD_COMMAND);
    break;
  }
}

void OtaCore::begin_fragment(const uint8_t *data, size_t len) {
  if (len <= BEGIN_HEADER_SIZE) {
    m_begin_len = 0;
    fail(Error::BAD_COMMAND);
    return;
  }
  const size_t fragment_offset = data[1];
  const size_t fragment_len = len - BEGIN_HEADER_SIZE;
  if (fragment_offset == 0) {
    m_begin_len = 0;
  }
  if (fragment_offset != m_begin_len ||
      fragment_len > BEGIN_BODY_SIZE - m_begin_len) {
    m_begin_len = 0;
    new_nonce();
    fail(Error::BAD_COMMAND);
    return;
  }
  memcpy(m_begin_body + m_begin_len, data + BEGIN_HEADER_SIZE, fragment_len);
  m_begin_len += fragment_len;
  if (m_begin_len == BEGIN_BODY_SIZE) {
    m_begin_len = 0;
    begin(m_begin_body);
  }
}

void OtaCore::begin(const uint8_t *body) {
  if (m_state == State::RECEIVING) {
    remove_files();
  }
  m_state = State::IDLE;
  m_received = 0;
  m_total = 0;

  const size_t label_len = strlen(AUTH_LABEL);
  uint8_t message[32 + NONCE_SIZE + BEGIN_SIGNED_SIZE];
  memcpy(message, AUTH_LABEL, label_len);
  memcpy(message + label_len, m_nonce, NONCE_SIZE);
  memcpy(message + label_len + NONCE_SIZE, body, BEGIN_SIGNED_SIZE);
  uint8_t expected_tag[Sha256::DIGEST_SIZE];
  hmac_sha256(m_secret, m_secret_len, message,
              label_len + NONCE_SIZE + BEGIN_SIGNED_SIZE, expected_tag);
  // A nonce is only good for one attempt, successful or not.
  new_nonce();
  if (!constant_time_equal(expected_tag, body + BEGIN_SIGNED_SIZE,
                           sizeof(expected_tag))) {
    fail(Error::AUTH);
    return;
  }

  const uint32_t size = read_u32_le(body);
  const Version version = {body[4], body[5], body[6]};
  if (compare_versions(version, m_current) <= 0) {
    fail(Error::VERSION);
    return;
  }
  if (size < MIN_IMAGE_SIZE || size > MAX_IMAGE_SIZE) {
    fail(Error::SIZE);
    return;
  }

  remove_files();
  m_file = fopen(m_tmp_path, "wb");
  if (m_file == nullptr) {
    fail(Error::STORAGE);
    return;
  }

  m_total = size;
  m_image_version = version;
  memcpy(m_image_hash, body + 7, sizeof(m_image_hash));
  m_error = Error::NONE;
  m_state = State::RECEIVING;
}

void OtaCore::handle_data(const uint8_t *data, size_t len) {
  if (m_state != State::RECEIVING) {
    // Chunks still in flight after a failure are dropped silently so the
    // original error stays visible to the client.
    if (m_state != State::ERROR) {
      fail(Error::STATE);
    }
    return;
  }
  if (len <= DATA_HEADER_SIZE) {
    fail(Error::BAD_COMMAND);
    return;
  }
  const uint32_t offset = read_u32_le(data);
  const size_t payload_len = len - DATA_HEADER_SIZE;
  if (offset != m_received) {
    fail(Error::SEQUENCE);
    return;
  }
  if (payload_len > m_total - m_received) {
    fail(Error::SIZE);
    return;
  }
  if (fwrite(data + DATA_HEADER_SIZE, 1, payload_len, m_file) != payload_len) {
    fail(Error::STORAGE);
    return;
  }
  m_received += payload_len;
}

void OtaCore::commit() {
  if (m_state != State::RECEIVING || m_received != m_total) {
    fail(Error::STATE);
    return;
  }
  m_state = State::VERIFYING;
  const bool flushed = fflush(m_file) == 0;
  const bool closed = fclose(m_file) == 0;
  m_file = nullptr;
  if (!flushed || !closed) {
    fail(Error::STORAGE);
    return;
  }

  Error error = Error::NONE;
  if (!verify_image(error)) {
    fail(error);
    return;
  }
  if (rename(m_tmp_path, m_bin_path) != 0) {
    fail(Error::STORAGE);
    return;
  }
  m_state = State::READY;
}

bool OtaCore::verify_image(Error &error) {
  // Mirrors the SFU bootloader, which looks for its signature string within
  // individual 256-byte flash pages of the first 64 KB.
  static constexpr size_t PAGE_SIZE = 256;
  static constexpr char SFU_SIGNATURE[] = "SFU version";
  static constexpr size_t MARKER_SIZE = MARKER_MAGIC_SIZE + 3;
  static constexpr size_t CARRY_SIZE = MARKER_SIZE - 1;

  FILE *file = fopen(m_tmp_path, "rb");
  if (file == nullptr) {
    error = Error::STORAGE;
    return false;
  }

  uint8_t buffer[CARRY_SIZE + PAGE_SIZE];
  size_t carry = 0;
  uint32_t offset = 0;
  bool sfu_found = false;
  bool marker_found = false;
  Version marker_version = {0, 0, 0};
  Sha256 sha;

  while (true) {
    const size_t read = fread(buffer + carry, 1, PAGE_SIZE, file);
    if (read == 0) {
      break;
    }
    sha.update(buffer + carry, read);

    if (!sfu_found && offset < SFU_SIZE &&
        find_bytes(buffer + carry, read,
                   reinterpret_cast<const uint8_t *>(SFU_SIGNATURE),
                   sizeof(SFU_SIGNATURE) - 1) != nullptr) {
      sfu_found = true;
    }

    const size_t window = carry + read;
    const uint32_t window_offset = offset - carry;
    for (size_t i = 0; !marker_found && i + MARKER_SIZE <= window; i++) {
      if (window_offset + i >= SFU_SIZE &&
          memcmp(buffer + i, m_marker_magic, MARKER_MAGIC_SIZE) == 0) {
        marker_found = true;
        marker_version = {buffer[i + 8], buffer[i + 9], buffer[i + 10]};
      }
    }

    offset += read;
    carry = window < CARRY_SIZE ? window : CARRY_SIZE;
    memmove(buffer, buffer + window - carry, carry);
  }

  const bool read_ok = ferror(file) == 0;
  fclose(file);

  uint8_t digest[Sha256::DIGEST_SIZE];
  sha.finish(digest);

  if (!read_ok || offset != m_total) {
    error = Error::STORAGE;
    return false;
  }
  if (!constant_time_equal(digest, m_image_hash, sizeof(digest))) {
    error = Error::HASH;
    return false;
  }
  if (!sfu_found) {
    error = Error::NO_SFU;
    return false;
  }
  if (!marker_found ||
      compare_versions(marker_version, m_image_version) != 0) {
    error = Error::MARKER;
    return false;
  }
  return true;
}

void OtaCore::get_info(uint8_t out[INFO_SIZE]) const {
  out[0] = PROTOCOL_VERSION;
  out[1] = m_current.major;
  out[2] = m_current.minor;
  out[3] = m_current.patch;
  memcpy(out + 4, m_nonce, NONCE_SIZE);
}

void OtaCore::get_status(uint8_t out[STATUS_SIZE]) const {
  out[0] = static_cast<uint8_t>(m_state);
  out[1] = static_cast<uint8_t>(m_error);
  write_u32_le(out + 2, m_received);
  write_u32_le(out + 6, m_total);
}

uint8_t OtaCore::progress_percent() const {
  if (m_total == 0) {
    return 0;
  }
  return static_cast<uint8_t>((static_cast<uint64_t>(m_received) * 100) /
                              m_total);
}

const char *OtaCore::error_name(Error error) {
  switch (error) {
  case Error::NONE:
    return "NONE";
  case Error::BAD_COMMAND:
    return "BAD_COMMAND";
  case Error::AUTH:
    return "AUTH";
  case Error::VERSION:
    return "VERSION";
  case Error::SIZE:
    return "SIZE";
  case Error::STORAGE:
    return "STORAGE";
  case Error::SEQUENCE:
    return "SEQUENCE";
  case Error::HASH:
    return "HASH";
  case Error::NO_SFU:
    return "NO_SFU";
  case Error::MARKER:
    return "MARKER";
  case Error::STATE:
    return "STATE";
  case Error::TIMEOUT:
    return "TIMEOUT";
  case Error::ABORTED:
    return "ABORTED";
  }
  return "UNKNOWN";
}
