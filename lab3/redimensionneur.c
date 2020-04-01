// Gestion des ressources et permissions
#include <sys/resource.h>

// Nécessaire pour pouvoir utiliser sched_setattr et le mode DEADLINE
#include <getopt.h> 
#include <sched.h>
#include "schedsupp.h"

#include "allocateurMemoire.h"
#include "commMemoirePartagee.h"
#include "utils.h"

int main(int argc, char* argv[]){
    
    // Écrivez le code permettant de redimensionner une image (en utilisant les fonctions précodées
    // dans utils.c, celles commençant par "resize"). Votre code doit lire une image depuis une zone 
    // mémoire partagée et envoyer le résultat sur une autre zone mémoire partagée.
    // N'oubliez pas de respecter la syntaxe de la ligne de commande présentée dans l'énoncé.

   // 1. Analyser parametres de la ligne de commande

    int opt, schedType = 0;
    char deadlineOpts[32] = "";

    uint32_t heightOutput = 0;
    uint32_t widthOutput = 0;
    uint32_t resizeType = 1;

    while((opt = getopt(argc, argv, "s:d:w:h:m:")) != -1) {
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
            case 'w' :
                widthOutput = atoi(optarg);
                break;
            case 'h' :
                heightOutput = atoi(optarg);
                break;
            case 'm' :
                resizeType = atoi(optarg);
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
    memPartageHeader* writeHeader = (memPartageHeader*)malloc(sizeof(memPartageHeader));
    initMemoirePartageeLecteur(readSpace, &readZone);

    // 2.2 Recuperer les informations sur le flux d'entree

    uint32_t heightInput , widthInput, channel = 0;

    writeHeader->frameReader = 0;
    writeHeader->frameWriter = 0;
    writeHeader->largeur = widthOutput; 
    writeHeader->hauteur = heightOutput;
    writeHeader->fps = readZone.header->fps; 
    writeHeader->canaux = readZone.header->canaux;

    heightInput = readZone.header->hauteur;
    widthInput = readZone.header->largeur;
    channel = readZone.header->canaux;

    size_t sizeInput = channel * heightInput * widthInput;
    size_t sizeOutput = channel * heightOutput * widthOutput;

    size_t outP = sizeOutput + sizeof(struct memPartageHeader);

    // 3. Initializer la zone memoire de sortie (celle sur laquelle on ecrit les trames)
    // 3.1 Creer la zone memoire

    struct memPartage writeZone;

    // 3.2 Ecrire les informations sur le flux de sortie

    initMemoirePartageeEcrivain(writeSpace, &writeZone, outP, writeHeader);
    
    // 4. Initialiser l'allocateur memoire et fixer les zones alloues (mlock)
    
    prepareMemoire(sizeInput*2, sizeOutput*2);

    // 5. Ajuster les parametres de l'ordonnanceur

    if (schedType != 0) {
        setScheduling(schedType, deadlineOpts);
    }

    // 6. Boucle principale

    ResizeGrid grid;

    if(resizeType){
        grid = resizeBilinearInit(heightOutput, widthOutput, heightInput, widthInput);
    }else{
        grid = resizeNearestNeighborInit(heightOutput, widthOutput, heightInput, widthInput);
    }

    while(1){  
        if(resizeType){
            resizeBilinear(readZone.data,
                heightInput,
                widthInput,
                writeZone.data,
                heightOutput,
                widthOutput,
                grid,
                channel);
        }else{
            resizeNearestNeighbor(readZone.data,
                heightInput,
                widthInput,
                writeZone.data,
                heightOutput,
                widthOutput,
                grid,
                channel);
        }

        readZone.copieCompteur = readZone.header->frameWriter;
        readZone.header->frameReader++;
        pthread_mutex_unlock(&readZone.header->mutex);

        writeZone.copieCompteur = writeZone.header->frameReader;
        writeZone.header->frameWriter++;
        pthread_mutex_unlock(&writeZone.header->mutex);

        attenteLecteur(&readZone);
        pthread_mutex_lock(&readZone.header->mutex);
        attenteEcrivain(&writeZone);
        pthread_mutex_lock(&writeZone.header->mutex);
    }
    return 0;
}