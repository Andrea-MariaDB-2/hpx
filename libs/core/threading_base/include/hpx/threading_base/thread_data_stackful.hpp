//  Copyright (c) 2007-2021 Hartmut Kaiser
//  Copyright (c)      2011 Bryce Lelbach
//  Copyright (c) 2008-2009 Chirag Dekate, Anshul Tandon
//
//  SPDX-License-Identifier: BSL-1.0
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <hpx/config.hpp>
#include <hpx/allocator_support/internal_allocator.hpp>
#include <hpx/assert.hpp>
#include <hpx/coroutines/thread_id_type.hpp>
#include <hpx/functional/function.hpp>
#include <hpx/modules/errors.hpp>
#include <hpx/threading_base/execution_agent.hpp>
#include <hpx/threading_base/thread_data.hpp>
#include <hpx/threading_base/thread_init_data.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>

#include <hpx/config/warnings_prefix.hpp>

///////////////////////////////////////////////////////////////////////////////
namespace hpx { namespace threads {

    ///////////////////////////////////////////////////////////////////////////
    /// A \a thread is the representation of a ParalleX thread. It's a first
    /// class object in ParalleX. In our implementation this is a user level
    /// thread running on top of one of the OS threads spawned by the \a
    /// thread-manager.
    ///
    /// A \a thread encapsulates:
    ///  - A thread status word (see the functions \a thread#get_state and
    ///    \a thread#set_state)
    ///  - A function to execute (the thread function)
    ///  - A frame (in this implementation this is a block of memory used as
    ///    the threads stack)
    ///  - A block of registers (not implemented yet)
    ///
    /// Generally, \a threads are not created or executed directly. All
    /// functionality related to the management of \a threads is
    /// implemented by the thread-manager.
    class HPX_CORE_EXPORT thread_data_stackful : public thread_data
    {
    private:
        // Avoid warning about using 'this' in initializer list
        thread_data* this_()
        {
            return this;
        }

        static util::internal_allocator<thread_data_stackful> thread_alloc_;

    public:
        HPX_FORCEINLINE coroutine_type::result_type call(
            hpx::execution_base::this_thread::detail::agent_storage*
                agent_storage)
        {
            HPX_ASSERT(get_state().state() == thread_schedule_state::active);
            HPX_ASSERT(this == coroutine_.get_thread_id().get());

            hpx::execution_base::this_thread::reset_agent ctx(
                agent_storage, agent_);
            return coroutine_(set_state_ex(thread_restart_state::signaled));
        }

#if defined(HPX_DEBUG)
        thread_id_type get_thread_id() const override
        {
            HPX_ASSERT(this == coroutine_.get_thread_id().get());
            return this->thread_data::get_thread_id();
        }
#endif
#if defined(HPX_HAVE_THREAD_PHASE_INFORMATION)
        std::size_t get_thread_phase() const noexcept override
        {
            return coroutine_.get_thread_phase();
        }
#endif

        std::size_t get_thread_data() const override
        {
            return coroutine_.get_thread_data();
        }

        std::size_t set_thread_data(std::size_t data) override
        {
            return coroutine_.set_thread_data(data);
        }

#if defined(HPX_HAVE_LIBCDS)
        std::size_t get_libcds_data() const override
        {
            return coroutine_.get_libcds_data();
        }

        std::size_t set_libcds_data(std::size_t data) override
        {
            return coroutine_.set_libcds_data(data);
        }

        std::size_t get_libcds_hazard_pointer_data() const override
        {
            return coroutine_.get_libcds_hazard_pointer_data();
        }

        std::size_t set_libcds_hazard_pointer_data(std::size_t data) override
        {
            return coroutine_.set_libcds_hazard_pointer_data(data);
        }

        std::size_t get_libcds_dynamic_hazard_pointer_data() const override
        {
            return coroutine_.get_libcds_dynamic_hazard_pointer_data();
        }

        std::size_t set_libcds_dynamic_hazard_pointer_data(
            std::size_t data) override
        {
            return coroutine_.set_libcds_dynamic_hazard_pointer_data(data);
        }
#endif

        void rebind(thread_init_data& init_data) override
        {
            this->thread_data::rebind_base(init_data);

            coroutine_.rebind(std::move(init_data.func), thread_id_type(this));

            HPX_ASSERT(coroutine_.is_ready());
        }

        thread_data_stackful(
            thread_init_data& init_data, void* queue, std::ptrdiff_t stacksize)
          : thread_data(init_data, queue, stacksize)
          , coroutine_(
                std::move(init_data.func), thread_id_type(this_()), stacksize)
          , agent_(coroutine_.impl())
        {
            HPX_ASSERT(coroutine_.is_ready());
        }

        ~thread_data_stackful();

        static inline thread_data* create(
            thread_init_data& init_data, void* queue, std::ptrdiff_t stacksize);

        void destroy() override
        {
            this->~thread_data_stackful();
            thread_alloc_.deallocate(this, 1);
        }

    private:
        coroutine_type coroutine_;
        execution_agent agent_;
    };

    ////////////////////////////////////////////////////////////////////////////
    inline thread_data* thread_data_stackful::create(
        thread_init_data& data, void* queue, std::ptrdiff_t stacksize)
    {
        thread_data* p = thread_alloc_.allocate(1);
        new (p) thread_data_stackful(data, queue, stacksize);
        return p;
    }
}}    // namespace hpx::threads

#include <hpx/config/warnings_suffix.hpp>
