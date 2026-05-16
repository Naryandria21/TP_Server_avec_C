# Rapport d’analyse – Serveur UNIX (Parties 1 à 5)
---
### Auteurs:

 -ANDRIANARISON Nary

 -ANDRIANTOAVINA Miaro Ny Aina

 -RANDRIAMANANTSOA Tafita Hariniaina

### Licence 3 en Telecommunication

---

## Introduction

Ce TP nous a demandé de construire un serveur TCP en C sur Linux capable de gérer plusieurs clients, de tracer son activité et de fonctionner en arrière-plan. Dans ce rapport,nous nous basons principalement sur le serveur multi‑threadé (Partie 3) car c’est celui qui nous semble le plus adapté à un service en production modeste.Contrairement à la version fork(), les threads partagent la mémoire facilement et évitent de dupliquer tout l’espace du serveur. Ils restent plus simples à programmer que select() pour une centaine de clients. Nous avions déjà vu les concepts en cours, mais les mettre en pratique… c’est autre chose.Nous allons raconter ce que nous avons fait, les problèmes rencontrés, et ce que nous avons appris. Les captures d’écran sont là pour montrer que ça tourne (ou pas) sur nos machines.

---

## Partie 1 – Serveur itératif (un client à la fois)

### Ce que nous avons fait

Nous avons codé un serveur basique. Il crée une  socket(), l’attache avec bind() au port 9999, écoute listen(), puis accepte accept()les clients un par un. Pour chaque client, il lit read() un message et renvoie “Echo : ” plus le message.

### Test et comportement

Nous avons lancé deux clients en même temps avec `nc localhost 9999`. Le premier client envoie “coucou”, le serveur répond. Pendant ce temps, le deuxième client reste bloqué. Quand le premier se déconnecte, le second est servi.

`[CAPTURE – deux terminaux montrant l’attente]`
<div align="center">
<img src="TP_Server_avec_C/server.png" alt="description" width="40%">
<img src="TP_Server_avec_C/server1.png" alt="description" width="40%">
<img src="TP_Server_avec_C/server2.png" alt="description" width="40%">
<img src="TP_Server_avec_C/server3.png" alt="description" width="40%">
</div>



**Pourquoi ce comportement ?**  
Le serveur fait `accept()`, puis `read()`, puis `write()`, puis `close()`. Il ne retourne à `accept()` qu’après avoir fini de traiter le client. C’est un serveur “itératif” : il traite les demandes en file d’attente. Pas de parallélisme.

C’est simple à coder mais pas efficace si un client met du temps à envoyer ses données.

---

## Partie 2 – Serveur concurrent avec fork()

### Architecture

Après chaque `accept()`, nous appelons `fork()`. Le processus fils ferme la socket d’écoute (`listenfd`) et exécute `handle_client()`, tandis que le père ferme la socket client (`connfd`) et continue d’accepter de nouvelles connexions.

### Gestion des zombies

Lors des premiers tests avec plusieurs clients, la commande `ps aux | grep defunct` a révélé des processus zombies (`<defunct>`).  



`[CAPTURE – ps aux avec zombies]`
![Deux clients : le second attend](images/server.png)
<div align="center">
<img src="TP_Server_avec_C/fork zombie.png" alt="description" width="75%">
</div>

### Interpretation de la PID 
```c
miaro    14949  0.0  0.0  2752 1812 pts/2    S+  21:01  0:00 ./server --fork
miaro    14996  0.0  0.0     0    0 pts/2    Z+  21:01  0:00 [server] <defunct>
miaro    14997  0.0  0.0     0    0 pts/2    Z+  21:01  0:00 [server] <defunct>
miaro    14998  0.0  0.0     0    0 pts/2    Z+  21:01  0:00 [server] <defunct>

```

 Ligne 1 : le processus père ./server --fork (PID 14949) est en cours d’exécution (statut S+ = sommeil, premier plan).

 Lignes suivantes : trois processus fils (PIDs 14996, 14997, 14998) sont zombies : leur état est Z+ (defunct). Ils n’utilisent presque plus de mémoire (VSZ=0, RSS=0), mais ils restent dans la table des processus.

### Compter les connexions actives

Le sujet demandait un compteur de connexions actives que le père puisse lire. Mais avec `fork()`, le père et les fils n’ont pas la même mémoire. Nous avons hésité entre fichier, pipe, mémoire partagée. Nous avons choisi un fichier temporaire dans `/tmp` : chaque fils incrémente un compteur dans le fichier (avec verrouillage `flock`), le père lit le fichier. Pas très élégant mais ça marche. Un autre membre du groupe a utilisé la mémoire partagée POSIX (`shm_open`) – c’est plus propre.

### Test avec 8 clients simultanés

Nous avons utilisé le script bash fourni. Les 8 clients sont lancés presque en même temps. Dans `ps aux`, nous avons vu 8 processus fils apparaître, puis disparaître. Les réponses sont arrivées en même temps (à quelques millisecondes près). Cela prouve le parallélisme.

`[CAPTURE – sortie du script + extrait de ps]`
<div align="center">
<img src="TP_Server_avec_C/fork !zombie.png" alt="description" width="75%">
</div>

### Interpretation de la PID 
```c
miaro 	15195	0.0	0.0	2752	1812	pts/s	S+	21:06	0:00	./server --fork	
miaro	15257	0.0	0.0	6692	4276	pts/s

```

 Ligne 1 : Processus père :le processus père ./server --fork (PID 15195) est en cours d’exécution (statut S+ = sommeil, premier plan).

 Lignes suivantes : (PIDs 15257) Commande grep elle-même, sans rapport avec le serveur.

**Ce que nous avons compris** : `fork()` est simple à comprendre mais lourd. Chaque fils a sa propre copie de toute la mémoire du père. Pour 8 clients, ça va, mais pour 1000, la machine s’épuise.



## Partie 3 – Version multi‑threadée (pthreads)

### Changement de modèle

on remplace fork() par des threads (pthread_create). On protège un compteur global de connexions actives avec un mutex. On limite à 16 threads simultanés (pool fixe) et on refuse toute nouvelle connexion au‑delà. Enfin, on compare la consommation mémoire avec la version fork() dans le rapport.


<div align="center">
<img src="TP_Server_avec_C/threads limite.png" alt="description" width="100%">
</div>

Le pool de threads de taille fixe (MAX_THREADS=16) m’a demandé un peu de réflexion. Nous avons déclaré un compteur `actifs` protégé par le même mutex. Si le client arrive alors qu’on a déjà 16 threads en vie, le serveur envoie un message d’erreur « Service busy » et ferme la connexion. Pas idéal pour l’utilisateur, mais ça protège le serveur.

**Comparaison mémoire fork vs threads** – j’ai regardé `/proc/PID/status` pour un processus père et pour un thread (en fait le PID du processus léger). Sous 8 clients :

<div align="center">
<img src="TP_Server_avec_C/fork vs threads 1.png" alt="description" width="100%">
</div>

*[Capture : plusieurs clients connectés en même temps, la modele FORK` montre 8 connexions établies]*

<div align="center">
<img src="TP_Server_avec_C/fork vs threads 2.png" alt="description" width="100%">
</div>

*[Capture : plusieurs clients connectés en même temps, la modele thread` montre 8 connexions établies]*

- Version fork : 8 processus × ~4,5 Mo = 36 Mo (VmRSS)
- Version threads : 1 processus + 8 threads = ~8,5 Mo au total

La différence vient du partage de la mémoire heap et des segments de code. Avec `fork()`, chaque fils duplique tout. Avec les threads, tout est partagé. Donc les threads économisent beaucoup de RAM.

| Modèle | Mémoire (8 clients) | Latence création | Complexité code |
|--------|---------------------|------------------|------------------|
| fork   | ~36 Mo              | plus lente (clone processus) | moyenne (gestion SIGCHLD) |
| threads| ~8,5 Mo             | très rapide      | plus délicate (mutex, passage fd) |

Mon avis : pour un petit service, `fork()` est plus simple à écrire sans erreur. Pour un serveur qui doit tenir des milliers de clients, les threads s’imposent, mais il faut être très rigoureux sur les accès partagés.

## Partie 4 – select() : un seul thread pour tous

Là nous avons complètement changé d’approche : plus de `fork()`, plus de `pthread_create()`. Un seul thread, une boucle avec `select()`. Nous avons déclaré un tableau `clients[FD_SETSIZE]` initialisé à -1. À chaque itération, je reconstruis la `fd_set` à partir de ce tableau. `select()` surveille `listenfd` et tous les `connfd` actifs.

Quand `select()` dit que `listenfd` est lisible, il appelle `accept()` et nous ajoutons le nouveau `connfd` dans le tableau. Quand c’est un `connfd` existant qui est prêt, nous fassions `read()`. Si `read()` retourne 0, c’est une déconnexion propre : nous ferme le descripteur et nous remettons son emplacement à -1.

*[Capture : boucle du serveur avec `select()` – on voit l’affichage du nombre de descripteurs surveillés après chaque tour]*

Dans cette premier partie ,on voit qu'il utilise l'appel systeme pour surveiller select() pour surveiller 3 descripteur de fichier composer de du socket du nouveaux clients en ecoute et 2 deja connectes. 
<div align="center">
<img src="TP_Server_avec_C/selecte description avec 2 client .png" alt="description" width="100%">
Mais dans cette partie la,on constate que l'un de 2 client est deconnectes.ce qui a permi au serveur d'intercepter instantanements l'evenement et permer de surveiller 2 descripteur de maniere fluide et sans bloc d'application.
<img src="TP_Server_avec_C/select, je ferme l'un de client et il revien 2 .png" alt="description" width="100%">
</div>

**Petite difficulté** : au début nous oublions de réinitialiser la `fd_set` avant chaque `select()`. Du coup les descripteurs fermés restaient dans le set, et `select()` retournait tout le temps des erreurs. Nous avons corrigé avec `FD_ZERO` et en rajoutant les bons FDs à chaque itération.

**Réponses aux quatre questions (demandées dans le rapport) :**

1. *Limite fondamentale de `select()` absente dans `poll()` ?*  
   `select()` a une limite fixe `FD_SETSIZE` (souvent 1024). On ne peut pas surveiller plus de 1024 descripteurs sans recompiler le noyau. `poll()` n’a pas cette limite (juste la mémoire disponible).

2. *Pourquoi `FD_SETSIZE=1024` est un problème en production ?*  
   Parce que des serveurs modernes gèrent facilement plusieurs milliers de connexions (ex : serveur web). Avec `select()`, on ne peut pas dépasser 1024 clients en même temps, c’est trop juste.

3. *Dans quel cas préférer `poll()` à `select()` pour 500 connexions ?*  
   À 500 connexions, les deux marchent. Mais `poll()` est plus pratique car il n’y a pas besoin de reconstruire tout l’ensemble à chaque fois. Aussi `select()` modifie les `fd_set` – on doit les recopier. Donc nous préfèrerons `poll()` pour la propreté du code.

4. *Syscall recommandée pour 10 000+ connexions (problème C10K) ?*  
   `epoll()` (sous Linux) . `epoll` est scalable en O(1) – il ne parcourt pas tous les descripteurs à chaque appel. C’est ce que utilisent nginx ou Redis.

Nous n’avons pas implémenté `poll()` dans le code, mais nous avons bien compris l’intérêt.

## Partie 5 – Daemoniser et passer par syslog

Nous avons pris la version `fork()` de la Partie 2 et nous l’avons transformée en daemon. La séquence à suivre est stricte :

- Premier `fork()` – le père se termine.
- `setsid()` pour créer une nouvelle session, sans terminal.
- Deuxième `fork()` – le premier fils se termine.
- `chdir("/")`, `umask(0)`.
- Rediriger `stdin`, `stdout`, `stderr` vers `/dev/null`.

Nous avons aussi écrit le PID dans `/var/run/myserverd.pid`. Au démarrage, nous vérifions si ce fichier existe et si le PID contenu correspond à un processus vivant. Si oui, le serveur refuse de se lancer (pas de double instance).

*[Capture : `sudo ./server` puis `ps aux | grep server` – le daemon tourne, plus attaché au terminal]*

```
$ pgrep tcp_server
6190
(fermeture du terminal)
$ pgrep tcp_server
6190  (même PID, daemon toujours actif)
$ ./tcp_server
Log : myserverd[6250]: Daemon déjà en cours (PID=6190) 

```

Pour les logs, nous avons remplacé tous mes `printf` par `syslog()`. Avant la boucle, j’appelle `openlog("myserverd", LOG_PID | LOG_CONS, LOG_DAEMON)`. Les niveaux utilisés :

- CLOG_INFO`   : connexions normales
- `LOG_WARNING` : erreurs récupérables (accept échoué)
- `LOG_ERR` : erreurs fatales (socket, bind, fork échoués)

*[Capture : extrait de `/var/log/myserverd.log` avec les dates, les PID entre crochets, et des messages "Connexion acceptée" / "Client déconnecté"]*

```
$ echo "Bonjour daemon" | nc 127.0.0.1 9999
[Connexion #1] Echo : Bonjour daemon
$ echo "Bonjour daemon" | nc 127.0.0.1 9999
[Connexion #1] Echo : Bonjour daemon

Logs dans /var/log/myserverd.log :
myserverd[6190]: Daemon démarré sur le port 9999
myserverd[6190]: Connexion acceptée de 127.0.0.1:54630
myserverd[6304]: Client traité (fd=5)
myserverd[6250]: Daemon déjà en cours (PID=6190)
Nous avons modifié `/etc/rsyslog.conf` en ajoutant la ligne :

```

```
daemon.*    /var/log/myserverd.log
```

Puis redémarré rsyslog. Ensuite nous avons lancé le daemon, envoyé quelques clients, et `tail -f /var/log/myserverd.log` nous avons bien montré les événements. Ouf.

## Conclusion – quel modèle pour la production ?

Pour conclure,si nous devions choisir pour un vrai service en production, nous partirions sur un **pool de threads**  avec une boucle d’accept gérée par le thread principal. Pourquoi ? Les threads consomment peu de mémoire, ils sont rapides à créer, et le partage de données est pratique . L’inconvénient c’est la complexité des mutex, mais avec des règles simples (une variable = un mutex), ça se maîtrise.Le modèle `fork()` est plus robuste sur l’isolation (un client qui plante ne tue que son processus), mais la consommation mémoire est trop élevée pour beaucoup de clients. Et la communication inter‑processus est pénible.Le multiplexage avec `select()` ou `epoll()` est génial pour les serveurs qui ne font que passer des données (proxy, serveur de chat). Mais dès qu’il y a du calcul ou des accès disque par client, un seul thread devient un goulot d’étranglement. Donc nous réserverons `epoll` à des cas très spécifiques.Au final, ce TP nous a appris qu’il n’y a pas de solution miracle. Chaque modèle a son domaine. Notre préféré reste les threads, parce que nous aime bien la programmation concurrente avec des mutex – même si j’ai passé deux heures à débugger une condition de course sur le compteur de connexions !


