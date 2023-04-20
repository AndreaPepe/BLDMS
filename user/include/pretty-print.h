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


#define print_color(color) \
            printf("\033[0;%dm", color)

#define print_color_bold(color) \
            printf("\033[1;%dm", color)

#define reset_color() \
            print_color(DEFAULT)

#endif