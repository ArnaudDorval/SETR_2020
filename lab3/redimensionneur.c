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
    
    // Écrivez le code permettant de redimensionner une image (en utilisant les fonctions précodées
    // dans utils.c, celles commençant par "resize"). Votre code doit lire une image depuis une zone 
    // mémoire partagée et envoyer le résultat sur une autre zone mémoire partagée.
    // N'oubliez pas de respecter la syntaxe de la ligne de commande présentée dans l'énoncé.
    int filterType = 0;
    int opt;
    
    char readSpace[30] = "";
    char writeSpace[30] = "";
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
    int sched_policy;

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
    writeHeader.largeur = readZone.header->largeur; 
    writeHeader.hauteur = readZone.header->hauteur;
    writeHeader.fps = readZone.header->fps; 
    writeHeader.canaux = readZone.header->canaux;

    heightInput = readZone.header->hauteur;
    widthInput = readZone.header->largeur;
    channel = readZone.header->canaux;

    size_t sizeInput;
    size_t sizeOutput;
    size_t sizeScale;

    sizeInput = channel * heightInput * widthInput;
    sizeOutput = channel * heightOutput * widthOutput;

    if(sizeInput > sizeOutput){
        sizeScale = sizeInput;
    }else{
        sizeScale = sizeOutput;
    }

    initMemoirePartageeEcrivain(writeSpace, &writeZone, outputSize, &writeHeader);
    prepareMemoire(sizeScale, 0);

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