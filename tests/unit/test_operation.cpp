// SPDX-License-Identifier: TBD
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

#include "platter/core/operation.hpp"

using namespace platter;
using namespace std::chrono_literals;

namespace {

// Minimal operation: `steps` units of 1 MiB, optional per-step delay.
class StepOp final : public Operation {
public:
    StepOp(int steps, std::chrono::microseconds delay = 0us, bool fail_validate = false, int fail_at = -1)
        : Operation("test", "dev0", AccessKind::shared_read),
          steps_(steps), delay_(delay), fail_validate_(fail_validate), fail_at_(fail_at) {}
    ~StepOp() override {
        cancel();
        wait();
    }
    std::atomic<int> done{0};

protected:
    Status do_validate() override {
        if (fail_validate_) return Error{Errc::invalid_argument, "bad config"};
        set_total_bytes(static_cast<std::uint64_t>(steps_) << 20);
        return {};
    }
    Status do_run(OperationControl& c) override {
        for (int i = 0; i < steps_; ++i) {
            if (!c.checkpoint()) return Error{Errc::cancelled, "cancelled"};
            if (i == fail_at_) return Error{Errc::io_error, "boom"};
            if (delay_.count()) std::this_thread::sleep_for(delay_);
            add_completed(1u << 20, static_cast<std::uint64_t>(i));
            ++done;
        }
        return {};
    }

private:
    int steps_;
    std::chrono::microseconds delay_;
    bool fail_validate_;
    int fail_at_;
};

template <typename Pred>
bool eventually(Pred p, std::chrono::milliseconds limit = 5000ms) {
    const auto end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end) {
        if (p()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return p();
}

}  // namespace

TEST_CASE("state machine edges", "[operation]") {
    using S = OperationState;
    CHECK(is_valid_transition(S::created, S::validating));
    CHECK(is_valid_transition(S::validating, S::ready));
    CHECK(is_valid_transition(S::ready, S::running));
    CHECK(is_valid_transition(S::running, S::paused));
    CHECK(is_valid_transition(S::paused, S::running));
    CHECK(is_valid_transition(S::running, S::stopping));
    CHECK(is_valid_transition(S::stopping, S::cancelled));
    CHECK_FALSE(is_valid_transition(S::created, S::running));     // must validate first
    CHECK_FALSE(is_valid_transition(S::completed, S::running));   // terminal is final
    CHECK_FALSE(is_valid_transition(S::cancelled, S::ready));
    CHECK_FALSE(is_valid_transition(S::failed, S::validating));
    CHECK(is_terminal(S::completed));
    CHECK_FALSE(is_terminal(S::paused));
    CHECK(operation_state_name(S::stopping) == "STOPPING");
}

TEST_CASE("operation runs to completion with progress", "[operation]") {
    StepOp op(20);
    CHECK(op.state() == OperationState::created);
    CHECK_FALSE(op.start());  // not validated yet
    REQUIRE(op.validate());
    CHECK(op.state() == OperationState::ready);
    REQUIRE(op.start());
    op.wait();
    CHECK(op.state() == OperationState::completed);
    auto p = op.progress();
    CHECK(p.completed_bytes == 20ull << 20);
    CHECK(p.total_bytes == 20ull << 20);
    CHECK(p.percent() == 100.0);
    CHECK(p.current_lba == 19);
    CHECK_FALSE(p.eta);
    CHECK(p.type == "test");
    CHECK(p.device_id == "dev0");
}

TEST_CASE("validation failure lands in FAILED with a message", "[operation]") {
    StepOp op(5, 0us, /*fail_validate=*/true);
    CHECK_FALSE(op.validate());
    CHECK(op.state() == OperationState::failed);
    CHECK(op.progress().message.find("bad config") != std::string::npos);
    CHECK_FALSE(op.start());
}

TEST_CASE("run failure lands in FAILED", "[operation]") {
    StepOp op(10, 0us, false, /*fail_at=*/4);
    REQUIRE(op.validate());
    REQUIRE(op.start());
    op.wait();
    CHECK(op.state() == OperationState::failed);
    CHECK(op.done == 4);
    CHECK(op.progress().message.find("boom") != std::string::npos);
}

TEST_CASE("pause stops progress, resume continues, elapsed excludes pause", "[operation]") {
    StepOp op(400, 1000us);
    REQUIRE(op.validate());
    REQUIRE(op.start());
    REQUIRE(eventually([&] { return op.done >= 5; }));
    op.pause();
    CHECK(op.state() == OperationState::paused);
    std::this_thread::sleep_for(20ms);  // let an in-flight step finish
    const int frozen = op.done;
    const auto elapsed_at_pause = op.progress().elapsed;
    std::this_thread::sleep_for(150ms);
    CHECK(op.done == frozen);
    CHECK(op.progress().elapsed == elapsed_at_pause);
    CHECK(op.progress().current_bytes_per_sec == 0);
    op.resume();
    CHECK(op.state() == OperationState::running);
    REQUIRE(eventually([&] { return op.done > frozen + 3; }));
    op.cancel();
    op.wait();
    CHECK(op.state() == OperationState::cancelled);
}

TEST_CASE("cancel while paused does not hang", "[operation]") {
    StepOp op(1000, 1000us);
    REQUIRE(op.validate());
    REQUIRE(op.start());
    REQUIRE(eventually([&] { return op.done >= 2; }));
    op.pause();
    op.cancel();
    op.wait();
    CHECK(op.state() == OperationState::cancelled);
    CHECK(op.done < 1000);
}

TEST_CASE("cancel before start", "[operation]") {
    StepOp op(3);
    REQUIRE(op.validate());
    op.cancel();
    op.wait();
    CHECK(op.state() == OperationState::cancelled);
    CHECK_FALSE(op.start());
}

TEST_CASE("ETA and throughput are reported while running", "[operation]") {
    StepOp op(300, 2000us);
    REQUIRE(op.validate());
    REQUIRE(op.start());
    REQUIRE(eventually([&] { return op.done >= 20; }));
    auto p = op.progress();
    CHECK(p.state == OperationState::running);
    CHECK(p.average_bytes_per_sec > 0);
    CHECK(p.eta.has_value());
    CHECK(p.percent() > 0);
    CHECK(p.percent() < 100);
    op.cancel();
    op.wait();
}

TEST_CASE("device leases: many readers or one exclusive holder", "[operation][safety]") {
    DeviceLeaseRegistry reg;
    auto r1 = reg.acquire("sda", AccessKind::shared_read);
    auto r2 = reg.acquire("sda", AccessKind::shared_read);
    REQUIRE(r1);
    REQUIRE(r2);
    CHECK(reg.holders("sda") == 2);

    auto x = reg.acquire("sda", AccessKind::exclusive);
    REQUIRE_FALSE(x);
    CHECK(x.error().code == Errc::device_busy);

    CHECK(reg.acquire("sdb", AccessKind::exclusive));  // other devices unaffected; lease released at once
    CHECK(reg.holders("sdb") == 0);

    r1->release();
    r2->release();
    CHECK(reg.holders("sda") == 0);

    auto ex = reg.acquire("sda", AccessKind::exclusive);
    REQUIRE(ex);
    CHECK(reg.acquire("sda", AccessKind::shared_read).error().code == Errc::device_busy);
    CHECK(reg.acquire("sda", AccessKind::exclusive).error().code == Errc::device_busy);
    {
        auto moved = std::move(ex).value();  // lease is movable; released when it dies
    }
    CHECK(reg.holders("sda") == 0);
    CHECK(reg.acquire("sda", AccessKind::shared_read));
}
