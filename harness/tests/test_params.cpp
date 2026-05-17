#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include <distingnt/api.h>

TEST_CASE("NT_setParameterFromUi writes v[p] and calls parameterChanged", "[params]") {
    nt::reset_runtime();
    auto* alg = nt::load_test_algorithm();
    int triggers_before = nt::test_parameter_changed_count();
    int p = 0;
    NT_setParameterFromUi(NT_algorithmIndex(alg->algorithm), p + NT_parameterOffset(), 75);

    REQUIRE(alg->algorithm->v[p] == 75);
    REQUIRE(nt::test_parameter_changed_count() == triggers_before + 1);
    REQUIRE(nt::test_last_changed_param() == p);
}

TEST_CASE("NT_setParameterFromUi with out-of-bounds param is a no-op", "[params]") {
    nt::reset_runtime();
    auto* alg = nt::load_test_algorithm();
    int triggers_before = nt::test_parameter_changed_count();
    NT_setParameterFromUi(NT_algorithmIndex(alg->algorithm), 999 + NT_parameterOffset(), 1);
    REQUIRE(nt::test_parameter_changed_count() == triggers_before);
}

TEST_CASE("NT_setParameterGrayedOut records state without invoking parameterChanged", "[params]") {
    nt::reset_runtime();
    auto* alg = nt::load_test_algorithm();
    int triggers_before = nt::test_parameter_changed_count();
    NT_setParameterGrayedOut(NT_algorithmIndex(alg->algorithm), 0 + NT_parameterOffset(), true);
    REQUIRE(nt::is_parameter_grayed_out(NT_algorithmIndex(alg->algorithm), 0 + NT_parameterOffset()));
    REQUIRE(nt::test_parameter_changed_count() == triggers_before);
}
