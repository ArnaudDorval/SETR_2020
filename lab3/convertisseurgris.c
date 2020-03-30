// Gestion des ressources et permissions
#include <sys/resource.h>

// Nécessaire pour pouvoir utiliser sched_setattr et le mode DEADLINE
#include <sched.h>
#include "schedsupp.h"
#include <getopt.h> 

#include "allocateurMemoire.h"
#include "commMemoirePartagee.h"
#include "utils.h"


int main(int argc, char* argv[]){
    
    // Écrivez le code permettant de convertir une image en niveaux de gris, en utilisant la
    // fonction convertToGray de utils.c. Votre code doit lire une image depuis une zone mémoire 
    // partagée et envoyer le résultat sur une autre zone mémoire partagée.
    // N'oubliez pas de respecter la syntaxe de la ligne de commande présentée dans l'énoncé.

    int opt, schedType = 0; 
    char deadlineOpts[32] = "";

    // 1. Analyser parametres de la ligne de commande

    while((opt = getopt(argc, argv, "s:d:")) != -1) {
    	switch (opt) {
    		case 's':
                if (strcmp(optarg, "RR") == 0) {
                    schedType = SCHED_RR;
                }
                else if (strcmp(optarg, "FIFO") == 0) {
                    schedType = SCHED_FIFO;
                }
                else if (strcmp(optarg, "DEADLINE") == 0) {
                    schedType = SCHED_DEADLINE;
                }
    			break;
    		case 'd':
                strcpy(deadlineOpts, optarg);
    			break;
    		default:
                return -1;
    			break;
    	}
    }

    char readSpace[8] = "";
    char writeSpace[8] = "";

    strcpy(readSpace, argv[optind++]);
    strcpy(writeSpace, argv[optind++]);

    // 2. Initialiser la zone memoire d'entree (celle sur laquelle on doit lire les trames)
    // 2.1 Ouvrir la zone memoire d'entree et attendre qu'elle soit prete

    struct memPartage readZone ;
    struct memPartageHeader writeHeader;
    initMemoirePartageeLecteur(readSpace, &readZone);

    // 2.2 Recuperer les informations sur le flux d'entree

    writeHeader.frameReader = 0 ;
    writeHeader.frameWriter = 0 ;
    writeHeader.largeur = readZone.header->largeur ; 
    writeHeader.hauteur = readZone.header->hauteur;
    writeHeader.fps = readZone.header->fps ; 
    writeHeader.canaux = 1;

    uint32_t height = readZone.header->hauteur;
    uint32_t width = readZone.header->largeur;
    
    size_t sizeInput = readZone.header->canaux * height * width;
    size_t sizeOutput = 1 * height * width;

    // 3. Initializer la zone memoire de sortie (celle sur laquelle on ecrit les trames)
    // 3.1 Creer la zone memoire

    struct memPartage writeZone;

    // 3.2 Ecrire les informations sur le flux de sortie

    initMemoirePartageeEcrivain(writeSpace, &writeZone, sizeOutput, &writeHeader);
    
    // 4. Initialiser l'allocateur memoire et fixer les zones alloues (mlock)

    prepareMemoire(sizeInput, 0);

    // 5. Ajuster les parametres de l'ordonnanceur

    int sched_policy = 0;
    struct sched_attr attr ; 

    attr.size = sizeof(attr);
    attr.sched_flags = 0 ;
    attr.sched_policy = sched_policy;

    switch (sched_policy)
    {
        case SCHED_NORMAL:
        case SCHED_RR:
            attr.__sched_priority = 0 ;
            break ;
        case SCHED_FIFO:
            attr.__sched_priority = 0 ;
            break;
        case SCHED_DEADLINE : 
            attr.sched_runtime = 30000000;
            attr.sched_period = 100000000;
            attr.sched_deadline = attr.sched_period;
        default:
            break ;
    }

    // 6. Boucle principale de traitement
    
    while(1){
        pthread_mutex_lock(&writeZone.header->mutex);

        convertToGray(readZone.data, 
                height, 
                width,
                3,
                writeZone.data);



        readZone.header->frameReader++;
        pthread_mutex_unlock(&(readZone.header->mutex)) ;
        attenteLecteur(&readZone) ;
        
        writeZone.header->frameWriter++;
        writeZone.copieCompteur = writeZone.header->frameReader;
        pthread_mutex_unlock(&(writeZone.header->mutex)) ;
        attenteEcrivain(&writeZone) ;

    }

    return 0;
}
