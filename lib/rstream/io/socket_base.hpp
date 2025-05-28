// See LICENSE file in the project root for license information.

#pragma once

#include <boost/system/error_code.hpp>

#include "io_object.hpp"

namespace rstream {
namespace io {

template <class T>
class socket_base : public io_object {
 public:
  using endpoint_type = T;

  socket_base(const executor_type& executor);

  virtual ~socket_base() = default;

  virtual void open(const endpoint_type& endpoint, boost::system::error_code& error_code) = 0;  // TODO : Remove this method in future release

  void open(const endpoint_type& endpoint);  // TODO : Remove this method in future release

  virtual void close(boost::system::error_code& error_code) = 0;

  void close();
};

template <class T>
socket_base<T>::socket_base(const executor_type& executor)
    : io_object(executor)
{
}

template <class T>
void socket_base<T>::open(const endpoint_type& endpoint)
{
  boost::system::error_code error_code;
  open(endpoint, error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
}

template <class T>
void socket_base<T>::close()
{
  boost::system::error_code error_code;
  close(error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
}

}  // namespace io
}  // namespace rstream
