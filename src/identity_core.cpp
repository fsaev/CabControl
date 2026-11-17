#include "identity_core.h"
#include "sha256.h"

#include <stdio.h>
#include <string.h>

IdentityCore::IdentityCore(const uint8_t *secret, size_t secret_len,
                           const char *dir, RandomFn random)
    : m_secret(secret), m_secret_len(secret_len), m_random(random) {
  snprintf(m_txt_path, sizeof(m_txt_path), "%s/NAME.TXT", dir);
  snprintf(m_tmp_path, sizeof(m_tmp_path), "%s/NAME.TMP", dir);
  snprintf(m_name, sizeof(m_name), "%s", DEFAULT_NAME);
}

bool IdentityCore::is_valid_name(const char *name, size_t len) {
  if (len == 0 || len > NAME_MAX_LEN || name[0] == ' ' ||
      name[len - 1] == ' ') {
    return false;
  }
  for (size_t i = 0; i < len; i++) {
    if (name[i] < 0x20 || name[i] > 0x7E) {
      return false;
    }
  }
  return true;
}

bool IdentityCore::read_name(const char *path,
                             char out[NAME_MAX_LEN + 1]) const {
  FILE *file = fopen(path, "rb");
  if (file == nullptr) {
    return false;
  }
  char buffer[NAME_MAX_LEN + 2] = {};
  const size_t len = fread(buffer, 1, sizeof(buffer) - 1, file);
  fclose(file);
  if (!is_valid_name(buffer, len)) {
    return false;
  }
  memcpy(out, buffer, len);
  out[len] = '\0';
  return true;
}

void IdentityCore::load() {
  // NAME.TMP only survives when a save was interrupted after NAME.TXT was
  // removed, so it holds the newest name.
  if (!read_name(m_txt_path, m_name) && !read_name(m_tmp_path, m_name)) {
    snprintf(m_name, sizeof(m_name), "%s", DEFAULT_NAME);
  }
}

bool IdentityCore::save(const char *name) {
  FILE *file = fopen(m_tmp_path, "wb");
  if (file == nullptr) {
    return false;
  }
  const size_t len = strlen(name);
  const bool written = fwrite(name, 1, len, file) == len;
  if (fclose(file) != 0 || !written) {
    remove(m_tmp_path);
    return false;
  }
  // FAT cannot rename onto an existing file, so NAME.TXT goes first. Either
  // step can fail, so check what load() would read back: NAME.TXT if present,
  // else NAME.TMP. A stale NAME.TXT means the save did not take effect.
  remove(m_txt_path);
  rename(m_tmp_path, m_txt_path);
  char stored[NAME_MAX_LEN + 1];
  const bool installed =
      (read_name(m_txt_path, stored) || read_name(m_tmp_path, stored)) &&
      strcmp(stored, name) == 0;
  if (!installed) {
    remove(m_tmp_path);
  }
  return installed;
}

void IdentityCore::new_nonce() { m_random(m_nonce, sizeof(m_nonce)); }

void IdentityCore::reset() {
  m_body_len = 0;
  m_result = Result::NONE;
  new_nonce();
}

void IdentityCore::get_auth(uint8_t out[AUTH_SIZE]) const {
  out[0] = PROTOCOL_VERSION;
  out[1] = static_cast<uint8_t>(m_result);
  memcpy(out + 2, m_nonce, NONCE_SIZE);
}

bool IdentityCore::handle_set(const uint8_t *data, size_t len) {
  if (len <= SET_HEADER_SIZE) {
    m_body_len = 0;
    m_result = Result::BAD_COMMAND;
    new_nonce();
    return false;
  }
  const size_t fragment_offset = data[0];
  const size_t fragment_len = len - SET_HEADER_SIZE;
  if (fragment_offset == 0) {
    m_body_len = 0;
  }
  if (fragment_offset != m_body_len ||
      fragment_len > SET_BODY_SIZE - m_body_len) {
    m_body_len = 0;
    m_result = Result::BAD_COMMAND;
    new_nonce();
    return false;
  }
  memcpy(m_body + m_body_len, data + SET_HEADER_SIZE, fragment_len);
  m_body_len += fragment_len;
  if (m_body_len < SET_BODY_SIZE) {
    return false;
  }
  m_body_len = 0;
  const bool changed = apply(m_body);
  new_nonce();
  return changed;
}

bool IdentityCore::apply(const uint8_t *body) {
  const size_t label_len = strlen(AUTH_LABEL);
  uint8_t message[32 + NONCE_SIZE + SET_SIGNED_SIZE];
  memcpy(message, AUTH_LABEL, label_len);
  memcpy(message + label_len, m_nonce, NONCE_SIZE);
  memcpy(message + label_len + NONCE_SIZE, body, SET_SIGNED_SIZE);
  uint8_t expected[Sha256::DIGEST_SIZE];
  hmac_sha256(m_secret, m_secret_len, message,
              label_len + NONCE_SIZE + SET_SIGNED_SIZE, expected);
  if (!constant_time_equal(expected, body + SET_SIGNED_SIZE,
                           Sha256::DIGEST_SIZE)) {
    m_result = Result::AUTH;
    return false;
  }

  const size_t len = body[0];
  const char *name = reinterpret_cast<const char *>(body + 1);
  if (!is_valid_name(name, len)) {
    m_result = Result::INVALID;
    return false;
  }
  for (size_t i = len; i < NAME_MAX_LEN; i++) {
    if (body[1 + i] != 0) {
      m_result = Result::INVALID;
      return false;
    }
  }

  char new_name[NAME_MAX_LEN + 1];
  memcpy(new_name, name, len);
  new_name[len] = '\0';
  if (strcmp(new_name, m_name) == 0) {
    m_result = Result::OK;
    return false;
  }
  if (!save(new_name)) {
    m_result = Result::STORAGE;
    return false;
  }
  memcpy(m_name, new_name, len + 1);
  m_result = Result::OK;
  return true;
}
