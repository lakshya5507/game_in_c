#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
  #include <conio.h>
  #include <windows.h>
#else
  #include <signal.h>
  #include <sys/select.h>
  #include <termios.h>
  #include <unistd.h>
#endif

#define WIDTH 30
#define HEIGHT 18

#ifndef _WIN32
static struct termios g_orig_termios;
static int g_termios_active = 0;

static void restore_terminal(void) {
    if (g_termios_active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
        g_termios_active = 0;
    }
    printf("\033[?25h\033[0m");
    fflush(stdout);
}

static void handle_sigint(int sig) {
    (void)sig;
    restore_terminal();
    _exit(0);
}

static void enable_raw_terminal(void) {
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &g_orig_termios) == 0) {
        struct termios raw = g_orig_termios;
        raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        g_termios_active = 1;
        atexit(restore_terminal);
        signal(SIGINT, handle_sigint);
        signal(SIGTERM, handle_sigint);
    }
    printf("\033[?25l\033[2J\033[H");
    fflush(stdout);
}

static int kbhit_portable(void) {
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

static int getch_portable(void) {
    unsigned char ch = 0;
    if (read(STDIN_FILENO, &ch, 1) == 1) {
        return (int)ch;
    }
    return -1;
}

static void sleep_ms(int ms) {
    usleep((useconds_t)(ms * 1000));
}
#else
static void enable_raw_terminal(void) {
    system("cls");
}

static void restore_terminal(void) {
}

static int kbhit_portable(void) {
    return _kbhit();
}

static int getch_portable(void) {
    return _getch();
}

static void sleep_ms(int ms) {
    Sleep((DWORD)ms);
}
#endif

static void clear_frame(void) {
#ifdef _WIN32
    system("cls");
#else
    printf("\033[H");
#endif
}

int main(void) {
    srand((unsigned int)time(NULL));
    enable_raw_terminal();

    int bestScore = 0;
    int playAgain = 1;

    while (playAgain) {
        int playerX = WIDTH / 2;
        int targetX = rand() % (WIDTH - 2) + 1;
        int targetY = 1;

        int bulletX = -1;
        int bulletY = -1;
        int bulletActive = 0;

        int score = 0;
        int gameOver = 0;
        int quitRequested = 0;
        int frame = 0;

#ifndef _WIN32
        printf("\033[2J\033[H");
#endif

        while (!gameOver) {
            while (kbhit_portable()) {
                int ch = getch_portable();
                if (ch == -1) {
                    break;
                }
                if (ch == 27) {
                    /* Handle ANSI arrow keys ESC [ C / ESC [ D */
                    if (kbhit_portable()) {
                        int seq1 = getch_portable();
                        if (seq1 == '[' && kbhit_portable()) {
                            int seq2 = getch_portable();
                            if (seq2 == 'D' && playerX > 1) {
                                playerX--;
                            } else if (seq2 == 'C' && playerX < WIDTH - 2) {
                                playerX++;
                            }
                        }
                    }
                } else if ((ch == 'a' || ch == 'A') && playerX > 1) {
                    playerX--;
                } else if ((ch == 'd' || ch == 'D') && playerX < WIDTH - 2) {
                    playerX++;
                } else if (ch == ' ' && !bulletActive) {
                    bulletActive = 1;
                    bulletX = playerX;
                    bulletY = HEIGHT - 2;
                } else if (ch == 'q' || ch == 'Q') {
                    gameOver = 1;
                    quitRequested = 1;
                }
            }

            if (bulletActive) {
                bulletY--;
                if (bulletY == targetY && bulletX == targetX) {
                    score += 10;
                    if (score > bestScore) {
                        bestScore = score;
                    }
                    bulletActive = 0;
                    targetX = rand() % (WIDTH - 2) + 1;
                    targetY = 1;
                } else if (bulletY < 1) {
                    bulletActive = 0;
                }
            }

            frame++;
            if (!gameOver && frame % 4 == 0) {
                targetY++;
                if (targetY >= HEIGHT - 1) {
                    gameOver = 1;
                }
            }

            clear_frame();
            printf("Score: %-4d | Best: %-4d | Move: A/D | Shoot: SPACE | Quit: Q\n",
                   score, bestScore);

            for (int y = 0; y < HEIGHT; y++) {
                for (int x = 0; x < WIDTH; x++) {
                    if (y == 0 || y == HEIGHT - 1) {
                        putchar('#');
                    } else if (x == 0 || x == WIDTH - 1) {
                        putchar('#');
                    } else if (y == targetY && x == targetX) {
                        putchar('O');
                    } else if (bulletActive && y == bulletY && x == bulletX) {
                        putchar('|');
                    } else if (y == HEIGHT - 2 && x == playerX) {
                        putchar('^');
                    } else {
                        putchar(' ');
                    }
                }
                putchar('\n');
            }
            fflush(stdout);

            sleep_ms(50);
        }

        if (quitRequested) {
            playAgain = 0;
            break;
        }

        printf("\nGame Over! Final Score: %d (Best: %d)\n", score, bestScore);
        printf("Press R to Play Again or Q to Quit...\n");
        fflush(stdout);

        int waiting = 1;
        while (waiting) {
            if (kbhit_portable()) {
                int ch = getch_portable();
                if (ch == 'r' || ch == 'R' || ch == '\n' || ch == '\r') {
                    waiting = 0;
                    playAgain = 1;
                } else if (ch == 'q' || ch == 'Q') {
                    waiting = 0;
                    playAgain = 0;
                }
            }
            sleep_ms(30);
        }
    }

    printf("\nSession ended. Final Best Score: %d\n", bestScore);
    restore_terminal();
    return 0;
}
