// Copyright 2017 Open Source Robotics Foundation, Inc.
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

/**
 * This test checks all implementations of rclcpp::executor to check they pass they basic API
 * tests. Anything specific to any executor in particular should go in a separate test file.
 *
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "rcl/error_handling.h"
#include "rcl/time.h"
#include "rclcpp/clock.hpp"
#include "rclcpp/detail/add_guard_condition_to_rcl_wait_set.hpp"
#include "rclcpp/duration.hpp"
#include "rclcpp/guard_condition.hpp"
#include "rclcpp/rclcpp.hpp"

#include "test_msgs/msg/empty.hpp"

using namespace std::chrono_literals;


template<typename T>
class TestExecutorsOnlyNode : public ::testing::Test
{
public:
  void SetUp()
  {
    rclcpp::init(0, nullptr);

    const auto test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    std::stringstream test_name;
    test_name << test_info->test_case_name() << "_" << test_info->name();
    node = std::make_shared<rclcpp::Node>("node", test_name.str());

  }

  void TearDown()
  {
    node.reset();

    rclcpp::shutdown();
  }

  rclcpp::Node::SharedPtr node;
};

template<typename T>
class TestExecutors : public ::testing::Test
{
public:
  void SetUp()
  {
    rclcpp::init(0, nullptr);

    const auto test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    std::stringstream test_name;
    test_name << test_info->test_case_name() << "_" << test_info->name();
    node = std::make_shared<rclcpp::Node>("node", test_name.str());

    callback_count = 0;

    const std::string topic_name = std::string("topic_") + test_name.str();
    publisher = node->create_publisher<test_msgs::msg::Empty>(topic_name, rclcpp::QoS(10));
    auto callback = [this](test_msgs::msg::Empty::ConstSharedPtr) {this->callback_count++;};
    subscription =
      node->create_subscription<test_msgs::msg::Empty>(
      topic_name, rclcpp::QoS(10), std::move(callback));
  }

  void TearDown()
  {
    publisher.reset();
    subscription.reset();
    node.reset();

    rclcpp::shutdown();
  }

  rclcpp::Node::SharedPtr node;
  rclcpp::Publisher<test_msgs::msg::Empty>::SharedPtr publisher;
  rclcpp::Subscription<test_msgs::msg::Empty>::SharedPtr subscription;
  int callback_count;
};

template<typename T>
class TestExecutorsStable : public TestExecutors<T> {};

using ExecutorTypes =
  ::testing::Types<
  rclcpp::executors::SingleThreadedExecutor,
  rclcpp::executors::MultiThreadedExecutor,
  rclcpp::executors::StaticSingleThreadedExecutor,
  rclcpp::experimental::executors::EventsExecutor>;

class ExecutorTypeNames
{
public:
  template<typename T>
  static std::string GetName(int idx)
  {
    (void)idx;
    if (std::is_same<T, rclcpp::executors::SingleThreadedExecutor>()) {
      return "SingleThreadedExecutor";
    }

    if (std::is_same<T, rclcpp::executors::MultiThreadedExecutor>()) {
      return "MultiThreadedExecutor";
    }

    if (std::is_same<T, rclcpp::executors::StaticSingleThreadedExecutor>()) {
      return "StaticSingleThreadedExecutor";
    }

    if (std::is_same<T, rclcpp::experimental::executors::EventsExecutor>()) {
      return "EventsExecutor";
    }

    return "";
  }
};

// TYPED_TEST_SUITE is deprecated as of gtest 1.9, use TYPED_TEST_SUITE when gtest dependency
// is updated.
TYPED_TEST_SUITE(TestExecutors, ExecutorTypes, ExecutorTypeNames);

// Make sure that executors detach from nodes when destructing
TYPED_TEST(TestExecutors, detachOnDestruction)
{
  using ExecutorType = TypeParam;
  {
    ExecutorType executor;
    executor.add_node(this->node);
  }
  {
    ExecutorType executor;
    EXPECT_NO_THROW(executor.add_node(this->node));
  }
}

// Make sure that the executor can automatically remove expired nodes correctly
TYPED_TEST(TestExecutors, addTemporaryNode) {
  using ExecutorType = TypeParam;
  ExecutorType executor;

  {
    // Let node go out of scope before executor.spin()
    auto node = std::make_shared<rclcpp::Node>("temporary_node");
    executor.add_node(node);
  }

  // Sleep for a short time to verify executor.spin() is going, and didn't throw.
  std::thread spinner([&]() {EXPECT_NO_THROW(executor.spin());});

  std::this_thread::sleep_for(50ms);
  executor.cancel();
  spinner.join();
}

// Make sure that a spinning empty executor can be cancelled
TYPED_TEST(TestExecutors, emptyExecutor)
{
  using ExecutorType = TypeParam;
  ExecutorType executor;
  std::thread spinner([&]() {EXPECT_NO_THROW(executor.spin());});
  std::this_thread::sleep_for(50ms);
  executor.cancel();
  spinner.join();
}

// Check executor throws properly if the same node is added a second time
TYPED_TEST(TestExecutors, addNodeTwoExecutors)
{
  using ExecutorType = TypeParam;
  ExecutorType executor1;
  ExecutorType executor2;
  EXPECT_NO_THROW(executor1.add_node(this->node));
  EXPECT_THROW(executor2.add_node(this->node), std::runtime_error);
  executor1.remove_node(this->node, true);
}

// Check simple spin example
TYPED_TEST(TestExecutors, spinWithTimer)
{
  using ExecutorType = TypeParam;
  ExecutorType executor;

  bool timer_completed = false;
  auto timer = this->node->create_wall_timer(1ms, [&]() {timer_completed = true;});
  executor.add_node(this->node);

  std::thread spinner([&]() {executor.spin();});

  auto start = std::chrono::steady_clock::now();
  while (!timer_completed && (std::chrono::steady_clock::now() - start) < 10s) {
    std::this_thread::sleep_for(1ms);
  }

  EXPECT_TRUE(timer_completed);
  // Cancel needs to be called before join, so that executor.spin() returns.
  executor.cancel();
  spinner.join();
  executor.remove_node(this->node, true);
}

TYPED_TEST(TestExecutors, spinWhileAlreadySpinning)
{
  using ExecutorType = TypeParam;
  ExecutorType executor;
  executor.add_node(this->node);

  std::atomic_bool timer_completed = false;
  auto timer = this->node->create_wall_timer(
    1ms, [&]() {
      timer_completed.store(true);
    });

  std::thread spinner([&]() {executor.spin();});
  // Sleep for a short time to verify executor.spin() is going, and didn't throw.

  auto start = std::chrono::steady_clock::now();
  while (!timer_completed.load() && (std::chrono::steady_clock::now() - start) < 10s) {
    std::this_thread::sleep_for(1ms);
  }

  EXPECT_TRUE(timer_completed);
  EXPECT_THROW(executor.spin(), std::runtime_error);

  // Shutdown needs to be called before join, so that executor.spin() returns.
  executor.cancel();
  spinner.join();
  executor.remove_node(this->node, true);
}

// Check executor exits immediately if future is complete.
TYPED_TEST(TestExecutors, testSpinUntilFutureComplete)
{
  using ExecutorType = TypeParam;
  ExecutorType executor;
  executor.add_node(this->node);

  // test success of an immediately finishing future
  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();
  promise.set_value(true);

  // spin_until_future_complete is expected to exit immediately, but would block up until its
  // timeout if the future is not checked before spin_once_impl.
  auto start = std::chrono::steady_clock::now();
  auto shared_future = future.share();
  auto ret = executor.spin_until_future_complete(shared_future, 1s);
  executor.remove_node(this->node, true);
  // Check it didn't reach timeout
  EXPECT_GT(500ms, (std::chrono::steady_clock::now() - start));
  EXPECT_EQ(rclcpp::FutureReturnCode::SUCCESS, ret);
}

// Same test, but uses a shared future.
TYPED_TEST(TestExecutors, testSpinUntilSharedFutureComplete)
{
  using ExecutorType = TypeParam;
  ExecutorType executor;
  executor.add_node(this->node);

  // test success of an immediately finishing future
  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();
  promise.set_value(true);

  // spin_until_future_complete is expected to exit immediately, but would block up until its
  // timeout if the future is not checked before spin_once_impl.
  auto shared_future = future.share();
  auto start = std::chrono::steady_clock::now();
  auto ret = executor.spin_until_future_complete(shared_future, 1s);
  executor.remove_node(this->node, true);

  // Check it didn't reach timeout
  EXPECT_GT(500ms, (std::chrono::steady_clock::now() - start));
  EXPECT_EQ(rclcpp::FutureReturnCode::SUCCESS, ret);
}

// For a longer running future that should require several iterations of spin_once
TYPED_TEST(TestExecutors, testSpinUntilFutureCompleteNoTimeout)
{
  using ExecutorType = TypeParam;
  ExecutorType executor;
  executor.add_node(this->node);

  // This future doesn't immediately terminate, so some work gets performed.
  std::future<void> future = std::async(
    std::launch::async,
    [this]() {
      auto start = std::chrono::steady_clock::now();
      while (this->callback_count < 1 && (std::chrono::steady_clock::now() - start) < 1s) {
        std::this_thread::sleep_for(1ms);
      }
    });

  bool spin_exited = false;

  // Timeout set to negative for no timeout.
  std::thread spinner([&]() {
      auto ret = executor.spin_until_future_complete(future, -1s);
      EXPECT_EQ(rclcpp::FutureReturnCode::SUCCESS, ret);
      executor.remove_node(this->node, true);
      executor.cancel();
      spin_exited = true;
    });

  // Do some work for longer than the future needs.
  for (int i = 0; i < 100; ++i) {
    this->publisher->publish(test_msgs::msg::Empty());
    std::this_thread::sleep_for(1ms);
    if (spin_exited) {
      break;
    }
  }

  // Not testing accuracy, just want to make sure that some work occurred.
  EXPECT_LT(0, this->callback_count);

  // If this fails, the test will probably crash because spinner goes out of scope while the thread
  // is active. However, it beats letting this run until the gtest timeout.
  ASSERT_TRUE(spin_exited);
  executor.cancel();
  spinner.join();
}

// Check spin_until_future_complete timeout works as expected
TYPED_TEST(TestExecutors, testSpinUntilFutureCompleteWithTimeout)
{
  using ExecutorType = TypeParam;
  ExecutorType executor;
  executor.add_node(this->node);

  bool spin_exited = false;

  // Needs to run longer than spin_until_future_complete's timeout.
  std::future<void> future = std::async(
    std::launch::async,
    [&spin_exited]() {
      auto start = std::chrono::steady_clock::now();
      while (!spin_exited && (std::chrono::steady_clock::now() - start) < 1s) {
        std::this_thread::sleep_for(1ms);
      }
    });

  // Short timeout
  std::thread spinner([&]() {
      auto ret = executor.spin_until_future_complete(future, 1ms);
      EXPECT_EQ(rclcpp::FutureReturnCode::TIMEOUT, ret);
      executor.remove_node(this->node, true);
      spin_exited = true;
    });

  // Do some work for longer than timeout needs.
  for (int i = 0; i < 100; ++i) {
    this->publisher->publish(test_msgs::msg::Empty());
    std::this_thread::sleep_for(1ms);
    if (spin_exited) {
      break;
    }
  }

  EXPECT_TRUE(spin_exited);
  spinner.join();
}

class TestWaitable : public rclcpp::Waitable
{
public:
  TestWaitable() = default;

  void
  add_to_wait_set(rcl_wait_set_t * wait_set) override
  {
    rclcpp::detail::add_guard_condition_to_rcl_wait_set(*wait_set, gc_);
  }

  void trigger()
  {
    gc_.trigger();
  }

  bool
  is_ready(rcl_wait_set_t * wait_set) override
  {
    for (size_t i = 0; i < wait_set->size_of_guard_conditions; ++i) {
      if (&gc_.get_rcl_guard_condition() == wait_set->guard_conditions[i]) {
        is_ready_called_before_take_data = true;
        return true;
      }
    }
    return false;
  }

  std::shared_ptr<void>
  take_data() override
  {
    if (!is_ready_called_before_take_data) {
      throw std::runtime_error(
              "TestWaitable : Internal error, take data was called, but is_ready was not called before");
    }

    is_ready_called_before_take_data = false;
    return nullptr;
  }

  std::shared_ptr<void>
  take_data_by_entity_id(size_t id) override
  {
    (void) id;
    return nullptr;
  }

  void
  execute(std::shared_ptr<void> & data) override
  {
    (void) data;
    count_++;
    std::this_thread::sleep_for(3ms);
    try {
      std::lock_guard<std::mutex> lock(execute_promise_mutex_);
      execute_promise_.set_value();
    } catch (const std::future_error & future_error) {
      if (future_error.code() != std::future_errc::promise_already_satisfied) {
        throw;
      }
    }
  }

  void
  set_on_ready_callback(std::function<void(size_t, int)> callback) override
  {
    auto gc_callback = [callback](size_t count) {
        callback(count, 0);
      };
    gc_.set_on_trigger_callback(gc_callback);
  }

  void
  clear_on_ready_callback() override
  {
    gc_.set_on_trigger_callback(nullptr);
  }

  size_t
  get_number_of_ready_guard_conditions() override {return 1;}

  size_t
  get_count()
  {
    return count_;
  }

  std::future<void>
  reset_execute_promise_and_get_future()
  {
    std::lock_guard<std::mutex> lock(execute_promise_mutex_);
    execute_promise_ = std::promise<void>();
    return execute_promise_.get_future();
  }

private:
  bool is_ready_called_before_take_data = false;
  std::promise<void> execute_promise_;
  std::mutex execute_promise_mutex_;
  size_t count_ = 0;
  rclcpp::GuardCondition gc_;
};


TYPED_TEST(TestExecutors, spinAll)
{
  using ExecutorType = TypeParam;
  ExecutorType executor;
  auto waitable_interfaces = this->node->get_node_waitables_interface();
  auto my_waitable = std::make_shared<TestWaitable>();
  waitable_interfaces->add_waitable(my_waitable, nullptr);
  executor.add_node(this->node);

  // Long timeout, but should not block test if spin_all works as expected as we cancel the
  // executor.
  bool spin_exited = false;
  std::thread spinner([&spin_exited, &executor, this]() {
      executor.spin_all(1s);
      executor.remove_node(this->node, true);
      spin_exited = true;
    });

  // Do some work until sufficient calls to the waitable occur
  auto start = std::chrono::steady_clock::now();
  while (
    my_waitable->get_count() <= 1 &&
    !spin_exited &&
    (std::chrono::steady_clock::now() - start < 1s))
  {
    my_waitable->trigger();
    this->publisher->publish(test_msgs::msg::Empty());
    std::this_thread::sleep_for(1ms);
  }

  executor.cancel();
  start = std::chrono::steady_clock::now();
  while (!spin_exited && (std::chrono::steady_clock::now() - start) < 1s) {
    std::this_thread::sleep_for(1ms);
  }

  EXPECT_LT(1u, my_waitable->get_count());
  waitable_interfaces->remove_waitable(my_waitable, nullptr);
  ASSERT_TRUE(spin_exited);
  spinner.join();
}

TEST(TestExecutorsOnlyNode, double_take_data)
{
  rclcpp::init(0, nullptr);

  const auto test_info = ::testing::UnitTest::GetInstance()->current_test_info();
  std::stringstream test_name;
  test_name << test_info->test_case_name() << "_" << test_info->name();
  rclcpp::Node::SharedPtr node = std::make_shared<rclcpp::Node>("node", test_name.str());

  class MyExecutor : public rclcpp::executors::SingleThreadedExecutor
  {
public:
    /**
     * This is a copy of Executor::get_next_executable with a callback, to test
     * for a special race condition
     */
    bool get_next_executable_with_callback(
      rclcpp::AnyExecutable & any_executable,
      std::chrono::nanoseconds timeout,
      std::function<void(void)> inbetween)
    {
      bool success = false;
      // Check to see if there are any subscriptions or timers needing service
      // TODO(wjwwood): improve run to run efficiency of this function
      success = get_next_ready_executable(any_executable);
      // If there are none
      if (!success) {

        inbetween();

        // Wait for subscriptions or timers to work on
        wait_for_work(timeout);
        if (!spinning.load()) {
          return false;
        }
        // Try again
        success = get_next_ready_executable(any_executable);
      }
      return success;
    }

    void spin_once_with_callback(
      std::chrono::nanoseconds timeout,
      std::function<void(void)> inbetween)
    {
      rclcpp::AnyExecutable any_exec;
      if (get_next_executable_with_callback(any_exec, timeout, inbetween)) {
        execute_any_executable(any_exec);
      }
    }

  };

  MyExecutor executor;

  auto callback_group = node->create_callback_group(
    rclcpp::CallbackGroupType::MutuallyExclusive,
    true);

  std::vector<std::shared_ptr<TestWaitable>> waitables;

  auto waitable_interfaces = node->get_node_waitables_interface();

  for (int i = 0; i < 3; i++) {
    auto waitable = std::make_shared<TestWaitable>();
    waitables.push_back(waitable);
    waitable_interfaces->add_waitable(waitable, callback_group);
  }
  executor.add_node(node);

  for (auto & waitable : waitables) {
    waitable->trigger();
  }

  // a node has some default subscribers, that need to get executed first, therefore the loop
  for (int i = 0; i < 10; i++) {
    executor.spin_once(std::chrono::milliseconds(10));
    if (waitables.front()->get_count() > 0) {
      // stop execution, after the first waitable has been executed
      break;
    }
  }

  EXPECT_EQ(waitables.front()->get_count(), 1);

  // block the callback group, this is something that may happen during multi threaded execution
  // This removes my_waitable2 from the list of ready events, and triggers a call to wait_for_work
  callback_group->can_be_taken_from().exchange(false);

  bool no_ready_executable = false;

  //now there should be no ready events now,
  executor.spin_once_with_callback(
    std::chrono::milliseconds(10), [&]() {
      no_ready_executable = true;
    });

  EXPECT_TRUE(no_ready_executable);

  //rearm, so that rmw_wait will push a second entry into the queue
  for (auto & waitable : waitables) {
    waitable->trigger();
  }

  no_ready_executable = false;

  while (!no_ready_executable) {
    executor.spin_once_with_callback(
      std::chrono::milliseconds(10), [&]() {
        //unblock the callback group
        callback_group->can_be_taken_from().exchange(true);

        no_ready_executable = true;

      });
  }
  EXPECT_TRUE(no_ready_executable);

  // now we process all events from get_next_ready_executable
  EXPECT_NO_THROW(
    for (int i = 0; i < 10; i++) {
    executor.spin_once(std::chrono::milliseconds(1));
  }
  );

  node.reset();

  rclcpp::shutdown();
}


TYPED_TEST(TestExecutorsOnlyNode, missing_event)
{
  using ExecutorType = TypeParam;
  ExecutorType executor;

  rclcpp::Node::SharedPtr node(this->node);
  auto callback_group = node->create_callback_group(
    rclcpp::CallbackGroupType::MutuallyExclusive,
    false);

  std::chrono::seconds max_spin_duration(2);
  auto waitable_interfaces = node->get_node_waitables_interface();
  auto my_waitable = std::make_shared<TestWaitable>();
  auto my_waitable2 = std::make_shared<TestWaitable>();
  waitable_interfaces->add_waitable(my_waitable, callback_group);
  waitable_interfaces->add_waitable(my_waitable2, callback_group);
  executor.add_callback_group(callback_group, node->get_node_base_interface());

  my_waitable->trigger();
  my_waitable2->trigger();

  {
    auto my_waitable_execute_future = my_waitable->reset_execute_promise_and_get_future();
    executor.spin_until_future_complete(my_waitable_execute_future, max_spin_duration);
  }

  EXPECT_EQ(1u, my_waitable->get_count());
  EXPECT_EQ(0u, my_waitable2->get_count());

  // block the callback group, this is something that may happen during multi threaded execution
  // This removes my_waitable2 from the list of ready events, and triggers a call to wait_for_work
  callback_group->can_be_taken_from().exchange(false);

  // now there should be no ready event
  {
    auto my_waitable2_execute_future = my_waitable2->reset_execute_promise_and_get_future();
    auto future_code = executor.spin_until_future_complete(
      my_waitable2_execute_future,
      std::chrono::milliseconds(100));  // expected to timeout
    EXPECT_EQ(future_code, rclcpp::FutureReturnCode::TIMEOUT);
  }

  EXPECT_EQ(1u, my_waitable->get_count());
  EXPECT_EQ(0u, my_waitable2->get_count());

  // unblock the callback group
  callback_group->can_be_taken_from().exchange(true);

  // now the second waitable should get processed
  {
    auto my_waitable2_execute_future = my_waitable2->reset_execute_promise_and_get_future();
    executor.spin_until_future_complete(my_waitable2_execute_future, max_spin_duration);
  }

  EXPECT_EQ(1u, my_waitable->get_count());
  EXPECT_EQ(1u, my_waitable2->get_count());
}

TYPED_TEST(TestExecutors, spinSome)
{
  using ExecutorType = TypeParam;
  ExecutorType executor;
  auto waitable_interfaces = this->node->get_node_waitables_interface();
  auto my_waitable = std::make_shared<TestWaitable>();
  waitable_interfaces->add_waitable(my_waitable, nullptr);
  executor.add_node(this->node);

  // Long timeout, doesn't block test from finishing because spin_some should exit after the
  // first one completes.
  bool spin_exited = false;
  std::thread spinner([&spin_exited, &executor, this]() {
      executor.spin_some(1s);
      executor.remove_node(this->node, true);
      spin_exited = true;
    });

  // Do some work until sufficient calls to the waitable occur, but keep going until either
  // count becomes too large, spin exits, or the 1 second timeout completes.
  auto start = std::chrono::steady_clock::now();
  while (
    my_waitable->get_count() <= 1 &&
    !spin_exited &&
    (std::chrono::steady_clock::now() - start < 1s))
  {
    my_waitable->trigger();
    this->publisher->publish(test_msgs::msg::Empty());
    std::this_thread::sleep_for(1ms);
  }
  // The count of "execute" depends on whether the executor starts spinning before (1) or after (0)
  // the first iteration of the while loop
  EXPECT_LE(1u, my_waitable->get_count());
  waitable_interfaces->remove_waitable(my_waitable, nullptr);
  EXPECT_TRUE(spin_exited);
  // Cancel if it hasn't exited already.
  executor.cancel();

  spinner.join();
}

// Check spin_node_until_future_complete with node base pointer
TYPED_TEST(TestExecutors, testSpinNodeUntilFutureCompleteNodeBasePtr)
{
  using ExecutorType = TypeParam;
  ExecutorType executor;

  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();
  promise.set_value(true);

  auto shared_future = future.share();
  auto ret = rclcpp::executors::spin_node_until_future_complete(
    executor, this->node->get_node_base_interface(), shared_future, 1s);
  EXPECT_EQ(rclcpp::FutureReturnCode::SUCCESS, ret);
}

// Check spin_node_until_future_complete with node pointer
TYPED_TEST(TestExecutors, testSpinNodeUntilFutureCompleteNodePtr)
{
  using ExecutorType = TypeParam;
  ExecutorType executor;

  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();
  promise.set_value(true);

  auto shared_future = future.share();
  auto ret = rclcpp::executors::spin_node_until_future_complete(
    executor, this->node, shared_future, 1s);
  EXPECT_EQ(rclcpp::FutureReturnCode::SUCCESS, ret);
}

// Check spin_until_future_complete can be properly interrupted.
TYPED_TEST(TestExecutors, testSpinUntilFutureCompleteInterrupted)
{
  using ExecutorType = TypeParam;
  ExecutorType executor;
  executor.add_node(this->node);

  bool spin_exited = false;

  // This needs to block longer than it takes to get to the shutdown call below and for
  // spin_until_future_complete to return
  std::future<void> future = std::async(
    std::launch::async,
    [&spin_exited]() {
      auto start = std::chrono::steady_clock::now();
      while (!spin_exited && (std::chrono::steady_clock::now() - start) < 1s) {
        std::this_thread::sleep_for(1ms);
      }
    });

  // Long timeout
  std::thread spinner([&spin_exited, &executor, &future]() {
      auto ret = executor.spin_until_future_complete(future, 1s);
      EXPECT_EQ(rclcpp::FutureReturnCode::INTERRUPTED, ret);
      spin_exited = true;
    });

  // Do some minimal work
  this->publisher->publish(test_msgs::msg::Empty());
  std::this_thread::sleep_for(1ms);

  // Force interruption
  rclcpp::shutdown();

  // Give it time to exit
  auto start = std::chrono::steady_clock::now();
  while (!spin_exited && (std::chrono::steady_clock::now() - start) < 1s) {
    std::this_thread::sleep_for(1ms);
  }

  EXPECT_TRUE(spin_exited);
  spinner.join();
}

// This test verifies that the add_node operation is robust wrt race conditions.
// It's mostly meant to prevent regressions in the events-executor, but the operation should be
// thread-safe in all executor implementations.
// The initial implementation of the events-executor contained a bug where the executor
// would end up in an inconsistent state and stop processing interrupt/shutdown notifications.
// Manually adding a node to the executor results in a) producing a notify waitable event
// and b) refreshing the executor collections.
// The inconsistent state would happen if the event was processed before the collections were
// finished to be refreshed: the executor would pick up the event but be unable to process it.
// This would leave the `notify_waitable_event_pushed_` flag to true, preventing additional
// notify waitable events to be pushed.
// The behavior is observable only under heavy load, so this test spawns several worker
// threads. Due to the nature of the bug, this test may still succeed even if the
// bug is present. However repeated runs will show its flakiness nature and indicate
// an eventual regression.
TYPED_TEST(TestExecutors, testRaceConditionAddNode)
{
  using ExecutorType = TypeParam;
  // rmw_connextdds doesn't support events-executor
  if (
    std::is_same<ExecutorType, rclcpp::experimental::executors::EventsExecutor>() &&
    std::string(rmw_get_implementation_identifier()).find("rmw_connextdds") == 0)
  {
    GTEST_SKIP();
  }

  // Spawn some threads to do some heavy work
  std::atomic<bool> should_cancel = false;
  std::vector<std::thread> stress_threads;
  for (size_t i = 0; i < 5 * std::thread::hardware_concurrency(); i++) {
    stress_threads.emplace_back(
      [&should_cancel, i]() {
        // This is just some arbitrary heavy work
        volatile size_t total = 0;
        for (size_t k = 0; k < 549528914167; k++) {
          if (should_cancel) {
            break;
          }
          total += k * (i + 42);
          (void)total;
        }
      });
  }

  // Create an executor
  auto executor = std::make_shared<ExecutorType>();
  // Start spinning
  auto executor_thread = std::thread(
    [executor]() {
      executor->spin();
    });
  // Add a node to the executor
  executor->add_node(this->node);

  // Cancel the executor (make sure that it's already spinning first)
  while (!executor->is_spinning() && rclcpp::ok()) {
    continue;
  }
  executor->cancel();

  // Try to join the thread after cancelling the executor
  // This is the "test". We want to make sure that we can still cancel the executor
  // regardless of the presence of race conditions
  executor_thread.join();

  // The test is now completed: we can join the stress threads
  should_cancel = true;
  for (auto & t : stress_threads) {
    t.join();
  }
}

// Check spin_until_future_complete with node base pointer (instantiates its own executor)
TEST(TestExecutors, testSpinUntilFutureCompleteNodeBasePtr)
{
  rclcpp::init(0, nullptr);

  {
    auto node = std::make_shared<rclcpp::Node>("node");

    std::promise<bool> promise;
    std::future<bool> future = promise.get_future();
    promise.set_value(true);

    auto shared_future = future.share();
    auto ret = rclcpp::spin_until_future_complete(
      node->get_node_base_interface(), shared_future, 1s);
    EXPECT_EQ(rclcpp::FutureReturnCode::SUCCESS, ret);
  }

  rclcpp::shutdown();
}

// Check spin_until_future_complete with node pointer (instantiates its own executor)
TEST(TestExecutors, testSpinUntilFutureCompleteNodePtr)
{
  rclcpp::init(0, nullptr);

  {
    auto node = std::make_shared<rclcpp::Node>("node");

    std::promise<bool> promise;
    std::future<bool> future = promise.get_future();
    promise.set_value(true);

    auto shared_future = future.share();
    auto ret = rclcpp::spin_until_future_complete(node, shared_future, 1s);
    EXPECT_EQ(rclcpp::FutureReturnCode::SUCCESS, ret);
  }

  rclcpp::shutdown();
}

template<typename T>
class TestIntraprocessExecutors : public ::testing::Test
{
public:
  static void SetUpTestCase()
  {
    rclcpp::init(0, nullptr);
  }

  static void TearDownTestCase()
  {
    rclcpp::shutdown();
  }

  void SetUp()
  {
    const auto test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    std::stringstream test_name;
    test_name << test_info->test_case_name() << "_" << test_info->name();
    node = std::make_shared<rclcpp::Node>("node", test_name.str());

    callback_count = 0u;

    const std::string topic_name = std::string("topic_") + test_name.str();

    rclcpp::PublisherOptions po;
    po.use_intra_process_comm = rclcpp::IntraProcessSetting::Enable;
    publisher = node->create_publisher<test_msgs::msg::Empty>(topic_name, rclcpp::QoS(1), po);

    auto callback = [this](test_msgs::msg::Empty::ConstSharedPtr) {
        this->callback_count.fetch_add(1u);
      };

    rclcpp::SubscriptionOptions so;
    so.use_intra_process_comm = rclcpp::IntraProcessSetting::Enable;
    subscription =
      node->create_subscription<test_msgs::msg::Empty>(
      topic_name, rclcpp::QoS(kNumMessages), std::move(callback), so);
  }

  void TearDown()
  {
    publisher.reset();
    subscription.reset();
    node.reset();
  }

  const size_t kNumMessages = 100;

  rclcpp::Node::SharedPtr node;
  rclcpp::Publisher<test_msgs::msg::Empty>::SharedPtr publisher;
  rclcpp::Subscription<test_msgs::msg::Empty>::SharedPtr subscription;
  std::atomic_size_t callback_count;
};

TYPED_TEST_SUITE(TestIntraprocessExecutors, ExecutorTypes, ExecutorTypeNames);

TYPED_TEST(TestIntraprocessExecutors, testIntraprocessRetrigger) {
  // This tests that executors will continue to service intraprocess subscriptions in the case
  // that publishers aren't continuing to publish.
  // This was previously broken in that intraprocess guard conditions were only triggered on
  // publish and the test was added to prevent future regressions.
  static constexpr size_t kNumMessages = 100;

  using ExecutorType = TypeParam;
  ExecutorType executor;
  executor.add_node(this->node);

  EXPECT_EQ(0u, this->callback_count.load());
  this->publisher->publish(test_msgs::msg::Empty());

  // Wait for up to 5 seconds for the first message to come available.
  const std::chrono::milliseconds sleep_per_loop(10);
  int loops = 0;
  while (1u != this->callback_count.load() && loops < 500) {
    rclcpp::sleep_for(sleep_per_loop);
    executor.spin_some();
    loops++;
  }
  EXPECT_EQ(1u, this->callback_count.load());

  // reset counter
  this->callback_count.store(0u);

  for (size_t ii = 0; ii < kNumMessages; ++ii) {
    this->publisher->publish(test_msgs::msg::Empty());
  }

  // Fire a timer every 10ms up to 5 seconds waiting for subscriptions to be read.
  loops = 0;
  auto timer = this->node->create_wall_timer(
    std::chrono::milliseconds(10), [this, &executor, &loops]() {
      loops++;
      if (kNumMessages == this->callback_count.load() || loops == 500) {
        executor.cancel();
      }
    });
  executor.spin();
  EXPECT_EQ(kNumMessages, this->callback_count.load());
}
