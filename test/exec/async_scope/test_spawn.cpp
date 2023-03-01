#include <catch2/catch.hpp>
#include <exec/async_scope.hpp>
#include "test_common/schedulers.hpp"
#include "test_common/receivers.hpp"
#include "test_common/type_helpers.hpp"

namespace ex = stdexec;
using exec::counting_scope;
using stdexec::sync_wait;

//! Sender that throws exception when connected
struct throwing_sender {
  using is_sender = void;
  using completion_signatures = ex::completion_signatures<ex::set_value_t()>;

  template <class Receiver>
  struct operation {
    Receiver rcvr_;

    friend void tag_invoke(ex::start_t, operation& self) noexcept {
      ex::set_value(std::move(self.rcvr_));
    }
  };

  template <class Receiver>
  friend auto tag_invoke(ex::connect_t, throwing_sender&& self, Receiver&& rcvr)
    -> operation<std::decay_t<Receiver>> {
    throw std::logic_error("cannot connect");
    return {std::forward<Receiver>(rcvr)};
  }

  friend empty_env tag_invoke(stdexec::get_env_t, const throwing_sender&) noexcept {
    return {};
  }
};

TEST_CASE("spawn will execute its work", "[counting_scope][spawn]") {
  impulse_scheduler sch;
  bool executed{false};
  counting_scope context;
  auto use = exec::async_resource.open(context) | 
    ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){

      // Non-blocking call
      exec::async_scope.spawn(scope, ex::on(sch, ex::just() | ex::then([&] { executed = true; })));
      REQUIRE_FALSE(executed);

      return exec::async_scope.close(scope);
    });
  auto op = ex::connect(ex::when_all(std::move(use), exec::async_resource.run(context)), expect_void_receiver{});
  ex::start(op);

  // Run the operation on the scheduler
  sch.start_next();
  // Now the spawn work should be completed
  REQUIRE(executed);
}

TEST_CASE("spawn will start sender before returning", "[counting_scope][spawn]") {
  bool executed{false};
  counting_scope context;
  auto use = exec::async_resource.open(context) | 
    ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){

      // This will be a blocking call
      exec::async_scope.spawn(scope, ex::just() | ex::then([&] { executed = true; }));
      REQUIRE(executed);

      return exec::async_scope.close(scope);
    });
  sync_wait(stdexec::when_all(use, exec::async_resource.run(context)));
}

#if !NO_TESTS_WITH_EXCEPTIONS
TEST_CASE(
  "spawn will propagate exceptions encountered during op creation", 
  "[counting_scope][spawn]") {
  counting_scope context;
  auto use = exec::async_resource.open(context) | 
    ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){
      try {
        exec::async_scope.spawn(scope, throwing_sender{} | ex::then([&] { FAIL("work should not be executed"); }));
        FAIL("Exceptions should have been thrown");
      } catch (const std::logic_error& e) {
        SUCCEED("correct exception caught");
      } catch (...) {
        FAIL("invalid exception caught");
      }
      return exec::async_scope.close(scope);
    });
  sync_wait(stdexec::when_all(use, exec::async_resource.run(context)));
}
#endif

TEST_CASE(
  "TODO: spawn will keep the scope non-empty until the work is executed",
  "[counting_scope][spawn]") {
  impulse_scheduler sch;
  bool executed{false};
  counting_scope context;
  auto use = exec::async_resource.open(context) | 
    ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){

      // Before adding any operations, the scope is empty
      // TODO: reenable this
      // REQUIRE(P2519::__scope::empty(scope));

      // Non-blocking call
      exec::async_scope.spawn(scope, ex::on(sch, ex::just() | ex::then([&] { executed = true; })));
      REQUIRE_FALSE(executed);

      // The scope is now non-empty
      // TODO: reenable this
      // REQUIRE_FALSE(P2519::__scope::empty(scope));
      // REQUIRE(P2519::__scope::op_count(scope) == 1);

      return exec::async_scope.close(scope);
    });
  auto op = ex::connect(ex::when_all(std::move(use), exec::async_resource.run(context)), expect_void_receiver{});
  ex::start(op);

  // Run the operation on the scheduler; blocking call
  sch.start_next();

  // Now the scope should again be empty
  // TODO: reenable this
  // REQUIRE(P2519::__scope::empty(scope));
  REQUIRE(executed);
}

TEST_CASE(
  "TODO: spawn will keep track on how many operations are in flight", 
  "[counting_scope][spawn]") {
  impulse_scheduler sch;
  constexpr std::size_t num_oper = 10;
  std::size_t num_executed{0};
  counting_scope context;
  auto use = exec::async_resource.open(context) | 
    ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){

      // Before adding any operations, the scope is empty
      // TODO: reenable this
      // REQUIRE(P2519::__scope::op_count(scope) == 0);
      // REQUIRE(P2519::__scope::empty(scope));

      for (std::size_t i = 0; i < num_oper; i++) {
        exec::async_scope.spawn(scope, ex::on(sch, ex::just() | ex::then([&] { num_executed++; })));
        size_t num_expected_ops = i + 1;
        // TODO: reenable this
        // REQUIRE(P2519::__scope::op_count(scope) == num_expected_ops);
        (void)num_expected_ops;
      }
      return exec::async_scope.close(scope);
    });
  auto op = ex::connect(ex::when_all(std::move(use), exec::async_resource.run(context)), expect_void_receiver{});
  ex::start(op);

  // Now execute the operations
  for (std::size_t i = 0; i < num_oper; i++) {
    sch.start_next();
    size_t num_expected_ops = num_oper - i - 1;
    // TODO: reenable this
    // REQUIRE(P2519::__scope::op_count(scope) == num_expected_ops);
    (void) num_expected_ops;
  }

  // The scope is empty after all the operations are executed
  // TODO: reenable this
  // REQUIRE(P2519::__scope::empty(scope));
  REQUIRE(num_executed == num_oper);
}

TEST_CASE("TODO: spawn work can be cancelled by cancelling the scope", "[counting_scope][spawn]") {
  impulse_scheduler sch;
  bool cancelled1{false};
  bool cancelled2{false};
  counting_scope context;
  auto use = exec::async_resource.open(context) | 
    ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){


      exec::async_scope.spawn(
        scope, 
        ex::on(sch, ex::just() 
          | ex::let_stopped([&] {
              cancelled1 = true;
              return ex::just();
            })));
      exec::async_scope.spawn(
        scope, 
        ex::on(sch, ex::just() 
          | ex::let_stopped([&] {
              cancelled2 = true;
              return ex::just();
            })));

      // TODO: reenable this
      // REQUIRE(P2519::__scope::op_count(scope) == 2);

      return exec::async_scope.close(scope);
    });
  auto op = ex::connect(ex::when_all(std::move(use), exec::async_resource.run(context)), expect_void_receiver{});
  ex::start(op);

  // Execute the first operation, before cancelling
  sch.start_next();
  REQUIRE_FALSE(cancelled1);
  REQUIRE_FALSE(cancelled2);

  // Cancel the counting_scope object

  // context.request_stop();

  // TODO: reenable this
  // REQUIRE(P2519::__scope::op_count(scope) == 1);

  // Execute the first operation, after cancelling
  sch.start_next();
  REQUIRE_FALSE(cancelled1);
  // TODO: second operation should be cancelled
  // REQUIRE(cancelled2);
  REQUIRE_FALSE(cancelled2);

  // TODO: reenable this
  // REQUIRE(P2519::__scope::empty(scope));
}

template <typename S>
concept is_spawn_worthy = requires(S&& snd, exec::__scope::__async_scope& scope) { 
  exec::async_scope.spawn(scope, std::move(snd), ex::empty_env{}); 
};

TEST_CASE("spawn accepts void senders", "[counting_scope][spawn]") {
  static_assert(is_spawn_worthy<decltype(ex::just())>);
}
TEST_CASE(
  "spawn doesn't accept non-void senders", 
  "[counting_scope][spawn]") {
  static_assert(!is_spawn_worthy<decltype(ex::just(13))>);
  static_assert(!is_spawn_worthy<decltype(ex::just(3.14))>);
  static_assert(!is_spawn_worthy<decltype(ex::just("hello"))>);
}
TEST_CASE(
  "TODO: spawn doesn't accept senders of errors", 
  "[counting_scope][spawn]") {
  // TODO: check if just_error(exception_ptr) should be allowed
  static_assert(is_spawn_worthy<decltype(ex::just_error(std::exception_ptr{}))>);
  static_assert(!is_spawn_worthy<decltype(ex::just_error(std::error_code{}))>);
  static_assert(!is_spawn_worthy<decltype(ex::just_error(-1))>);
}
TEST_CASE(
  "spawn should accept senders that send stopped signal", 
  "[counting_scope][spawn]") {
  static_assert(is_spawn_worthy<decltype(ex::just_stopped())>);
}

TEST_CASE(
  "TODO: spawn works with senders that complete with stopped signal", 
  "[counting_scope][spawn]") {
  impulse_scheduler sch;
  counting_scope context;
  auto use = exec::async_resource.open(context) | 
    ex::let_value([&](exec::satisfies<exec::async_scope> auto scope){

      // TODO: reenable this
      // REQUIRE(P2519::__scope::empty(scope));

      exec::async_scope.spawn(scope, ex::on(sch, ex::just_stopped()));

      // The scope is now non-empty
      // TODO: reenable this
      // REQUIRE_FALSE(P2519::__scope::empty(scope));
      // REQUIRE(P2519::__scope::op_count(scope) == 1);

      return exec::async_scope.close(scope);
    });
  auto op = ex::connect(ex::when_all(std::move(use), exec::async_resource.run(context)), expect_void_receiver{});
  ex::start(op);

  // Run the operation on the scheduler; blocking call
  sch.start_next();

  // Now the scope should again be empty
  // TODO: reenable this
  // REQUIRE(P2519::__scope::empty(scope));
}
