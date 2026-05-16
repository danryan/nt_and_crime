#pragma once
#include <distingnt/api.h>

namespace nt {
struct LoadedPlugin {
    const _NT_factory* factory;
    _NT_algorithm*     algorithm;
};
LoadedPlugin* load_test_algorithm();  // a stub algorithm used by param tests
// LoadedPlugin* load_plugin();        // <-- introduced in Task 10

// Test helpers: introspect the stub algorithm's parameterChanged call counter.
int test_parameter_changed_count();
int test_last_changed_param();
}
