#pragma once

#include "litebird/js/JsValue.hpp"
#include <string>
#include <string_view>

namespace litebird {

class Json {
public:
    static JsValue parse(std::string_view json_str);
    static std::string stringify(const JsValue& value);
};

} // namespace litebird
