//  Copyright (c) 2021 ETH Zurich
//
//  SPDX-License-Identifier: BSL-1.0
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <hpx/modules/execution.hpp>
#include <hpx/modules/testing.hpp>

#include <atomic>
#include <exception>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace ex = hpx::execution::experimental;

template <typename F>
struct callback_receiver
{
    std::decay_t<F> f;
    std::atomic<bool>& set_value_called;

    template <typename E>
    void set_error(E&&) noexcept
    {
        HPX_TEST(false);
    }

    void set_done() noexcept
    {
        HPX_TEST(false);
    };

    template <typename... Ts>
    void set_value(Ts&&... ts) noexcept
    {
        HPX_INVOKE(f, std::forward<Ts>(ts)...);
        set_value_called = true;
    }
};

template <typename F>
struct error_callback_receiver
{
    std::decay_t<F> f;
    std::atomic<bool>& set_error_called;

    template <typename E>
    void set_error(E&& e) noexcept
    {
        HPX_INVOKE(f, std::forward<E>(e));
        set_error_called = true;
    }

    void set_done() noexcept
    {
        HPX_TEST(false);
    };

    template <typename... Ts>
    void set_value(Ts&&...) noexcept
    {
        HPX_TEST(false);
    }
};

struct custom_transformer
{
    std::atomic<bool>& tag_invoke_overload_called;
    std::atomic<bool>& call_operator_called;
    bool throws;

    void operator()() const
    {
        call_operator_called = true;
        if (throws)
        {
            throw std::runtime_error("error");
        }
    }
};

template <typename S>
auto tag_invoke(ex::transform_t, S&& s, custom_transformer t)
{
    t.tag_invoke_overload_called = true;
    return ex::transform(std::forward<S>(s), [t = std::move(t)]() { t(); });
}

void check_exception_ptr(std::exception_ptr eptr)
{
    try
    {
        std::rethrow_exception(eptr);
    }
    catch (const std::runtime_error& e)
    {
        HPX_TEST_EQ(std::string(e.what()), std::string("error"));
    }
};

int main()
{
    // Success path
    {
        std::atomic<bool> set_value_called{false};
        auto s = ex::transform(ex::just(), [] {});
        auto f = [] {};
        auto r = callback_receiver<decltype(f)>{f, set_value_called};
        ex::start(ex::connect(s, r));
        HPX_TEST(set_value_called);
    }

    {
        std::atomic<bool> set_value_called{false};
        auto s = ex::transform(ex::just(0), [](int x) { return ++x; });
        auto f = [](int x) { HPX_TEST_EQ(x, 1); };
        auto r = callback_receiver<decltype(f)>{f, set_value_called};
        ex::start(ex::connect(s, r));
        HPX_TEST(set_value_called);
    }

    {
        std::atomic<bool> set_value_called{false};
        auto s1 = ex::transform(ex::just(0), [](int x) { return ++x; });
        auto s2 = ex::transform(std::move(s1), [](int x) { return ++x; });
        auto s3 = ex::transform(std::move(s2), [](int x) { return ++x; });
        auto s4 = ex::transform(std::move(s3), [](int x) { return ++x; });
        auto f = [](int x) { HPX_TEST_EQ(x, 4); };
        auto r = callback_receiver<decltype(f)>{f, set_value_called};
        ex::start(ex::connect(std::move(s4), r));
        HPX_TEST(set_value_called);
    }

    {
        std::atomic<bool> set_value_called{false};
        auto s1 = ex::transform(ex::just(), []() { return 3; });
        auto s2 = ex::transform(std::move(s1), [](int x) { return x / 1.5; });
        auto s3 = ex::transform(std::move(s2), [](double x) { return x / 2; });
        auto s4 = ex::transform(
            std::move(s3), [](int x) { return std::to_string(x); });
        auto f = [](std::string x) { HPX_TEST_EQ(x, std::string("1")); };
        auto r = callback_receiver<decltype(f)>{f, set_value_called};
        ex::start(ex::connect(std::move(s4), r));
        HPX_TEST(set_value_called);
    }

    {
        std::atomic<bool> receiver_set_value_called{false};
        std::atomic<bool> tag_invoke_overload_called{false};
        std::atomic<bool> custom_transformer_call_operator_called{false};
        auto s = ex::transform(ex::just(),
            custom_transformer{tag_invoke_overload_called,
                custom_transformer_call_operator_called, false});
        auto f = [] {};
        auto r = callback_receiver<decltype(f)>{f, receiver_set_value_called};
        ex::start(ex::connect(s, r));
        HPX_TEST(receiver_set_value_called);
        HPX_TEST(tag_invoke_overload_called);
        HPX_TEST(custom_transformer_call_operator_called);
    }

    // Failure path
    {
        std::atomic<bool> set_error_called{false};
        auto s = ex::transform(
            ex::just(), [] { throw std::runtime_error("error"); });
        auto r = error_callback_receiver<decltype(check_exception_ptr)>{
            check_exception_ptr, set_error_called};
        ex::start(ex::connect(s, r));
        HPX_TEST(set_error_called);
    }

    {
        std::atomic<bool> set_error_called{false};
        auto s1 = ex::transform(ex::just(0), [](int x) { return ++x; });
        auto s2 = ex::transform(std::move(s1), [](int x) {
            throw std::runtime_error("error");
            return ++x;
        });
        auto s3 = ex::transform(std::move(s2), [](int x) {
            HPX_TEST(false);
            return ++x;
        });
        auto s4 = ex::transform(std::move(s3), [](int x) {
            HPX_TEST(false);
            return ++x;
        });
        auto r = error_callback_receiver<decltype(check_exception_ptr)>{
            check_exception_ptr, set_error_called};
        ex::start(ex::connect(std::move(s4), r));
        HPX_TEST(set_error_called);
    }

    {
        std::atomic<bool> receiver_set_error_called{false};
        std::atomic<bool> tag_invoke_overload_called{false};
        std::atomic<bool> custom_transformer_call_operator_called{false};
        auto s = ex::transform(ex::just(),
            custom_transformer{tag_invoke_overload_called,
                custom_transformer_call_operator_called, true});
        auto r = error_callback_receiver<decltype(check_exception_ptr)>{
            check_exception_ptr, receiver_set_error_called};
        ex::start(ex::connect(s, r));
        HPX_TEST(receiver_set_error_called);
        HPX_TEST(tag_invoke_overload_called);
        HPX_TEST(custom_transformer_call_operator_called);
    }

    return hpx::util::report_errors();
}
