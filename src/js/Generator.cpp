#include <litebird/js/Generator.hpp>

namespace litebird {

static thread_local GeneratorData* s_active_generator = nullptr;

GeneratorData* GeneratorContext::get_active() {
    return s_active_generator;
}

void GeneratorContext::set_active(GeneratorData* gen) {
    s_active_generator = gen;
}

} // namespace litebird
