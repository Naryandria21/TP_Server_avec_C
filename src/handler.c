#include "server.h"

/* Traitement d'un client (identique pour fork et threads) */
void handle_client(int connfd, int num) {
    char buffer[BUFFER_SIZE];
    int n = read(connfd, buffer, sizeof(buffer) - 1);
    if (n < 0) {
        syslog(LOG_ERR, "read: %m");
        close(connfd);
        return;
    }
    if (n == 0) {
        syslog(LOG_INFO, "Client %d déconnecté sans envoyer de données", num);
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

/* Version thread (reçoit un pointeur vers int) */
void *handle_client_thread(void *arg) {
    int *fd_ptr = (int*)arg;
    int connfd = *fd_ptr;
    free(fd_ptr);
    // Incrémentation du compteur (sécurisé par mutex)
    pthread_mutex_lock(&mutex_compteur);
    int num = ++compteur_global;
    pthread_mutex_unlock(&mutex_compteur);
    // Compteur de connexions actives (pool)
    pthread_mutex_lock(&mutex_actives);
    connexions_actives++;
    pthread_mutex_unlock(&mutex_actives);
    handle_client(connfd, num);
    pthread_mutex_lock(&mutex_actives);
    connexions_actives--;
    pthread_mutex_unlock(&mutex_actives);
    return NULL;
}

void afficher_statut(void) {
    pthread_mutex_lock(&mutex_actives);
    int act = connexions_actives;
    pthread_mutex_unlock(&mutex_actives);
    syslog(LOG_INFO, "Connexions actives: %d", act);
}