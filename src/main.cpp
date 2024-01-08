// Copyright (c) 2018 Martyn Afford
// Licensed under the MIT licence

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <ncursesw/cursesw.h>
#include <random>

namespace {

// RAII wrapper around ncurses.
struct ncurses {
    ncurses()
    {
        setlocale(LC_ALL, "");
        initscr();
        cbreak();
        nonl();
        noecho();
        nodelay(stdscr, true);
        curs_set(0);
    }

    ~ncurses()
    {
        endwin();
    }
};

// Encapsulate a two-dimensional grid.
class grid {
public:
    int width;
    int height;

    grid(int w, int h)
        : width{w}
        , height{h}
        , data{new bool[width * height]}
    {}

    bool&
    operator()(int x, int y)
    {
        return data[((x+width)%width) + ((y+height)%height) * width];
    }

    bool&
    operator()(int x, int y) const
    {
        return data[((x+width)%width) + ((y+height)%height) * width];
    }

    void
    big_bang()
    {
        auto random_int_generator = std::bind(std::uniform_int_distribution<>(0,10),std::default_random_engine());
        for ( int i = 0; i < width*height; i++ ) {
            if (!data[i]) {
                data[i] = bool(random_int_generator()==0);
            }
        }
    }
    void
    invert()
    {
        for ( int i = 0; i < width*height; i++ ) {
            data[i] = !data[i];
        }
    }
    void
    thanos()
    {
        auto random_int_generator = std::bind(std::uniform_int_distribution<>(0,1),std::default_random_engine());
        for ( int i = 0; i < width*height; i++ ) {
            if (data[i]) {
                data[i] = bool(random_int_generator());
            }
        }
    }

private:
    std::unique_ptr<bool[]> data;
};

// Encapsulate two grids to provide double-buffering, where the grids alternate
// being the main (front) buffer or the secondary (back).
class double_buffered_grid {
public:
    double_buffered_grid(int width, int height)
        : a{width, height}
        , b{width, height}
    {}

    grid&
    front()
    {
        return first ? a : b;
    }

    grid&
    back()
    {
        return first ? b : a;
    }

    void
    swap()
    {
        first = !first;
    }

private:
    bool first = true;
    grid a;
    grid b;
};

// Given two grids (of equal size) produce the next generation according to the
// rules of Conway's game of life. The rules are as follows, quoting Wikipedia:
//
// - Any live cell with fewer than two live neighbors dies, as if by
//   underpopulation.
// - Any live cell with two or three live neighbors lives on to the next
//   generation.
// - Any live cell with more than three live neighbors dies, as if by
//   overpopulation.
// - Any dead cell with exactly three live neighbors becomes a live cell, as if
//   by reproduction.
void
next_generation(grid& in, grid& out, bool wrap)
{
    assert(in.width == out.width && in.height == out.height);

    for (auto y = 0; y != in.height; ++y) {
        for (auto x = 0; x != in.width; ++x) {
            auto left = wrap | (x != 0);
            auto up = wrap | (y != 0);
            auto right = wrap | (x != in.width - 1);
            auto down = wrap | (y != in.height - 1);
            int neighbours = (left && up && in(x - 1, y - 1)) +
                             (left && in(x - 1, y)) +
                             (left && down && in(x - 1, y + 1)) +
                             (up && in(x, y - 1)) +
                             (down && in(x, y + 1)) +
                             (right && up && in(x + 1, y - 1)) +
                             (right && in(x + 1, y)) +
                             (right && down && in(x + 1, y + 1));
            out(x, y) = (neighbours == 3 || (in(x, y) && neighbours == 2));
        }
    }
}

// Represent the game of life, including rendering. The constructor generates a
// random board to start.
class game_of_life {
public:
    game_of_life(int width, int height)
        : buffer{width, height}
    {
        auto& grid = buffer.back();
        unsigned long seed = std::chrono::system_clock::now().time_since_epoch().count();
        auto frequency = 3; // lower means more initial alive cells

        std::default_random_engine engine{seed};
        std::uniform_int_distribution<int> distribution{0, frequency};

        for (auto i = 0; i != width * height; ++i) {
            grid(i % width, i / width) = distribution(engine) == 0;
        }

        buffer.swap();
    }

    void
    render()
    {
        assert(buffer.front().height % 2 == 0);

        auto full = "█";
        auto upper = "▀";
        auto lower = "▄";

        auto& grid = buffer.front();

        for (auto y = 0; y != grid.height; y += 2) {
            move(y / 2, 0);

            for (auto x = 0; x != grid.width; ++x) {
                auto top = grid(x, y);
                auto bottom = grid(x, y + 1);

                addstr(top ? (bottom ? full : upper) : (bottom ? lower : " "));
            }
        }

        refresh();
    }

    void
    tick(bool wrap)
    {
        next_generation(buffer.front(), buffer.back(), wrap);
        buffer.swap();
    }

    void
    big_bang()
    {
        buffer.front().big_bang();
    }

    void
    invert()
    {
        buffer.front().invert();
    }
    void
    thanos()
    {
        buffer.front().thanos();
    }
private:
    double_buffered_grid buffer;
};

} // namespace

std::unique_ptr<game_of_life> new_game() {
    auto width = 0;
    auto height = 0;
    getmaxyx(stdscr, height, width);
    auto width_env = std::getenv("GOL_WIDTH");
    auto height_env = std::getenv("GOL_HEIGHT");
    if (width_env) {
        width = std::stoi(width_env);
    }
    if (height_env) {
        height = std::stoi(height_env);
    }
    return std::unique_ptr<game_of_life>(new game_of_life{width, height * 2});
}

int
main()
{
    ncurses nc;
    constexpr auto min_tick_ms = 8;
    constexpr auto max_tick_ms = 1024;
    auto           tick_ms     = min_tick_ms;
    timeout(tick_ms);

    auto game = new_game();
    game->render();
    auto random_int_generator = std::bind(std::uniform_int_distribution<>(0,100000),std::default_random_engine());
    auto running = true;
    bool wrap = true;
    while (true) {
        auto random_selector = random_int_generator();
        auto key = getch();

        // If we resized, generate a new board with the appropriate size
        if (key == KEY_RESIZE || key == 'r') {
            game = new_game();
            game->render();
        }

        // Decrease speed
        if (key == '-' && tick_ms < max_tick_ms) {
            tick_ms *= 2;
            timeout(tick_ms);
        }

        // Increase speed
        if (key == '+' && tick_ms > min_tick_ms) {
            tick_ms /= 2;
            timeout(tick_ms);
        }

        // Play/pause
        if (key == 'w') {
            wrap = !wrap;
        }

        // Play/pause
        if (key == 'p' || key == ' ') {
            running = !running;
        }

        // big bang 
        if (key == 'b' || (running && random_selector == 0)) {
            game->big_bang();
            game->tick(wrap);
            game->render();
        }

        // invert
        if (key == 'i' || (running && random_selector == 1)) {
            game->invert();
            game->render();
        }

        // thanos snap
        if (key == 't' || (running && random_selector == 2)) {
            game->thanos();
            game->render();
        }

        // If the game is paused you can still step through generations
        if (running || key == 's') {
            game->tick(wrap);
            game->render();
        }

        // Quit
        if (key == 'q') {
            break;
        }
    }

    return EXIT_SUCCESS;
}
