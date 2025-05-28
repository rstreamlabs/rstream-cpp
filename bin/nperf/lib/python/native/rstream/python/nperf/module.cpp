// See LICENSE file in the project root for license information.

#include <boost/python.hpp>

#include "client.hpp"
#include "nperf.hpp"

BOOST_PYTHON_MODULE(nperf)
{
  boost::python::enum_<rstream::nperf::protocol>("protocol")
      .value("websocket", rstream::nperf::protocol::websocket)
      .value("plain", rstream::nperf::protocol::plain);
  boost::python::class_<rstream::nperf::client::config>("client_config")
      .def_readwrite("address", &rstream::nperf::client::config::m_address);
  boost::python::class_<rstream::nperf::settings>("settings")
      .def_readwrite("buffer_size", &rstream::nperf::settings::m_buffer_size)
      .def_readwrite("timeouts_max_time_ms", &rstream::nperf::settings::m_timeouts_max_time_ms)
      .def_readwrite("timeouts_open_close_ms", &rstream::nperf::settings::m_timeouts_open_close_ms)
      .def_readwrite("protocol", &rstream::nperf::settings::m_protocol);
  boost::python::class_<rstream::nperf::settings_client>("settings_client")
      .def_readwrite("common", &rstream::nperf::settings_client::m_common)
      .def_readwrite("execution_count", &rstream::nperf::settings_client::m_execution_count)
      .def_readwrite("max_ping", &rstream::nperf::settings_client::m_max_ping)
      .def_readwrite("period_metrics_ms", &rstream::nperf::settings_client::m_period_metrics_ms)
      .def_readwrite("period_ms", &rstream::nperf::settings_client::m_period_ms)
      .def_readwrite("ping_buffer_size", &rstream::nperf::settings_client::m_ping_buffer_size)
      .def_readwrite("sessions", &rstream::nperf::settings_client::m_sessions)
      .def_readwrite("max_data_bytes", &rstream::nperf::settings_client::m_max_data_bytes)
      .def_readwrite("retry", &rstream::nperf::settings_client::m_retry);
  boost::python::enum_<rstream::nperf::sample::type>("sample_type")
      .value("connection", rstream::nperf::sample::type::connection)
      .value("handshake", rstream::nperf::sample::type::handshake)
      .value("ping", rstream::nperf::sample::type::ping);
  boost::python::class_<rstream::nperf::sample>("sample")
      .def_readwrite("type", &rstream::nperf::sample::m_type)
      .def_readwrite("size", &rstream::nperf::sample::m_size)
      .def_readwrite("min_us", &rstream::nperf::sample::m_min_us)
      .def_readwrite("max_us", &rstream::nperf::sample::m_max_us)
      .def_readwrite("mean_us", &rstream::nperf::sample::m_mean_us)
      .def_readwrite("stdev_us", &rstream::nperf::sample::m_stdev_us);
  boost::python::class_<rstream::nperf::speed>("speed")
      .def_readwrite("measured_bytes", &rstream::nperf::speed::m_measured_bytes)
      .def_readwrite("elapsed_time_ms", &rstream::nperf::speed::m_elapsed_time_ms)
      .def_readwrite("max_time_ms", &rstream::nperf::speed::m_max_time_ms);
  boost::python::class_<rstream::nperf::metrics>("metrics", boost::python::no_init)
      .def_readwrite("final", &rstream::nperf::metrics::m_final)
      .def_readwrite("options", &rstream::nperf::metrics::m_options)
      .add_property("timestamp", +[](const rstream::nperf::metrics& metrics) { return rstream::python::nperf::to_string(metrics.m_timestamp, "%Z %Y-%m-%d %H:%M:%S"); })
      .add_property("data", +[](const rstream::nperf::metrics& metrics) { return rstream::python::nperf::variant_wrapper<rstream::nperf::metrics::data>(metrics.m_data).get(); });
  boost::python::class_<rstream::python::nperf::client>("client", boost::python::init<const rstream::nperf::client::config&, const rstream::nperf::settings_client&, rstream::nperf::options, unsigned int>())
      .def("start", &rstream::python::nperf::client::start)
      .def("stop", &rstream::python::nperf::client::stop)
      .def("get", +[](rstream::python::nperf::client& client) { return rstream::python::nperf::variant_wrapper<rstream::python::nperf::client::data>(client.get()).get(); });
}
