#include "server.h"

void daemonize(const char *pidfile) {
    pid_t pid;
    /* Premier fork */
    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) exit(EXIT_SUCCESS);  // père quitte
    /* Fils devient leader d'une nouvelle session */
    if (setsid() < 0) {
        perror("setsid");
        exit(EXIT_FAILURE);
    }
    /* Deuxième fork pour éviter la réacquisition d'un terminal */
    pid = fork();
    if (pid < 0) {
        perror("fork2");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) exit(EXIT_SUCCESS);
    /* Répertoire courant = / */
    if (chdir("/") < 0) {
        perror("chdir");
        exit(EXIT_FAILURE);
    }
    /* Masque de création de fichiers */
    umask(0);
    /* Fermeture des descripteurs standard */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    /* Redirection vers /dev/null */
    open("/dev/null", O_RDWR);   // stdin
    dup(0);                       // stdout
    dup(0);                       // stderr
    /* Écriture du PID */
    FILE *f = fopen(pidfile, "w");
    if (f) {
        fprintf(f, "%d\n", getpid());
        fclose(f);
    }
}

void verifier_single_instance(const char *pidfile) {
    FILE *f = fopen(pidfile, "r");
    if (f) {
        pid_t pid;
        fscanf(f, "%d", &pid);
        fclose(f);
        if (kill(pid, 0) == 0) {
            fprintf(stderr, "Instance déjà en cours (PID %d)\n", pid);
            exit(EXIT_FAILURE);
        }
    }
}

void gestion_signal(int sig) {
    (void)sig;
    // Rien de particulier, utilisée pour SIGCHLD
}