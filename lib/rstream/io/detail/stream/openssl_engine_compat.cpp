// See LICENSE file in the project root for license information.

#include "openssl_engine_compat.hpp"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

namespace rstream {
namespace io {
namespace detail {
namespace stream {
namespace openssl_engine_compat {

ENGINE* by_id(const char* id)
{
  return ::ENGINE_by_id(id);
}

int ctrl(ENGINE* engine, int command, long value, void* data, void (*callback)(void))
{
  return ::ENGINE_ctrl(engine, command, value, data, callback);
}

int ctrl_cmd(ENGINE* engine, const char* command, long value, void* data, void (*callback)(void), int optional)
{
  return ::ENGINE_ctrl_cmd(engine, command, value, data, callback, optional);
}

int finish(ENGINE* engine)
{
  return ::ENGINE_finish(engine);
}

int free(ENGINE* engine)
{
  return ::ENGINE_free(engine);
}

int init(ENGINE* engine)
{
  return ::ENGINE_init(engine);
}

EVP_PKEY* load_private_key(ENGINE* engine, const char* key_id, UI_METHOD* ui_method, void* callback_data)
{
  return ::ENGINE_load_private_key(engine, key_id, ui_method, callback_data);
}

}  // namespace openssl_engine_compat
}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
