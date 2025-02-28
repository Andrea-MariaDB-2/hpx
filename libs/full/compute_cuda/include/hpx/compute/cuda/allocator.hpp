//  Copyright (c) 2016 Thomas Heller
//  Copyright (c) 2016 Hartmut Kaiser
//
//  SPDX-License-Identifier: BSL-1.0
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <hpx/config.hpp>

#if defined(HPX_HAVE_GPU_SUPPORT)
#include <hpx/assert.hpp>
#include <hpx/async_cuda/target.hpp>
#include <hpx/compute/cuda/detail/launch.hpp>
#include <hpx/compute/cuda/detail/scoped_active_target.hpp>
#include <hpx/compute/cuda/target_ptr.hpp>
#include <hpx/compute/cuda/value_proxy.hpp>
#include <hpx/modules/errors.hpp>
#include <hpx/type_support/unused.hpp>
#include <hpx/util/min.hpp>

#include <hpx/async_cuda/custom_gpu_api.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace hpx { namespace cuda { namespace experimental {
    template <typename T>
    class allocator
    {
    public:
        typedef T value_type;
        typedef target_ptr<T> pointer;
        typedef target_ptr<T const> const_pointer;
#if defined(HPX_COMPUTE_DEVICE_CODE)
        typedef T& reference;
        typedef T const& const_reference;
#else
        typedef value_proxy<T> reference;
        typedef value_proxy<T const> const_reference;
#endif
        typedef std::size_t size_type;
        typedef std::ptrdiff_t difference_type;

        template <typename U>
        struct rebind
        {
            typedef allocator<U> other;
        };

        typedef std::true_type is_always_equal;
        typedef std::true_type propagate_on_container_move_assignment;

        typedef hpx::cuda::experimental::target target_type;

        allocator()
          : target_(hpx::cuda::experimental::get_default_target())
        {
        }

        allocator(target_type const& tgt)
          : target_(tgt)
        {
        }

        allocator(target_type&& tgt)
          : target_(std::move(tgt))
        {
        }

        template <typename U>
        allocator(allocator<U> const& alloc)
          : target_(alloc.target_)
        {
        }

        // Returns the actual address of x even in presence of overloaded
        // operator&
        pointer address(reference x) const noexcept
        {
#if defined(HPX_COMPUTE_DEVICE_CODE)
            return &x;
#else
            return pointer(x.device_ptr(), target_);
#endif
        }

        const_pointer address(const_reference x) const noexcept
        {
#if defined(HPX_COMPUTE_DEVICE_CODE)
            return &x;
#else
            return pointer(x.device_ptr(), target_);
#endif
        }

        // Allocates n * sizeof(T) bytes of uninitialized storage by calling
        // cudaMalloc, but it is unspecified when and how this function is
        // called. The pointer hint may be used to provide locality of
        // reference: the allocator, if supported by the implementation, will
        // attempt to allocate the new memory block as close as possible to hint.
        pointer allocate(size_type n, const void* /* hint */ = nullptr)
        {
#if defined(HPX_COMPUTE_DEVICE_CODE)
            HPX_UNUSED(n);
            pointer result;
#else
            value_type* p = nullptr;
            detail::scoped_active_target active(target_);

            cudaError_t error = cudaMalloc(&p, n * sizeof(T));

            pointer result(p, target_);
            if (error != cudaSuccess)
            {
                HPX_THROW_EXCEPTION(out_of_memory,
                    "cuda::experimental::allocator<T>::allocate()",
                    std::string("cudaMalloc failed: ") +
                        cudaGetErrorString(error));
            }
#endif
            return result;
        }

        // Deallocates the storage referenced by the pointer p, which must be a
        // pointer obtained by an earlier call to allocate(). The argument n
        // must be equal to the first argument of the call to allocate() that
        // originally produced p; otherwise, the behavior is undefined.
        void deallocate(pointer p, size_type n)
        {
            HPX_UNUSED(n);
#if !defined(HPX_COMPUTE_DEVICE_CODE)
            detail::scoped_active_target active(target_);

            cudaError_t error = cudaFree(p.device_ptr());
            if (error != cudaSuccess)
            {
                HPX_THROW_EXCEPTION(kernel_error,
                    "cuda::experimental::allocator<T>::deallocate()",
                    std::string("cudaFree failed: ") +
                        cudaGetErrorString(error));
            }
#else
            HPX_UNUSED(p);
#endif
        }

        // Returns the maximum theoretically possible value of n, for which the
        // call allocate(n, 0) could succeed. In most implementations, this
        // returns std::numeric_limits<size_type>::max() / sizeof(value_type).
        size_type max_size() const noexcept
        {
            detail::scoped_active_target active(target_);
            std::size_t free = 0;
            std::size_t total = 0;
            cudaError_t error = cudaMemGetInfo(&free, &total);
            if (error != cudaSuccess)
            {
                HPX_THROW_EXCEPTION(kernel_error,
                    "cuda::experimental::allocator<T>::max_size()",
                    std::string("cudaMemGetInfo failed: ") +
                        cudaGetErrorString(error));
            }

            return total / sizeof(value_type);
        }

    public:
        // Constructs count objects of type T in allocated uninitialized
        // storage pointed to by p, using placement-new
        template <typename... Args>
        HPX_HOST_DEVICE void bulk_construct(pointer p, std::size_t count,
            Args&&...
#if defined(HPX_COMPUTE_CODE)
            args
#endif
        )
        {
#if defined(HPX_COMPUTE_CODE)
            int threads_per_block = (hpx::detail::min)(1024, int(count));
            int num_blocks =
                int((count + threads_per_block - 1) / threads_per_block);

            detail::launch(
                target_, num_blocks, threads_per_block,
                [] HPX_DEVICE(T * p, std::size_t count, Args const&... args) {
                    std::size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
                    if (idx < count)
                    {
                        ::new (p + idx) T(std::forward<Args>(args)...);
                    }
                },
                p.device_ptr(), count, std::forward<Args>(args)...);
            target_.synchronize();
#else
            HPX_UNUSED(p);
            HPX_UNUSED(count);
#endif
        }

        // Constructs an object of type T in allocated uninitialized storage
        // pointed to by p, using placement-new
        template <typename... Args>
        HPX_HOST_DEVICE void construct(pointer p,
            Args&&...
#if defined(HPX_COMPUTE_CODE)
            args
#endif
        )
        {
#if defined(HPX_COMPUTE_CODE)
            detail::launch(
                target_, 1, 1,
                [] HPX_DEVICE(T * p, Args const&... args) {
                    ::new (p) T(std::forward<Args>(args)...);
                },
                p.device_ptr(), std::forward<Args>(args)...);
            target_.synchronize();
#else
            HPX_UNUSED(p);
#endif
        }

        // Calls the destructor of count objects pointed to by p
        HPX_HOST_DEVICE void bulk_destroy(pointer p, std::size_t count)
        {
#if defined(HPX_COMPUTE_CODE)
            int threads_per_block = (hpx::detail::min)(1024, int(count));
            int num_blocks =
                int((count + threads_per_block) / threads_per_block) - 1;

            detail::launch(
                target_, num_blocks, threads_per_block,
                [] HPX_DEVICE(T * p, std::size_t count) {
                    std::size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
                    if (idx < count)
                    {
                        (p + idx)->~T();
                    }
                },
                p.device_ptr(), count);
            target_.synchronize();
#else
            HPX_UNUSED(p);
            HPX_UNUSED(count);
#endif
        }

        // Calls the destructor of the object pointed to by p
        HPX_HOST_DEVICE void destroy(pointer p)
        {
            bulk_destroy(p, 1);
        }

        // Access the underlying target (device)
        HPX_HOST_DEVICE target_type& target() noexcept
        {
            return target_;
        }
        HPX_HOST_DEVICE target_type const& target() const noexcept
        {
            return target_;
        }

    private:
        target_type target_;
    };
}}}    // namespace hpx::cuda::experimental

#endif
