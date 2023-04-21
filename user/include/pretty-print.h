#pragma once
#ifndef __PRETTY_PRINT_H__
#define __PRETTY_PRINT_H__

#include <stdio.h>

#define DEFAULT     0
#define BLACK       30
#define RED         31
#define GREEN       32
#define YELLOW      33
#define BLUE        34
#define MAGENTA     35
#define CYAN        36
#define WHITE       37

#define DEFAULT_STR "\033[0;0m"
#define BLACK_STR "\033[1;30m"
#define RED_STR "\033[1;31m"
#define GREEN_STR "\033[1;32m"
#define YELLOW_STR "\033[1;33m"
#define BLUE_STR "\033[1;34m"
#define MAGENTA_STR "\033[1;35m"
#define CYAN_STR "\033[1;36m"
#define WHITE_STR "\033[1;37m"


#define print_color(color) \
            printf("\033[0;%dm", color)

#define print_color_bold(color) \
            printf("\033[1;%dm", color)

#define reset_color() \
            print_color(DEFAULT)

#endif