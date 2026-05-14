#include "server.h"

/* Compteur global (pour les modèles sans mutex) */
static int compteur = 0;

void initialiser_compteur(void) {
    compteur = 0;
}

void incrementer_compteur(void) {
    compteur++;
}

int get_compteur(void) {
    return compteur;
}

void initialiser_actives(void) {
    // Rien, car connexions_actives est globale avec mutex
}

void incrementer_actives(void) {
    pthread_mutex_lock(&mutex_actives);
    connexions_actives++;
    pthread_mutex_unlock(&mutex_actives);
}

void decrementer_actives(void) {
    pthread_mutex_lock(&mutex_actives);
    connexions_actives--;
    pthread_mutex_unlock(&mutex_actives);
}