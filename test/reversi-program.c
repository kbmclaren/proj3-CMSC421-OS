/*
    reversi-program.c -- Interface program for /dev/reversi.

    Copyright (C) 2021 Lawrence Sebald

    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define RESP_MAX    1024
#define BOARD_LEN   67
#define BOARD_DIM   8

static int print_game_board(const char *bd, ssize_t bdl) {
    int i;

    if(bdl != BOARD_LEN) {
        fprintf(stderr, "Game board response of invalid length: %d\n",
                (int)bdl);
        printf("%s\n", bd);
        return -1;
    }

    /* Check the board for validity before printing it... */
    for(i = 0; i < BOARD_LEN - 3; ++i) {
        if(bd[i] != 'X' && bd[i] != 'O' && bd[i] != '-') {
            fprintf(stderr, "Game board response has invalid form\n");
            printf("%s\n", bd);
            return -1;
        }
    }

    if(bd[64] != '\t' || (bd[65] != 'X' && bd[65] != 'O') || bd[66] != '\n') {
        fprintf(stderr, "Game board response has invalid form\n");
        printf("%s\n", bd);
        return -1;
    }

    /* Print it out nicely. */
    for(i = 0; i < BOARD_DIM; ++i) {
        fwrite(bd + (i << 3), 1, 8, stdout);
        fwrite("\n", 1, 1, stdout);
    }

    printf("Next turn: %c\n", bd[65]);
    return 0;
}

int main(int argc, char *argv[]) {
    int fd;
    char *cmd;
    size_t len;
    ssize_t rlen;
    char response[RESP_MAX];

    if((fd = open("/dev/reversi", O_RDWR)) < 0) {
        fprintf(stderr, "Cannot open /dev/reversi: %s\n", strerror(errno));
        return 1;
    }

    for(;;) {
        cmd = NULL;
        len = 0;

        /* Read a command from the user */
        printf("Enter a command (enter QUIT to exit): ");
        if((rlen = getline(&cmd, &len, stdin)) < 0) {
            fprintf(stderr, "Error reading command: %s\n", strerror(errno));
            close(fd);
            return 1;
        }

        /* See if they've asked to exit or not... */
        if(!strcmp(cmd, "QUIT\n")) {
            free(cmd);
            break;
        }

        /* Write it to the device */
        if(write(fd, cmd, (size_t)rlen) != rlen) {
            fprintf(stderr, "Could not write to /dev/reversi\n");
            free(cmd);
            close(fd);
            return 1;
        }

        /* Read back the response. */
        if((rlen = read(fd, response, RESP_MAX)) < 0) {
            fprintf(stderr, "Could not read from /dev/reversi: %s\n",
                    strerror(errno));
            free(cmd);
            close(fd);
            return 1;
        }
        else if(rlen == RESP_MAX) {
            fprintf(stderr, "Read from /dev/reversi is too long (%d bytes)!\n",
                    (int)rlen);
            free(cmd);
            close(fd);
            return 1;
        }

        response[rlen] = 0;

        /* Did the user ask for the game board? If so, print it nicely,
           otherwise just display the raw response */
        if(!strcmp(cmd, "01\n")) {
            if(print_game_board(response, rlen)) {
                free(cmd);
                close(fd);
                return 1;
            }
        }
        else {
            printf("%s\n", response);
        }

        free(cmd);
    }

    close(fd);
    return 0;
}

