#include "TestUtil.h"
#include <Arduino.h>
#include <tuple>
#include <type_traits>
#include <unity.h>
#include <utility>

namespace cpp17_probe
{

inline constexpr int inlineVariable = 17;

template <typename... Values> constexpr int foldSum(Values... values)
{
    return (values + ...);
}

constexpr int ifConstexprProbe()
{
    if constexpr (std::is_same_v<int, int>) {
        return inlineVariable;
    } else {
        return 0;
    }
}

constexpr int structuredBindingProbe()
{
    auto [left, right] = std::pair<int, int>{7, 10};
    return left + right;
}

constexpr int constexprLambdaProbe()
{
    constexpr auto probe = [](int value) constexpr { return value + 10; };
    return probe(7);
}

static_assert(ifConstexprProbe() == 17, "if constexpr probe failed");
static_assert(structuredBindingProbe() == 17, "structured bindings probe failed");
static_assert(foldSum(5, 6, 6) == 17, "fold expression probe failed");
static_assert(constexprLambdaProbe() == 17, "constexpr lambda probe failed");
static_assert(std::is_same_v<std::tuple_element_t<0, std::tuple<int, bool>>, int>, "tuple alias probe failed");

} // namespace cpp17_probe

void setUp(void) {}
void tearDown(void) {}

static void test_cpp17_runtime_probes()
{
    TEST_ASSERT_EQUAL_INT(17, cpp17_probe::ifConstexprProbe());
    TEST_ASSERT_EQUAL_INT(17, cpp17_probe::structuredBindingProbe());
    TEST_ASSERT_EQUAL_INT(17, cpp17_probe::foldSum(8, 4, 5));
    TEST_ASSERT_EQUAL_INT(17, cpp17_probe::constexprLambdaProbe());
    TEST_ASSERT_EQUAL_INT(17, cpp17_probe::inlineVariable);
}

void setup()
{
    initializeTestEnvironment();

    UNITY_BEGIN();
    RUN_TEST(test_cpp17_runtime_probes);
    exit(UNITY_END());
}

void loop() {}