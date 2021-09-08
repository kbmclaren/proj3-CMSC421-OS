# proj3-CMSC421-OS
## Description
In this project, I must implement a linux driver.<br> This driver must take the form of a loadable kernel module. Inside the loadable kernel module, I must implement a virtual character device. Naturally kernels have many virtual character devices, and ours is intended to enable the user to play a game of Reversi(Othello) against the CPU. This project had to be turned in before I met all the project requirements.
## Repo Contents
- module:<br>
    - Makefile: custom makefile.<br>
    - reversi.c: This linux character device driver must implement the game reversi a.k.a. Othello.<br>
- test:<br>
    - README: attribution for the user-space test program provided by the course director/TA's.<br>
    - reversi-program.c: User space test program.<br>
- README<br>
- "finalDesignDoc-Project3-CMSC421-Spring21-UMBC.pdf" : Required Design Document detailing the final design of the project.<br>
- "preliminaryDesignDoc.pdf" : Required Design Document submitted at the start of the project. <br>
