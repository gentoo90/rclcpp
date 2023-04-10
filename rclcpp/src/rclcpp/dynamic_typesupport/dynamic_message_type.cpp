// Copyright 2023 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <rosidl_dynamic_typesupport/api/dynamic_data.h>
#include <rosidl_dynamic_typesupport/api/dynamic_type.h>
#include <rosidl_dynamic_typesupport/api/serialization_support.h>
#include <rosidl_dynamic_typesupport/types.h>

#include <memory>
#include <string>

#include "rcutils/logging_macros.h"

#include "rclcpp/dynamic_typesupport/dynamic_message.hpp"
#include "rclcpp/dynamic_typesupport/dynamic_message_type.hpp"
#include "rclcpp/dynamic_typesupport/dynamic_message_type_builder.hpp"
#include "rclcpp/dynamic_typesupport/dynamic_serialization_support.hpp"
#include "rclcpp/exceptions.hpp"


using rclcpp::dynamic_typesupport::DynamicMessage;
using rclcpp::dynamic_typesupport::DynamicMessageType;
using rclcpp::dynamic_typesupport::DynamicMessageTypeBuilder;
using rclcpp::dynamic_typesupport::DynamicSerializationSupport;


// CONSTRUCTION ====================================================================================
DynamicMessageType::DynamicMessageType(DynamicMessageTypeBuilder::SharedPtr dynamic_type_builder)
: serialization_support_(dynamic_type_builder->get_shared_dynamic_serialization_support()),
  rosidl_dynamic_type_(nullptr)
{
  if (!serialization_support_) {
    throw std::runtime_error("dynamic type could not bind serialization support!");
  }

  rosidl_dynamic_typesupport_dynamic_type_builder_t * rosidl_dynamic_type_builder =
    dynamic_type_builder->get_rosidl_dynamic_type_builder();
  if (!rosidl_dynamic_type_builder) {
    throw std::runtime_error("dynamic type builder cannot be nullptr!");
  }

  rosidl_dynamic_typesupport_dynamic_type_t * rosidl_dynamic_type = nullptr;
  rcutils_ret_t ret = rosidl_dynamic_typesupport_dynamic_type_create_from_dynamic_type_builder(
    rosidl_dynamic_type_builder, &rosidl_dynamic_type);
  if (ret != RCUTILS_RET_OK || !rosidl_dynamic_type) {
    throw std::runtime_error("could not create new dynamic type object");
  }

  rosidl_dynamic_type_.reset(
    rosidl_dynamic_type,
    // Custom deleter
    [](rosidl_dynamic_typesupport_dynamic_type_t * rosidl_dynamic_type)->void {
      rosidl_dynamic_typesupport_dynamic_type_destroy(rosidl_dynamic_type);
    });
}


DynamicMessageType::DynamicMessageType(
  DynamicSerializationSupport::SharedPtr serialization_support,
  rosidl_dynamic_typesupport_dynamic_type_t * rosidl_dynamic_type)
: serialization_support_(serialization_support), rosidl_dynamic_type_(nullptr)
{
  if (!rosidl_dynamic_type) {
    throw std::runtime_error("rosidl dynamic type cannot be nullptr!");
  }
  if (serialization_support) {
    if (!match_serialization_support_(*serialization_support, *rosidl_dynamic_type)) {
      throw std::runtime_error(
              "serialization support library identifier does not match dynamic type's!");
    }
  }

  rosidl_dynamic_type_.reset(
    rosidl_dynamic_type,
    // Custom deleter
    [](rosidl_dynamic_typesupport_dynamic_type_t * rosidl_dynamic_type)->void {
      rosidl_dynamic_typesupport_dynamic_type_destroy(rosidl_dynamic_type);
    });
}


DynamicMessageType::DynamicMessageType(
  DynamicSerializationSupport::SharedPtr serialization_support,
  std::shared_ptr<rosidl_dynamic_typesupport_dynamic_type_t> rosidl_dynamic_type)
: serialization_support_(serialization_support), rosidl_dynamic_type_(rosidl_dynamic_type)
{
  if (!rosidl_dynamic_type) {
    throw std::runtime_error("rosidl dynamic type cannot be nullptr!");
  }
  if (serialization_support) {
    if (!match_serialization_support_(*serialization_support, *rosidl_dynamic_type)) {
      throw std::runtime_error(
              "serialization support library identifier does not match dynamic type's!");
    }
  }
}


DynamicMessageType::DynamicMessageType(
  DynamicSerializationSupport::SharedPtr serialization_support,
  const rosidl_runtime_c__type_description__TypeDescription & description)
: serialization_support_(serialization_support), rosidl_dynamic_type_(nullptr)
{
  init_from_description(description, serialization_support);
}


DynamicMessageType::DynamicMessageType(const DynamicMessageType & other)
: enable_shared_from_this(), serialization_support_(nullptr), rosidl_dynamic_type_(nullptr)
{
  DynamicMessageType out = other.clone();
  std::swap(serialization_support_, out.serialization_support_);
  std::swap(rosidl_dynamic_type_, out.rosidl_dynamic_type_);
}


DynamicMessageType::DynamicMessageType(DynamicMessageType && other) noexcept
: serialization_support_(std::exchange(other.serialization_support_, nullptr)),
  rosidl_dynamic_type_(std::exchange(other.rosidl_dynamic_type_, nullptr)) {}


DynamicMessageType &
DynamicMessageType::operator=(const DynamicMessageType & other)
{
  return *this = DynamicMessageType(other);
}


DynamicMessageType &
DynamicMessageType::operator=(DynamicMessageType && other) noexcept
{
  std::swap(serialization_support_, other.serialization_support_);
  std::swap(rosidl_dynamic_type_, other.rosidl_dynamic_type_);
  return *this;
}


DynamicMessageType::~DynamicMessageType() {}


void
DynamicMessageType::init_from_description(
  const rosidl_runtime_c__type_description__TypeDescription & description,
  DynamicSerializationSupport::SharedPtr serialization_support)
{
  if (serialization_support) {
    // Swap serialization support if serialization support is given
    serialization_support_ = serialization_support;
  }

  rosidl_dynamic_typesupport_dynamic_type_t * rosidl_dynamic_type = nullptr;
  rcutils_ret_t ret = rosidl_dynamic_typesupport_dynamic_type_create_from_description(
    &serialization_support_->get_rosidl_serialization_support(),
    &description,
    &rosidl_dynamic_type);
  if (ret != RCUTILS_RET_OK || !rosidl_dynamic_type) {
    throw std::runtime_error("could not create new dynamic type object");
  }

  rosidl_dynamic_type_.reset(
    rosidl_dynamic_type,
    // Custom deleter
    [](rosidl_dynamic_typesupport_dynamic_type_t * rosidl_dynamic_type)->void {
      rosidl_dynamic_typesupport_dynamic_type_destroy(rosidl_dynamic_type);
    });
}


bool
DynamicMessageType::match_serialization_support_(
  const DynamicSerializationSupport & serialization_support,
  const rosidl_dynamic_typesupport_dynamic_type_t & rosidl_dynamic_type)
{
  bool out = true;

  if (serialization_support.get_serialization_library_identifier() != std::string(
      rosidl_dynamic_type.serialization_support->serialization_library_identifier))
  {
    RCUTILS_LOG_ERROR(
      "serialization support library identifier does not match dynamic type's (%s vs %s)",
      serialization_support.get_serialization_library_identifier().c_str(),
      rosidl_dynamic_type.serialization_support->serialization_library_identifier);
    out = false;
  }

  return out;
}


// GETTERS =========================================================================================
const std::string
DynamicMessageType::get_serialization_library_identifier() const
{
  return std::string(rosidl_dynamic_type_->serialization_support->serialization_library_identifier);
}


const std::string
DynamicMessageType::get_name() const
{
  size_t buf_length;
  const char * buf;
  rosidl_dynamic_typesupport_dynamic_type_get_name(get_rosidl_dynamic_type(), &buf, &buf_length);
  return std::string(buf, buf_length);
}


size_t
DynamicMessageType::get_member_count() const
{
  size_t out;
  rcutils_ret_t ret = rosidl_dynamic_typesupport_dynamic_type_get_member_count(
    rosidl_dynamic_type_.get(), &out);
  if (ret != RCUTILS_RET_OK) {
    throw std::runtime_error(
            std::string("could not get member count: ") + rcl_get_error_string().str);
  }
  return out;
}


rosidl_dynamic_typesupport_dynamic_type_t *
DynamicMessageType::get_rosidl_dynamic_type()
{
  return rosidl_dynamic_type_.get();
}


const rosidl_dynamic_typesupport_dynamic_type_t *
DynamicMessageType::get_rosidl_dynamic_type() const
{
  return rosidl_dynamic_type_.get();
}


std::shared_ptr<rosidl_dynamic_typesupport_dynamic_type_t>
DynamicMessageType::get_shared_rosidl_dynamic_type()
{
  return std::shared_ptr<rosidl_dynamic_typesupport_dynamic_type_t>(
    shared_from_this(), rosidl_dynamic_type_.get());
}


std::shared_ptr<const rosidl_dynamic_typesupport_dynamic_type_t>
DynamicMessageType::get_shared_rosidl_dynamic_type() const
{
  return std::shared_ptr<rosidl_dynamic_typesupport_dynamic_type_t>(
    shared_from_this(), rosidl_dynamic_type_.get());
}


DynamicSerializationSupport::SharedPtr
DynamicMessageType::get_shared_dynamic_serialization_support()
{
  return serialization_support_;
}


DynamicSerializationSupport::ConstSharedPtr
DynamicMessageType::get_shared_dynamic_serialization_support() const
{
  return serialization_support_;
}


// METHODS =========================================================================================
DynamicMessageType
DynamicMessageType::clone() const
{
  rosidl_dynamic_typesupport_dynamic_type_t * rosidl_dynamic_type = nullptr;
  rcutils_ret_t ret = rosidl_dynamic_typesupport_dynamic_type_clone(
    get_rosidl_dynamic_type(), &rosidl_dynamic_type);
  if (ret != RCUTILS_RET_OK || !rosidl_dynamic_type) {
    throw std::runtime_error(
            std::string("could not clone dynamic type: ") + rcl_get_error_string().str);
  }
  return DynamicMessageType(serialization_support_, rosidl_dynamic_type);
}


DynamicMessageType::SharedPtr
DynamicMessageType::clone_shared() const
{
  rosidl_dynamic_typesupport_dynamic_type_t * rosidl_dynamic_type = nullptr;
  rcutils_ret_t ret = rosidl_dynamic_typesupport_dynamic_type_clone(
    get_rosidl_dynamic_type(), &rosidl_dynamic_type);
  if (ret != RCUTILS_RET_OK || !rosidl_dynamic_type) {
    throw std::runtime_error(
            std::string("could not clone dynamic type: ") + rcl_get_error_string().str);
  }
  return DynamicMessageType::make_shared(serialization_support_, rosidl_dynamic_type);
}


bool
DynamicMessageType::equals(const DynamicMessageType & other) const
{
  if (get_serialization_library_identifier() != other.get_serialization_library_identifier()) {
    throw std::runtime_error("library identifiers don't match");
  }
  bool out;
  rcutils_ret_t ret = rosidl_dynamic_typesupport_dynamic_type_equals(
    get_rosidl_dynamic_type(), other.get_rosidl_dynamic_type(), &out);
  if (ret != RCUTILS_RET_OK) {
    throw std::runtime_error(
            std::string("could not equate dynamic message types: ") + rcl_get_error_string().str);
  }
  return out;
}


DynamicMessage
DynamicMessageType::build_dynamic_message()
{
  return DynamicMessage(shared_from_this());
}


DynamicMessage::SharedPtr
DynamicMessageType::build_dynamic_message_shared()
{
  return DynamicMessage::make_shared(shared_from_this());
}
