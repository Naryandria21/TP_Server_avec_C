

content = """# Structure du Projet TP Serveur en C

Ce document détaille la structure du projet, les instructions de compilation et les modes d'exécution pour le Travaux Pratiques (TP) du serveur.

## Structure du Répertoire

La structure suivante doit être respectée pour l'organisation des fichiers source et des en-têtes :

```text
tp_final/
├── src/
│   ├── server.c          # Serveur principal (tout-en-un ou modulaire)
│   ├── daemon.c          # Fonctions de démonisation (si séparé)
│   ├── handler.c         # Gestionnaires de clients
│   └── utils.c           # Fonctions utilitaires (compteur, mutex, etc.)
├── include/
│   └── server.h          # Prototypes, constantes, inclusions
├── Makefile              # Compilation obligatoire
├── syslog.conf.example   # Exemple de configuration syslog
└── rapport.md            # Rapport d'analyse (5 pages max)

```

---

## Compilation

Pour compiler le projet, utilisez la commande `make` à la racine du dossier :

```bash
make

```

---

## Modes d'Exécution

Le serveur supporte différents modes de gestion des connexions. Voici les commandes pour lancer chaque partie du TP :

### 1. Mode itératif (Partie 1)

```bash
./server --iterative

```

### 2. Mode fork (Partie 2)

```bash
./server --fork

```

### 3. Mode threads (Partie 3)

```bash
./server --threads

```

### 4. Mode select (Partie 4)

```bash
./server --select

```

### 5. Mode daemon (Partie 5)

*Note : Nécessite des privilèges administrateur pour écrire le fichier PID.*

```bash
sudo ./server --daemon

```

---

##  Test du Serveur

Pour tester si le serveur fonctionne correctement, ouvrez un second terminal et utilisez la commande `nc` (Netcat) :

```bash
nc localhost 9999

```

**Résultat attendu :**
Après avoir tapé un message, le serveur doit vous renvoyer :
[Connexion #X] Echo : <votre_message>
"""
