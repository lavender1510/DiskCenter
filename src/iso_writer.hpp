#pragma once
#include "optical.hpp"

namespace dc {

void write_iso_joliet(const std::wstring& folder, const std::wstring& destination, const std::wstring& label,
                      std::size_t buffer_size, const Report& report, const IsCancelled& cancelled);

}
