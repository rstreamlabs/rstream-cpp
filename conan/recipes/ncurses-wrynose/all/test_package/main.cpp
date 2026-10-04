#include <ncurses.h>
#include <term.h>
// Include standard headers afterwards to catch ncurses redefining C++ bool.
#include <cstdio>
#include <type_traits>

static_assert(std::is_same_v<decltype(true), bool>);

int main() {
    auto* input = std::tmpfile();
    auto* output = std::tmpfile();
    if (!input || !output) return 1;
    auto* screen = newterm("xterm-256color", output, input);
    if (!screen) return 2;
    set_term(screen);
    auto* window = newwin(3, 30, 0, 0);
    if (!window || waddstr(window, "Yocto ncurses") == ERR) return 3;
    if ((mvwinch(window, 0, 0) & A_CHARTEXT) != 'Y') return 4;
    if (ungetch('x') == ERR || wgetch(window) != 'x') return 5;
    auto* clear = tigetstr("clear");
    if (!clear || clear == reinterpret_cast<char*>(-1)) return 6;
    delwin(window);
    endwin();
    delscreen(screen);
    std::fclose(input);
    std::fclose(output);
    std::puts("ncurses-runtime-ok");
}
