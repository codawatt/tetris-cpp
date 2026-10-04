#pragma once

// Small POSIX terminal console helper for Linux.
//
// - No X11 dependency
// - Works under X11, Wayland, TTY and SSH
// - ANSI escape sequences for drawing
// - termios + nonblocking stdin for keyboard input
//
// Arrow keys are parsed from terminal escape sequences.
// Character keys such as 'z' and 'q' are read directly.

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace linux_console {

inline volatile sig_atomic_t interrupted = 0;

inline void on_interrupt(int) {
    interrupted = 1;
}

enum class Key {
    Left,
    Right,
    Up,
    Down
};

class Console {
    int width_ = 0;
    int height_ = 0;

    // Original terminal settings, restored on exit
    termios old_termios_{};
    int old_stdin_flags_ = -1;

    bool termios_saved_ = false;
    bool flags_saved_ = false;
    bool screen_active_ = false;
    bool ready_ = false;

    // Keys detected during the current poll()
    std::array<bool, 256> chars_{};

    bool left_ = false;
    bool right_ = false;
    bool up_ = false;
    bool down_ = false;

    // Terminal escape sequences can be split across read() calls,
    // so preserve incomplete input between polls.
    std::string pending_;

    void mark_char(unsigned char c) {
        chars_[c] = true;

        // Treat alphabetic controls as case-insensitive.
        // So 'z' also works if Caps Lock or Shift produces 'Z'.
        if (c >= 'a' && c <= 'z') {
            chars_[static_cast<unsigned char>(c - 'a' + 'A')] = true;
        } else if (c >= 'A' && c <= 'Z') {
            chars_[static_cast<unsigned char>(c - 'A' + 'a')] = true;
        }
    }

    void mark_arrow(char final) {
        switch (final) {
        case 'A':
            up_ = true;
            break;

        case 'B':
            down_ = true;
            break;

        case 'C':
            right_ = true;
            break;

        case 'D':
            left_ = true;
            break;

        default:
            break;
        }
    }

    void parse_input() {
        std::size_t i = 0;

        while (i < pending_.size()) {
            const unsigned char c =
                static_cast<unsigned char>(pending_[i]);

            // Normal character
            if (c != 0x1B) {
                mark_char(c);
                ++i;
                continue;
            }

            // ESC at end of buffer:
            // sequence may continue during next poll()
            if (i + 1 >= pending_.size())
                break;

            // Some terminals use:
            //
            // ESC O A
            // ESC O B
            // ESC O C
            // ESC O D
            //
            if (pending_[i + 1] == 'O') {
                if (i + 2 >= pending_.size())
                    break;

                mark_arrow(pending_[i + 2]);
                i += 3;
                continue;
            }

            // Normal cursor-key sequence:
            //
            // ESC [ A
            // ESC [ B
            // ESC [ C
            // ESC [ D
            //
            // Also handles modified forms such as:
            //
            // ESC [ 1 ; 5 C
            //
            if (pending_[i + 1] == '[') {
                std::size_t j = i + 2;

                // CSI sequences finish with a byte from 0x40..0x7E
                while (j < pending_.size()) {
                    const unsigned char final =
                        static_cast<unsigned char>(pending_[j]);

                    if (final >= 0x40 && final <= 0x7E)
                        break;

                    ++j;
                }

                if (j >= pending_.size()) {
                    // Incomplete sequence.
                    // Keep it until next poll().
                    break;
                }

                mark_arrow(pending_[j]);

                i = j + 1;
                continue;
            }

            // Unknown ESC sequence.
            // Ignore ESC and process following byte normally.
            ++i;
        }

        pending_.erase(0, i);

        // Safety against malformed input growing forever
        if (pending_.size() > 64)
            pending_.clear();
    }

public:
    Console(int width, int height)
        : width_(width), height_(height) {

        // We need both stdin and stdout connected to a terminal.
        if (!isatty(STDIN_FILENO) ||
            !isatty(STDOUT_FILENO)) {

            std::cerr
                << "Run this program in an interactive terminal.\n";

            return;
        }

        // Check terminal dimensions
        winsize size{};

        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 &&
            (size.ws_col < width || size.ws_row < height)) {

            std::cerr
                << "Terminal must be at least "
                << width << " columns by "
                << height << " rows (currently "
                << size.ws_col << "x"
                << size.ws_row << ").\n";

            return;
        }

        // Save current terminal configuration
        if (tcgetattr(STDIN_FILENO, &old_termios_) == -1) {
            std::cerr
                << "tcgetattr failed: "
                << std::strerror(errno)
                << '\n';

            return;
        }

        termios_saved_ = true;

        // Configure terminal for immediate key input.
        //
        // Keep ISIG enabled so Ctrl+C still generates SIGINT.
        termios raw = old_termios_;

        raw.c_lflag &=
            static_cast<tcflag_t>(~(ICANON | ECHO));

        raw.c_iflag &=
            static_cast<tcflag_t>(~(IXON | ICRNL));

        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;

        if (tcsetattr(
                STDIN_FILENO,
                TCSANOW,
                &raw) == -1) {

            std::cerr
                << "tcsetattr failed: "
                << std::strerror(errno)
                << '\n';

            return;
        }

        // Make stdin nonblocking
        old_stdin_flags_ =
            fcntl(STDIN_FILENO, F_GETFL, 0);

        if (old_stdin_flags_ == -1) {
            std::cerr
                << "fcntl(F_GETFL) failed: "
                << std::strerror(errno)
                << '\n';

            tcsetattr(
                STDIN_FILENO,
                TCSANOW,
                &old_termios_);

            termios_saved_ = false;

            return;
        }

        flags_saved_ = true;

        if (fcntl(
                STDIN_FILENO,
                F_SETFL,
                old_stdin_flags_ | O_NONBLOCK) == -1) {

            std::cerr
                << "fcntl(F_SETFL) failed: "
                << std::strerror(errno)
                << '\n';

            tcsetattr(
                STDIN_FILENO,
                TCSANOW,
                &old_termios_);

            termios_saved_ = false;
            flags_saved_ = false;

            return;
        }

        std::signal(SIGINT, on_interrupt);
        std::signal(SIGTERM, on_interrupt);

        // Alternate screen
        // Hide cursor
        // Clear screen
        std::cout
            << "\x1b[?1049h"
            << "\x1b[?25l"
            << "\x1b[2J"
            << std::flush;

        screen_active_ = true;
        ready_ = true;
    }

    ~Console() {
        close();
    }

    // Console owns terminal state, so copying it would be dangerous.
    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;

    explicit operator bool() const {
        return ready_;
    }

    void close() {
        if (screen_active_) {
            // Show cursor and leave alternate screen
            std::cout
                << "\x1b[?25h"
                << "\x1b[?1049l"
                << std::flush;

            screen_active_ = false;
        }

        if (flags_saved_) {
            fcntl(
                STDIN_FILENO,
                F_SETFL,
                old_stdin_flags_);

            flags_saved_ = false;
        }

        if (termios_saved_) {
            tcsetattr(
                STDIN_FILENO,
                TCSANOW,
                &old_termios_);

            termios_saved_ = false;
        }

        ready_ = false;
    }

    void poll() {
        // These represent keys received since the previous poll().
        chars_.fill(false);

        left_ = false;
        right_ = false;
        up_ = false;
        down_ = false;

        if (!ready_)
            return;

        char buffer[128];

        for (;;) {
            const ssize_t n =
                ::read(
                    STDIN_FILENO,
                    buffer,
                    sizeof(buffer));

            if (n > 0) {
                pending_.append(
                    buffer,
                    static_cast<std::size_t>(n));

                continue;
            }

            if (n == -1 && errno == EINTR)
                continue;

            if (n == -1 &&
                errno != EAGAIN &&
                errno != EWOULDBLOCK) {

                interrupted = 1;
            }

            break;
        }

        parse_input();
    }

    bool down(char c) const {
        return chars_[
            static_cast<unsigned char>(c)
        ];
    }

    bool down(Key key) const {
        switch (key) {
        case Key::Left:
            return left_;

        case Key::Right:
            return right_;

        case Key::Up:
            return up_;

        case Key::Down:
            return down_;
        }

        return false;
    }

    void draw(const wchar_t* cells, const unsigned char* colors = nullptr) const {
        std::string frame;

        frame.reserve(
            width_ * height_ * 3 +
            height_ * 12
        );

        int active_color = -1;

        for (int y = 0; y < height_; ++y) {
            // Move cursor to beginning of row
            frame +=
                "\x1b[" +
                std::to_string(y + 1) +
                ";1H";

            for (int x = 0; x < width_; ++x) {
                const int index = y * width_ + x;

                const unsigned char color =
                    colors ? colors[index] : 0;

                if (color != active_color) {
                    if (color == 0) {
                        // Restore terminal's default foreground color
                        frame += "\x1b[39m";
                    } else {
                        // ANSI 256-color foreground
                        frame += "\x1b[38;5;";
                        frame += std::to_string(color);
                        frame += "m";
                    }

                    active_color = color;
                }

                const wchar_t c =
                    cells[index];

                switch (c) {
                case 0x2588:
                    frame += "\xE2\x96\x88";
                    break;

                case 0x2593:
                    frame += "\xE2\x96\x93";
                    break;

                case 0x2592:
                    frame += "\xE2\x96\x92";
                    break;

                case 0x2591:
                    frame += "\xE2\x96\x91";
                    break;

                default:
                    frame +=
                        (c >= 32 && c < 127)
                            ? char(c)
                            : ' ';

                    break;
                }
            }
        }
        frame += "\x1b[39m";
        std::cout << frame << std::flush;
    }
};

} // namespace linux_console