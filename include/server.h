#ifndef SERVER_H
#define SERVER_H

/* Inclusion des bibliothèques standard */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>

/* Pour les threads (partie 3) */
#include <pthread.h>

/* Pour select (partie 4) */
#include <sys/select.h>

/* Pour syslog (partie 5) */
#include <syslog.h>

/* Constantes communes */
#define PORT            9999
#define BACKLOG         10
#define BUFFER_SIZE     1024
#define MAX_THREADS     16          /* Pool de threads (partie 3) */
#define MAX_CLIENTS     FD_SETSIZE  /* Pour select (partie 4) */

/* Variables globales (avec extern pour les .c) */
extern int compteur_global;          /* Numéro de connexion */
extern pthread_mutex_t mutex_compteur; /* Mutex pour compteur (threads) */
extern int connexions_actives;       /* Pour pool de threads */
extern pthread_mutex_t mutex_actives;

/* Prototypes des fonctions des différents modules */
/* daemon.c */
void daemonize(const char *pidfile);
void verifier_single_instance(const char *pidfile);
void gestion_signal(int sig);

/* handler.c */
void handle_client(int connfd, int num_connexion);
void *handle_client_thread(void *arg);
void afficher_statut(void);

/* utils.c */
void initialiser_compteur(void);
void incrementer_compteur(void);
int get_compteur(void);
void initialiser_actives(void);
void incrementer_actives(void);
void decrementer_actives(void);

/* server.c (déclarations internes) */
void serveur_iteratif(int listenfd);
void serveur_fork(int listenfd);
void serveur_thread(int listenfd);
void serveur_select(int listenfd);
void serveur_daemon(int listenfd);   // utilise fork

#endif