/*
 * Minimal microshell wrapper for MeowOS
 * Provides a simple shell interface without complex filesystem abstraction
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("microshell: MeowOS integration shell\n");
    printf("Type 'help' for available commands, 'exit' to quit\n");

    char line[256];
    while (1) {
        printf("$ ");
        fflush(stdout);
        
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }

        // Check for exit command
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            printf("Goodbye!\n");
            break;
        }

        // Basic echo command for testing
        if (strncmp(line, "echo ", 5) == 0) {
            printf("%s\n", line + 5);
            continue;
        }

        // Help command
        if (strcmp(line, "help") == 0) {
            printf("Available commands:\n");
            printf("  echo <text>  - Print text\n");
            printf("  help         - Show this help\n");
            printf("  exit/quit    - Exit the shell\n");
            continue;
        }

        if (strlen(line) > 0) {
            printf("Unknown command: %s\n", line);
        }
    }

    return 0;
}
