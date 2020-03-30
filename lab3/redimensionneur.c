// Gestion des ressources et permissions
#include <sys/resource.h>

// Nécessaire pour pouvoir utiliser sched_setattr et le mode DEADLINE
#include <sched.h>
#include "schedsupp.h"
#include <getopt.h> 

#include "allocateurMemoire.h"
#include "commMemoirePartagee.h"
#include "utils.h"

static int debug_flag;


int main(int argc, char* argv[]){
    
    // Écrivez le code permettant de redimensionner une image (en utilisant les fonctions précodées
    // dans utils.c, celles commençant par "resize"). Votre code doit lire une image depuis une zone 
    // mémoire partagée et envoyer le résultat sur une autre zone mémoire partagée.
    // N'oubliez pas de respecter la syntaxe de la ligne de commande présentée dans l'énoncé.
    int opt;
    
    char readSpace[100] = "";
    char writeSpace[100] = "";
    uint32_t channel = 0;
    uint32_t heightInput = 0;
    uint32_t widthInput = 0;
    uint32_t heightOutput = 0;
    uint32_t widthOutput = 0;
    uint32_t resizeType = 1;

    struct memPartage readZone ;
    struct memPartage writeZone;
    struct memPartageHeader writeHeader;

    struct sched_attr attr; 
    int sched_policy = 0;

        /**
     * Section pour parse la commande 
     **/
    while(1){
		static struct option long_options[] =
		{
			{"debug", no_argument, &debug_flag, 1},
			{0, 0, 0, 0}
		};

		int option_index;

		opt = getopt_long(argc, argv, "s:d:w:h:m:", long_options, &option_index);

		if(opt == -1) {
				break;
		}
		switch(opt){
            case 's':
                printf("option s '%s' \n", optarg);
                break;
            case 'd':
                printf("option d '%s' \n", optarg);
                break;
            case 'w' :
                printf("option w '%s' \n", optarg);
                widthOutput = atoi(optarg);
                break;
            case 'h' :
                printf("option h '%s' \n", optarg);
                heightOutput = atoi(optarg);
                break;
            case 'm' :
                printf("option m '%s' \n", optarg);
                resizeType = atoi(optarg);
                break;
            default:
                printf("redimensionneur bad request");
                break;
            
		}
	}

    if(debug_flag){
        strcpy(readSpace, "/mem1");
        strcpy(writeSpace, "/mem2");
        widthOutput = 400;
        heightOutput = 250;
        resizeType = 1;
    }
    else{
        strcpy(readSpace, argv[argc-2]);
        strcpy(writeSpace, argv[argc-1]);
    }


    initMemoirePartageeLecteur(readSpace, &readZone);

    writeHeader.frameReader = 0;
    writeHeader.frameWriter = 0;
    writeHeader.largeur = widthOutput; 
    writeHeader.hauteur = heightOutput;
    writeHeader.fps = readZone.header->fps; 
    writeHeader.canaux = readZone.header->canaux;

    heightInput = readZone.header->hauteur;
    widthInput = readZone.header->largeur;
    channel = readZone.header->canaux;

    size_t sizeInput;
    size_t sizeOutput;

    sizeInput = channel * heightInput * widthInput;
    sizeOutput = channel * heightOutput * widthOutput;

    size_t outP = sizeOutput + sizeof(struct memPartageHeader);

    initMemoirePartageeEcrivain(writeSpace, &writeZone, outP, &writeHeader);
    prepareMemoire(sizeInput, sizeOutput);

    attr.size = sizeof(attr);
    attr.sched_flags = 0;
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

    pthread_mutex_lock(&(writeZone.header->mutex));
    ResizeGrid grid;

    if(resizeType){
        grid = resizeNearestNeighborInit(heightOutput, widthOutput, heightInput, widthInput);
    }else{
        grid = resizeBilinearInit(heightOutput, widthOutput, heightInput, widthInput);
    }

    while(1){
        if(resizeType){
            resizeNearestNeighbor(readZone.data,
                heightInput,
                widthInput,
                writeZone.data,
                heightOutput,
                widthOutput,
                grid,
                channel);
        }else{
            resizeBilinear(readZone.data,
                heightInput,
                widthInput,
                writeZone.data,
                heightOutput,
                widthOutput,
                grid,
                channel);
        }
        readZone.header->frameReader++;
        pthread_mutex_unlock(&(readZone.header->mutex)) ;
        attenteLecteur(&readZone) ;

        writeZone.header->frameWriter++;
        pthread_mutex_unlock(&(writeZone.header->mutex)) ;
        attenteEcrivain(&writeZone) ;
    }
    return 0;
}