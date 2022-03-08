#ifndef BGNCURSES_H_INCLUDED
#define BGNCURSES_H_INCLUDED

#include <curses.h>



int bg_ncurses_init();

int bg_ncurses_cleanup();

void bg_ncurses_process_events(bg_msg_sink_t * sink);

#endif // BGNCURSES_H_INCLUDED
