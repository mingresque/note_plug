#include "../src/rules.hpp"
#include <cassert>
#include <string>
#include <string_view>
using namespace noteplug;
int main() {
    static_assert(capitalize(""));
    static_assert(capitalize("- "));
    static_assert(!capitalize("a- "));
    static_assert(capitalize("word. "));
    static_assert(capitalize("word? "));
    static_assert(capitalize("word: "));
    static_assert(capitalize("word! "));
    static_assert(!capitalize("word.  "));
    static_assert(!capitalize("word."));
    static_assert(!capitalize("word\t"));
    static_assert(!capitalize("word\n"));
    static_assert(uppercase('a') == 'A' && uppercase('z') == 'Z' && uppercase('Q') == 'Q');
    static_assert(lowercase('Z') == 'z' && lowercase('1') == '1');
    static_assert(transition('a', false, true) == 'A');
    static_assert(transition('A', true, false) == 'a');
    static_assert(transition('A', true, true) == 'A');
    static_assert(upper_successor("ABC", 0) == 0);
    static_assert(upper_successor(" ABC", 0) == 1);
    static_assert(upper_successor("  ABC", 0) == std::string_view::npos);
    static_assert(upper_successor("aBC", 0) == std::string_view::npos);
    static_assert(upper_successor("", 0) == std::string_view::npos);
    static_assert(space_window("  X", 2).start == 0);
    static_assert(space_window("    X", 4).start == 2);
    static_assert(space_window("X    Y", 3).start == 1);
    static_assert(space_window("X    Y", 3).end == 5);
    static_assert(space_window("a", 0).end == 0);
    static_assert(enter_trim("x   ") == 3);
    static_assert(enter_trim("x    ") == 3);
    static_assert(enter_trim("x  \t ") == 1);
    static_assert(enter_trim("x") == 0);
    static_assert(enter_trim("\t") == 0);
    assert(capitalize(std::string("something. ")));
}
