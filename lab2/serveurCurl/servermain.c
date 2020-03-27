/* TP2 2020 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Pour recuperer les descriptions d'erreur
#include <errno.h>

// Multiprocessing
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>

// Sockets UNIX
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>

// Signaux UNIX
#include <signal.h>


// Structures et fonctions de communication
#include "communications.h"
// Fonction de téléchargement utilisant cURL
#include "telechargeur.h"
// Structures et fonctions stockant et traitant les requêtes en cours
#include "requete.h"
// Fonctions de la boucle principale
#include "actions.h"

// Chaînes de caractères représentant chaque statut (utile pour l'affichage)
const char* statusDesc[] = {"Inoccupe", "Connexion client etablie", "En cours de telechargement", "Pret a envoyer"};

// Nombre maximal de connexions simultanés
#define MAX_CONNEXIONS 10
// Contient les requetes en cours de traitement
struct requete reqList[MAX_CONNEXIONS];


void gereSignal(int signo) {
    // Fonction affichant des statistiques sur les tâches en cours
    // lorsque SIGUSR1 (et _seulement_ SIGUSR1) est reçu 
    int activeConnection = 0;
    //char* name;

    printf("| # | Actif (0/1) | Nom du fichier | PID | Status |\n");

    if (signo == SIGUSR1) {
        for (int i = 0; i < MAX_CONNEXIONS; i++) {
            struct msgReq req;
            char* fname;
            
            if (reqList[i].status >= 2) {
                char index[] = "index.txt";
                memcpy(&req, reqList[i].buf, sizeof(req));
                size_t allocsize = (req.type == REQ_LIST) ? sizeof(index) : req.sizePayload;
                fname = malloc(allocsize);
                // Le nombre de connexions actives
                activeConnection = 1;
            
                // Le nom des fichiers en cours de telechargement
                if (req.type == REQ_LIST) {
                    strncat(fname, index, sizeof(index)+1);

                } else if (req.type == REQ_READ) {
                    strncat(fname, reqList[i].buf + sizeof(req), req.sizePayload);
                }

            } else {
                activeConnection = 0;
                fname = malloc(1);
                *fname = '\0';
            }

            // L'identifiant des processus enfent s'occupant de chaque telechargement
            const char* status = statusDesc[reqList[i].status];

            // printing data
            printf("| %d | %d | %s | %d | %s |\n", i, activeConnection, fname, reqList[i].pid, status);
        }
    }
}

int main(int argc, char* argv[]){
    // Chemin du socket UNIX
    // Linux ne supporte pas un chemin de plus de 108 octets (voir man 7 unix)
    char path[108] = "/tmp/unixsocket";
    if(argc > 1)        // On peut également le passer en paramètre
        strncpy(path, argv[1], sizeof(path));
    unlink(path);       // Au cas ou le fichier liant le socket UNIX existerait deja

    // On initialise la liste des requêtes
    memset(&reqList, 0, sizeof(reqList));

    // Implémentez ici le code permettant d'attacher la fonction "gereSignal" au signal SIGUSR1
    if (signal(SIGUSR1, gereSignal) == SIG_ERR) {
        perror("Error catching SIGUSR1\n");
    }


    // Création et initialisation du socket (il y a 5 étapes)
    // 1) Créez une struct de type sockaddr_un et initialisez-la à 0.
    //      Puis, désignez le socket comme étant de type AF_UNIX
    //      Finalement, copiez le chemin vers le socket UNIX dans le bon attribut de la structure
    //      Voyez man unix(7) pour plus de détails sur cette structure

    //Creating socket, what should be its name ?
    struct sockaddr_un createdSocket;
    // Gotta initialize to 0
    memset(&createdSocket, 0, sizeof(createdSocket));
    // Assign AF_UNIX type
    createdSocket.sun_family = AF_UNIX;
    // Copying path to socket
    memcpy(&createdSocket.sun_path, path, sizeof(createdSocket.sun_path));


    // 2) Créez le socket en utilisant la fonction socket().
    //      Vérifiez si sa création a été effectuée avec succès, sinon quittez le processus en affichant l'erreur

    // Creating socket
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    // Checking if creation was successful
    if (sock < 0) {
        perror("Error creating socket");
        return 1;
    }

    // 3) Utilisez fcntl() pour mettre le socket en mode non-bloquant
    //      Vérifiez si l'opération a été effectuée avec succès, sinon quittez le processus en affichant l'erreur
    //      Voyez man fcntl pour plus de détails sur le champ à modifier

    // Using fcntl() to set socket as non blocking and checking for error
    if (fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
        perror("Error setting socket as non blocking");
        return 1;
    }

    // 4) Faites un bind sur le socket
    //      Vérifiez si l'opération a été effectuée avec succès, sinon quittez le processus en affichant l'erreur
    //      Voyez man bind(2) pour plus de détails sur cette opération

    // Biding socket and checking for error
    if (bind(sock, (struct sockaddr*)&createdSocket, sizeof(createdSocket)) == -1) {
        perror("Error binding socket");
        return 1;
    }

    // 5) Mettez le socket en mode écoute (listen), en acceptant un maximum de MAX_CONNEXIONS en attente
    //      Vérifiez si l'opération a été effectuée avec succès, sinon quittez le processus en affichant l'erreur
    //      Voyez man listen pour plus de détails sur cette opération

    // Setting socket in listen mode and checking for errors
    if (listen(sock, MAX_CONNEXIONS) == -1) {
        perror("Error listening to socket");
        return 1;
    }

    // Initialisation du socket UNIX terminée!

    // Boucle principale du programme
    int tacheRealisee;
    while(1){
        // On vérifie si de nouveaux clients attendent pour se connecter
        tacheRealisee = verifierNouvelleConnexion(reqList, MAX_CONNEXIONS, sock);

        // On teste si un client vient de nous envoyer une requête
        // Si oui, on la traite
        tacheRealisee += traiterConnexions(reqList, MAX_CONNEXIONS);

        // On teste si un de nos processus enfants a terminé son téléchargement
        // Dans ce cas, on traite le résultat
        tacheRealisee += traiterTelechargements(reqList, MAX_CONNEXIONS);

        // Si on a des données à envoyer au client, on le fait
        tacheRealisee += envoyerReponses(reqList, MAX_CONNEXIONS);

        // Si on n'a pas attendu dans un select ou effectué une tâche, on ajoute
        // un petit delai ici pour éviter d'utiliser 100% du CPU inutilement
        if(tacheRealisee == 0)
            usleep(SLEEP_TIME);
    }

    return 0;
}
