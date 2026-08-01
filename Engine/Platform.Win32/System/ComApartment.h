#pragma once

#include <Windows.h>
#include <objbase.h>

namespace mrg::platform
{
    // RAII wrapper for one COM apartment on the calling thread.
    class ComApartment final
    {
    public:
        explicit ComApartment(
            DWORD concurrencyModel = COINIT_MULTITHREADED);
        ~ComApartment();

        ComApartment(const ComApartment&) = delete;
        ComApartment& operator=(const ComApartment&) = delete;

    private:
        HRESULT result_{};
    };
}
