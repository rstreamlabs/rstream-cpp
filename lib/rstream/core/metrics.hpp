// See LICENSE file in the project root for license information.

#pragma once

#include "detail/metrics/builder.hpp"
#include "detail/metrics/collectable.hpp"
#include "detail/metrics/counter.hpp"
#include "detail/metrics/gauge.hpp"
#include "detail/metrics/histogram.hpp"
#include "detail/metrics/info.hpp"
#include "detail/metrics/registry.hpp"
#include "detail/metrics/serializer.hpp"
#include "detail/metrics/summary.hpp"

namespace rstream {
namespace core {
namespace metrics {

template <class T>
using builder     = detail::metrics::builder<T>;
using collectable = detail::metrics::collectable;
using counter     = detail::metrics::counter;
using gauge       = detail::metrics::gauge;
using histogram   = detail::metrics::histogram;
using info        = detail::metrics::info;
using registry    = detail::metrics::registry;
using serializer  = detail::metrics::serializer;
using summary     = detail::metrics::summary;
registry::ptr default_registry();
collectable::ptr system_collector();

}  // namespace metrics
}  // namespace core
}  // namespace rstream
