// See LICENSE file in the project root for license information.

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "common.hpp"
#include "metric.hpp"
#include "registry.hpp"

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

template <typename T>
class wrapper;

class wrapper_common {
  friend class registry;
  template <typename T>
  friend class wrapper;

 public:
  using ptr                                                                             = std::shared_ptr<wrapper_common>;
  virtual ~wrapper_common()                                                             = default;
  virtual std::string name() const                                                      = 0;
  virtual std::string help() const                                                      = 0;
  virtual metric::type type() const                                                     = 0;
  virtual void get_labels(detail::metrics::labels& labels, bool recursive = true) const = 0;
  labels get_labels(bool recursive = true) const;
  virtual sample get_sample();
  virtual metric get_metric() = 0;

 protected:
  struct description {
    description(enum metric::type type, const std::string& name, const std::string& help);
    metric::type m_type;
    std::string m_name;
    std::string m_help;
  };
  static void register_metric(registry::ptr registry, ptr metric);

 private:
  virtual void deinit();
};

template <class T>
class wrapper_base : public wrapper_common {
 public:
  virtual T labels(const detail::metrics::labels& labels) = 0;
  virtual metric get_metric() override;
};

template <class T>
class wrapper : public wrapper_base<T> {
 public:
  std::string name() const override;
  std::string help() const override;
  metric::type type() const override;
  void get_labels(detail::metrics::labels& labels, bool recursive = true) const override;
  sample get_sample() override;
  metric get_metric() override;
  T labels(const detail::metrics::labels& labels) override;

 protected:
  class impl;
  class handle;
  template <class... Args>
  wrapper(const wrapper_common::description& description, const detail::metrics::labels& labels, Args&&... args);
  template <class... Args>
  wrapper(const wrapper_common::description& description, const detail::metrics::labels& labels, registry::ptr registry, Args&&... args);
  template <class... Args>
  wrapper(wrapper_common::ptr parent, const detail::metrics::labels& labels, Args&&... args);
  wrapper(std::shared_ptr<impl> impl);
  std::shared_ptr<wrapper_base<T>> get_base() const;
  std::shared_ptr<impl> get_impl() const;

 private:
  void deinit() override;
  const std::shared_ptr<impl> m_impl;
};

template <class T>
class wrapper<T>::handle : public wrapper_base<T> {
 public:
  std::string name() const override;
  std::string help() const override;
  metric::type type() const override;
  void get_labels(detail::metrics::labels& labels, bool recursive = true) const override;
  metric get_metric() override;
  T labels(const detail::metrics::labels& labels) override;

 protected:
  template <class... Args>
  handle(const wrapper_common::description& description, const detail::metrics::labels& labels, Args&&... args);
  handle(wrapper_common::ptr parent, const detail::metrics::labels& labels);

 private:
  class parent;
  class child;
  const std::shared_ptr<wrapper_base<T>> m_impl;
};

template <class T>
class wrapper<T>::handle::parent : public wrapper_base<T>, public std::enable_shared_from_this<parent> {
 public:
  template <class... Args>
  parent(const wrapper_common::description& description, const detail::metrics::labels& labels, Args&&... args);
  std::string name() const override;
  std::string help() const override;
  metric::type type() const override;
  void get_labels(detail::metrics::labels& labels, bool recursive = true) const override;
  metric get_metric() override;
  T labels(const detail::metrics::labels& labels) override;

 private:
  using childs = std::map<detail::metrics::labels, wrapper_common::ptr>;
  void deinit() override;
  const wrapper_common::description m_description;
  const detail::metrics::labels m_labels;
  const std::function<T(wrapper_common::ptr, const detail::metrics::labels&)> m_create_child_func;
  std::mutex m_mutex;
  childs m_childs;
};

template <class T>
class wrapper<T>::handle::child : public wrapper_base<T> {
 public:
  child(wrapper_common::ptr parent, const detail::metrics::labels& labels);
  std::string name() const override;
  std::string help() const override;
  metric::type type() const override;
  void get_labels(detail::metrics::labels& labels, bool recursive = true) const override;
  T labels(const detail::metrics::labels& labels) override;

 private:
  const wrapper_common::ptr m_parent;
  const detail::metrics::labels m_labels;
};

template <class T>
metric wrapper_base<T>::get_metric()
{
  metric metric{
      .m_name    = name(),
      .m_help    = help(),
      .m_type    = type(),
      .m_samples = {},
  };
  return metric;
}

template <class T>
template <class... Args>
wrapper<T>::wrapper(const wrapper_common::description& description, const detail::metrics::labels& labels, Args&&... args)
    : m_impl(std::make_shared<impl>(description, labels, std::forward<Args>(args)...))
{
}

template <class T>
template <class... Args>
wrapper<T>::wrapper(const wrapper_common::description& description, const detail::metrics::labels& labels, registry::ptr registry, Args&&... args)
    : wrapper(description, labels, std::forward<Args>(args)...)
{
  wrapper_common::register_metric(registry, m_impl);
}

template <class T>
template <class... Args>
wrapper<T>::wrapper(wrapper_common::ptr parent, const detail::metrics::labels& labels, Args&&... args)
    : m_impl(std::make_shared<impl>(parent, labels, std::forward<Args>(args)...))
{
}

template <class T>
wrapper<T>::wrapper(std::shared_ptr<impl> impl)
    : m_impl(impl)
{
}

template <class T>
std::string wrapper<T>::name() const
{
  return get_base()->name();
}

template <class T>
std::string wrapper<T>::help() const
{
  return get_base()->help();
}

template <class T>
metric::type wrapper<T>::type() const
{
  return get_base()->type();
}

template <class T>
void wrapper<T>::get_labels(detail::metrics::labels& labels, bool recursive) const
{
  return get_base()->get_labels(labels, recursive);
}

template <class T>
sample wrapper<T>::get_sample()
{
  return get_base()->get_sample();
}

template <class T>
metric wrapper<T>::get_metric()
{
  return get_base()->get_metric();
}

template <class T>
T wrapper<T>::labels(const detail::metrics::labels& labels)
{
  return get_base()->labels(labels);
}

template <class T>
std::shared_ptr<typename wrapper<T>::impl> wrapper<T>::get_impl() const
{
  return m_impl;
}

template <class T>
void wrapper<T>::deinit()
{
  return get_base()->deinit();
}

template <class T>
template <class... Args>
wrapper<T>::handle::handle(const wrapper_common::description& description, const detail::metrics::labels& labels, Args&&... args)
    : m_impl(std::make_shared<parent>(description, labels, std::forward<Args>(args)...))
{
}

template <class T>
wrapper<T>::handle::handle(wrapper_common::ptr parent, const detail::metrics::labels& labels)
    : m_impl(std::make_shared<child>(parent, labels))
{
}

template <class T>
std::string wrapper<T>::handle::name() const
{
  return m_impl->name();
}

template <class T>
std::string wrapper<T>::handle::help() const
{
  return m_impl->help();
}

template <class T>
metric::type wrapper<T>::handle::type() const
{
  return m_impl->type();
}

template <class T>
void wrapper<T>::handle::get_labels(detail::metrics::labels& labels, bool recursive) const
{
  return m_impl->get_labels(labels, recursive);
}

template <class T>
metric wrapper<T>::handle::get_metric()
{
  auto metric = m_impl->get_metric();

  metric.m_samples.emplace_front(this->get_sample());

  return metric;
}

template <class T>
T wrapper<T>::handle::labels(const detail::metrics::labels& labels)
{
  return m_impl->labels(labels);
}

template <class T>
template <class... Args>
wrapper<T>::handle::parent::parent(const wrapper_common::description& description, const detail::metrics::labels& labels, Args&&... args)
    : m_description(description),
      m_labels(labels),
      m_create_child_func([... args = std::forward<Args>(args)](wrapper_common::ptr parent, const detail::metrics::labels& labels) { return T(parent, labels, std::forward<Args>(args)...); })
{
  check_metric_name(m_description.m_name);
  check_label_name(m_labels);
}

template <class T>
std::string wrapper<T>::handle::parent::name() const
{
  return m_description.m_name;
}

template <class T>
std::string wrapper<T>::handle::parent::help() const
{
  return m_description.m_help;
}

template <class T>
metric::type wrapper<T>::handle::parent::type() const
{
  return m_description.m_type;
}

template <class T>
void wrapper<T>::handle::parent::get_labels(detail::metrics::labels& labels, bool recursive) const
{
  if (recursive) {
    labels.insert(m_labels.cbegin(), m_labels.cend());
  }
}

template <class T>
metric wrapper<T>::handle::parent::get_metric()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  metric metric = wrapper_base<T>::get_metric();
  for (const auto& child : m_childs) {
    metric.m_samples.emplace_back(child.second->get_sample());
  }
  return metric;
}

template <class T>
T wrapper<T>::handle::parent::labels(const detail::metrics::labels& labels)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  {
    auto it = m_childs.find(labels);
    if (it != m_childs.end()) {
      return T(std::dynamic_pointer_cast<impl>(it->second));
    }
  }
  check_label_name_overlap(labels, m_labels);
  auto child = m_create_child_func(this->shared_from_this(), labels);
  m_childs.insert(std::make_pair(labels, std::dynamic_pointer_cast<wrapper_common>(child.m_impl)));
  return child;
}

template <class T>
void wrapper<T>::handle::parent::deinit()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_childs.clear();
}

template <class T>
wrapper<T>::handle::child::child(wrapper_common::ptr parent, const detail::metrics::labels& labels)
    : m_parent(parent),
      m_labels(labels)
{
  check_label_name(m_labels);
}

template <class T>
std::string wrapper<T>::handle::child::name() const
{
  return m_parent->name();
}

template <class T>
std::string wrapper<T>::handle::child::help() const
{
  return m_parent->help();
}

template <class T>
metric::type wrapper<T>::handle::child::type() const
{
  return m_parent->type();
}

template <class T>
void wrapper<T>::handle::child::get_labels(detail::metrics::labels& labels, bool recursive) const
{
  if (recursive) {
    m_parent->get_labels(labels, recursive);
  }
  labels.insert(m_labels.cbegin(), m_labels.cend());
}

template <class T>
T wrapper<T>::handle::child::labels(const detail::metrics::labels& labels)
{
  auto tmp = m_labels;
  check_label_name_overlap(labels, tmp);
  m_parent->get_labels(tmp);
  return std::dynamic_pointer_cast<wrapper_base<T>>(m_parent)->labels(tmp);
}

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
