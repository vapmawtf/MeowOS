#include <meow/userland/init.h>
#include <meow/vga.h>
#include <meow/io.h>
#include <meow/userland/shell.h>
#include <meow/storage.h>
#include <meow/vfs.h>

void init_userland()
{
    vfs_init();
    storage_init();
    printf("Initializing userland...\n");
    printf("Welcome to MeowOS!\n");
printf(" _____               _____ _____ \n");
printf("|     |___ ___ _ _ _|     |   __|\n");
printf("| | | | -_| . | | | |  |  |__   |\n");
printf("|_|_|_|___|___|_____|_____|_____|\n");
printf("\n");   
    shell_main();
}