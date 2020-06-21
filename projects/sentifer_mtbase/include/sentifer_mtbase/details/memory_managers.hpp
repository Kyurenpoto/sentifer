#pragma once

#include <cstddef>
#include <memory_resource>

namespace mtbase
{
    struct generic_allocator
    {
        generic_allocator(const generic_allocator&) noexcept = default;

        generic_allocator(std::pmr::memory_resource* r) :
            res{ r }
        {}

    public:
        [[nodiscard]] void* allocate_bytes(size_t nbytes, size_t alignment = alignof(std::max_align_t))
        {
            return res->allocate(nbytes, alignment);
        }

        void deallocate_bytes(void* p, size_t nbytes, size_t alignment = alignof(std::max_align_t))
        {
            res->deallocate(p, nbytes, alignment);
        }

        template<class T>
        [[nodiscard]] T* allocate_object(size_t n = 1)
        {
            if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
                throw std::bad_array_new_length{};

            return static_cast<T*>(allocate_bytes(n * sizeof(T), alignof(T)));
        }

        template<class T>
        void deallocate_object(T* p, size_t n = 1)
        {
            deallocate_bytes(p, n * sizeof(T), alignof(T));
        }

        template<class T, class... Args>
        void construct(T* p, Args&&... args)
        {
            new(p) T{ std::forward<Args>(args)... };
        }

        template<class T>
        void construct(T* p, T&& other)
        {
            new(p) T{ other };
        }

        template<class T>
        void destroy(T* p)
        {
            p->~T();
        }

        template<class T, class... Args>
        [[nodiscard]] T* new_object(Args&&... args)
        {
            T* p = allocate_object<T>();

            try
            {
                construct(p, std::forward<Args>(args)...);
            }
            catch (...)
            {
                deallocate_object(p);
                throw;
            }

            return p;
        }

        template<class T>
        [[nodiscard]] T* new_object(T&& other)
        {
            T* p = allocate_object<T>();

            try
            {
                construct(p, other);
            }
            catch (...)
            {
                deallocate_object(p);
                throw;
            }

            return p;
        }

        template<class T>
        void delete_object(T* p)
        {
            destroy(p);
            deallocate_object(p);
        }

        std::pmr::memory_resource* resource() const
        {
            return res;
        }

    private:
        std::pmr::memory_resource* res;
    };

    template<class T>
    struct obj_allocator
    {
        obj_allocator(std::pmr::memory_resource* r) :
            genAlloc{ r }
        {}

    public:
        template<class... Args>
        [[nodiscard]] T* new_object(Args&&... args)
        {
            return genAlloc.new_object<T>(std::forward<Args>(args)...);
        }

        [[nodiscard]] T* new_object(T&& other)
        {
            return genAlloc.new_object<T>(other);
        }

        void delete_object(T* p)
        {
            genAlloc.delete_object(p);
        }

    private:
        generic_allocator genAlloc;
    };
}
