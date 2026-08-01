// See LICENSE file in the project root for license information.

#pragma once

#include <openssl/engine.h>

namespace rstream {
namespace io {
namespace detail {
namespace stream {
namespace openssl_engine_compat {

ENGINE* by_id(const char* id);

int ctrl(ENGINE* engine, int command, long value, void* data, void (*callback)(void));

int ctrl_cmd(ENGINE* engine, const char* command, long value, void* data, void (*callback)(void), int optional);

int finish(ENGINE* engine);

int free(ENGINE* engine);

int init(ENGINE* engine);

EVP_PKEY* load_private_key(ENGINE* engine, const char* key_id, UI_METHOD* ui_method, void* callback_data);

}  // namespace openssl_engine_compat
}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
