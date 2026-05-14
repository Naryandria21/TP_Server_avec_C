#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <pthread.h>
#include <syslog.h>
#include <errno.h>

#define PORT 9999
#define BACKLOG 10
#define BUFFER_SIZE 1024
#define MAX_THREADS 16

/* Variables globales */
static int compteur_global = 0;
static pthread_mutex_t mutex_compteur = PTHREAD_MUTEX_INITIALIZER;
static int connexions_actives = 0;
static pthread_mutex_t mutex_actives = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t keep_running = 1;
static int listenfd_global = -1;

/* Prototypes */
void arret(int sig);
int creer_socket(void);
void gerer_client(int connfd, int num);
void *thread_client(void *arg);
void serveur_iteratif(int listenfd);
void serveur_fork(int listenfd);
void serveur_thread(int listenfd);
void serveur_select(int listenfd);
void daemoniser(const char *pidfile);

/* Gestion signal d'arrêt */
void arret(int sig) {
    (void)sig;
    keep_running = 0;
    if (listenfd_global >= 0) close(listenfd_global);
    syslog(LOG_INFO, "Signal recu, arret demande");
}

/* Création et configuration de la socket */
int creer_socket(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(fd);
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        exit(EXIT_FAILURE);
    }
    if (listen(fd, BACKLOG) < 0) {
        perror("listen");
        close(fd);
        exit(EXIT_FAILURE);
    }
    return fd;
}

/* Traitement d'un client (commun à tous les modèles) */
void gerer_client(int connfd, int num) {
    char buffer[BUFFER_SIZE];
    int n = read(connfd, buffer, sizeof(buffer) - 1);
    if (n < 0) {
        syslog(LOG_ERR, "read: %m");
        close(connfd);
        return;
    }
    if (n == 0) {
        syslog(LOG_INFO, "Client %d deconnecte", num);
        close(connfd);
        return;
    }
    buffer[n] = '\0';
    char reponse[BUFFER_SIZE + 50];
    snprintf(reponse, sizeof(reponse), "[Connexion #%d] Echo : %s", num, buffer);
    if (write(connfd, reponse, strlen(reponse)) < 0) {
        syslog(LOG_WARNING, "write: %m");
    }
    close(connfd);
}

/* Version thread */
void *thread_client(void *arg) {
    int *pfd = (int*)arg;
    int connfd = *pfd;
    free(pfd);
    
    pthread_mutex_lock(&mutex_compteur);
    int num = ++compteur_global;
    pthread_mutex_unlock(&mutex_compteur);
    
    pthread_mutex_lock(&mutex_actives);
    connexions_actives++;
    syslog(LOG_DEBUG, "Threads actifs: %d", connexions_actives);
    pthread_mutex_unlock(&mutex_actives);
    
    gerer_client(connfd, num);
    
    pthread_mutex_lock(&mutex_actives);
    connexions_actives--;
    pthread_mutex_unlock(&mutex_actives);
    return NULL;
}

/* PARTIE 1 : Serveur itératif */
void serveur_iteratif(int listenfd) {
    int compteur = 0;
    syslog(LOG_INFO, "Mode iteratif demarre");
    while (keep_running) {
        int client = accept(listenfd, NULL, NULL);
        if (client < 0) {
            if (keep_running) syslog(LOG_WARNING, "accept: %m");
            continue;
        }
        compteur++;
        syslog(LOG_INFO, "Client %d connecte (mode iteratif)", compteur);
        gerer_client(client, compteur);
    }
}

/* PARTIE 2 : Serveur avec fork */
void serveur_fork(int listenfd) {
    int compteur = 0;
    syslog(LOG_INFO, "Mode fork demarre");
    
    /* Ignorer SIGCHLD pour eviter les zombies */
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);
    
    while (keep_running) {
        int client = accept(listenfd, NULL, NULL);
        if (client < 0) {
            if (keep_running) syslog(LOG_WARNING, "accept: %m");
            continue;
        }
        compteur++;
        pid_t pid = fork();
        if (pid == 0) {
            /* Fils */
            close(listenfd);
            gerer_client(client, compteur);
            exit(0);
        } else if (pid > 0) {
            /* Pere */
            close(client);
        } else {
            syslog(LOG_ERR, "fork: %m");
            close(client);
        }
    }
}

/* PARTIE 3 : Serveur avec threads (pool max 16) */
void serveur_thread(int listenfd) {
    syslog(LOG_INFO, "Mode thread demarre (max %d threads)", MAX_THREADS);
    while (keep_running) {
        int client = accept(listenfd, NULL, NULL);
        if (client < 0) {
            if (keep_running) syslog(LOG_WARNING, "accept: %m");
            continue;
        }
        
        /* Verifier le pool */
        pthread_mutex_lock(&mutex_actives);
        if (connexions_actives >= MAX_THREADS) {
            pthread_mutex_unlock(&mutex_actives);
            syslog(LOG_WARNING, "Pool sature, connexion refusee");
            close(client);
            continue;
        }
        pthread_mutex_unlock(&mutex_actives);
        
        int *fd_copy = malloc(sizeof(int));
        if (!fd_copy) {
            close(client);
            continue;
        }
        *fd_copy = client;
        pthread_t tid;
        if (pthread_create(&tid, NULL, thread_client, fd_copy) != 0) {
            syslog(LOG_ERR, "pthread_create: %m");
            free(fd_copy);
            close(client);
        } else {
            pthread_detach(tid);
        }
    }
}

/* PARTIE 4 : Serveur avec select */
void serveur_select(int listenfd) {
    fd_set readfds;
    int clients[FD_SETSIZE];
    for (int i = 0; i < FD_SETSIZE; i++) clients[i] = -1;
    int compteur = 0;
    syslog(LOG_INFO, "Mode select demarre");
    
    while (keep_running) {
        FD_ZERO(&readfds);
        FD_SET(listenfd, &readfds);
        int maxfd = listenfd;
        for (int i = 0; i < FD_SETSIZE; i++) {
            if (clients[i] != -1) {
                FD_SET(clients[i], &readfds);
                if (clients[i] > maxfd) maxfd = clients[i];
            }
        }
        
        struct timeval tv = {1, 0};
        int activity = select(maxfd + 1, &readfds, NULL, NULL, &tv);
        if (activity < 0 && keep_running) {
            syslog(LOG_WARNING, "select: %m");
            continue;
        }
        
        /* Nouvelle connexion */
        if (FD_ISSET(listenfd, &readfds)) {
            int client = accept(listenfd, NULL, NULL);
            if (client >= 0) {
                int i;
                for (i = 0; i < FD_SETSIZE && clients[i] != -1; i++);
                if (i < FD_SETSIZE) {
                    clients[i] = client;
                    syslog(LOG_INFO, "Client %d connecte (select)", i);
                } else {
                    syslog(LOG_WARNING, "Trop de clients, fermeture");
                    close(client);
                }
            }
        }
        
        /* Lire les clients existants */
        for (int i = 0; i < FD_SETSIZE; i++) {
            if (clients[i] != -1 && FD_ISSET(clients[i], &readfds)) {
                char buf[BUFFER_SIZE];
                int n = read(clients[i], buf, sizeof(buf) - 1);
                if (n <= 0) {
                    close(clients[i]);
                    clients[i] = -1;
                } else {
                    buf[n] = '\0';
                    char rep[BUFFER_SIZE + 50];
                    compteur++;
                    snprintf(rep, sizeof(rep), "[Connexion #%d] Echo : %s", compteur, buf);
                    write(clients[i], rep, strlen(rep));
                }
            }
        }
    }
    
    /* Nettoyage */
    for (int i = 0; i < FD_SETSIZE; i++) {
        if (clients[i] != -1) close(clients[i]);
    }
}

/* PARTIE 5 : Daemonisation */
void daemoniser(const char *pidfile) {
    pid_t pid;
    
    /* Premier fork */
    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);
    
    /* Nouvelle session */
    if (setsid() < 0) exit(EXIT_FAILURE);
    
    /* Deuxieme fork */
    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);
    
    /* Repertoire racine */
    if (chdir("/") < 0) exit(EXIT_FAILURE);
    
    /* Masque de creation */
    umask(0);
    
    /* Fermeture des descripteurs standard */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    /* Redirection vers /dev/null */
    open("/dev/null", O_RDWR);
    dup(0);
    dup(0);
    
    /* Ecriture du PID */
    FILE *f = fopen(pidfile, "w");
    if (f) {
        fprintf(f, "%d\n", getpid());
        fclose(f);
    }
}

/* MAIN */
int main(int argc, char *argv[]) {
    int mode = 0;  /* 0=iteratif, 1=fork, 2=thread, 3=select */
    int daemon_mode = 0;
    
    /* Analyse des arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--iterative") == 0) mode = 0;
        else if (strcmp(argv[i], "--fork") == 0) mode = 1;
        else if (strcmp(argv[i], "--thread") == 0) mode = 2;
        else if (strcmp(argv[i], "--select") == 0) mode = 3;
        else if (strcmp(argv[i], "--daemon") == 0) {
            mode = 1;  /* fork par defaut pour daemon */
            daemon_mode = 1;
        }
    }
    
    /* Mode daemon */
    if (daemon_mode) {
        daemoniser("/var/run/myserverd.pid");
        openlog("myserverd", LOG_PID | LOG_CONS, LOG_DAEMON);
        syslog(LOG_INFO, "Serveur demarre en mode daemon (fork)");
    } else {
        openlog("myserverd", LOG_PID | LOG_CONS, LOG_DAEMON);
        printf("Serveur demarre sur le port %d\n", PORT);
        syslog(LOG_INFO, "Serveur demarre en mode interactif");
    }
    
    /* Gestion des signaux */
    signal(SIGINT, arret);
    signal(SIGTERM, arret);
    
    /* Creation socket */
    listenfd_global = creer_socket();
    
    /* Lancement du mode choisi */
    switch (mode) {
        case 0:
            serveur_iteratif(listenfd_global);
            break;
        case 1:
            serveur_fork(listenfd_global);
            break;
        case 2:
            serveur_thread(listenfd_global);
            break;
        case 3:
            serveur_select(listenfd_global);
            break;
        default:
            serveur_fork(listenfd_global);
    }
    
    /* Nettoyage */
    close(listenfd_global);
    syslog(LOG_INFO, "Serveur arrete");
    if (!daemon_mode) printf("Serveur arrete.\n");
    return 0;
}
