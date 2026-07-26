// See LICENSE file in the project root for license information.

#include <cassert>
#include <limits>

#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/wrappers.pb.h>

#include <rstream/core/detail/protobuf.hpp>

static void check_supported_message_is_serialized()
{
  google::protobuf::BoolValue source;
  source.set_value(true);
  rstream::core::buffer buffer;
  assert(rstream::core::detail::serialize_protobuf_message(source, buffer));

  google::protobuf::BoolValue destination;
  auto memory = buffer.map(rstream::core::buffer::map_mode::read);
  assert(destination.ParseFromArray(memory.get_const_data(), static_cast<int>(memory.get_size())));
  assert(destination.value());
}

static void check_uninitialized_message_is_rejected()
{
  google::protobuf::UninterpretedOption_NamePart source;
  auto original = rstream::core::make_buffer_wrapped("unchanged", 9);
  auto output   = original;
  assert(!source.IsInitialized());
  assert(!rstream::core::detail::serialize_protobuf_message(source, output));
  assert(output.get_size() == original.get_size());
  assert(output.map(rstream::core::buffer::map_mode::read).get_const_data()
         == original.map(rstream::core::buffer::map_mode::read).get_const_data());
}

static void check_unsupported_size_is_rejected()
{
  const auto maximum = static_cast<std::size_t>(std::numeric_limits<int>::max());
  assert(rstream::core::detail::is_protobuf_buffer_size_valid(maximum));
  assert(!rstream::core::detail::is_protobuf_buffer_size_valid(maximum + 1));
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_supported_message_is_serialized();
  check_uninitialized_message_is_rejected();
  check_unsupported_size_is_rejected();
  return 0;
}
