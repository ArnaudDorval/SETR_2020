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
    
    // Écrivez le code permettant de convertir une image en niveaux de gris, en utilisant la
    // fonction convertToGray de utils.c. Votre code doit lire une image depuis une zone mémoire 
    // partagée et envoyer le résultat sur une autre zone mémoire partagée.
    // N'oubliez pas de respecter la syntaxe de la ligne de commande présentée dans l'énoncé.
    int opt;

    uint32_t height;
    uint32_t width;
    uint32_t channel = 1;

    size_t sizeInput;
    size_t sizeOutput;

    char readSpace[100] = "";
    char writeSpace[100] = "";

    struct memPartage readZone ;
    struct memPartage writeZone;
    struct memPartageHeader writeHeader;

    int sched_policy = 0;
    struct sched_attr attr ; 

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

		opt = getopt_long(argc, argv, "s:d:", long_options, &option_index);
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
            default:
                printf("convertisseur bad request");
                break;

		}
	}
    if(debug_flag){
        strcpy(readSpace, "/mem1") ;
        strcpy(writeSpace, "/mem2");
    }
    else{
        strcpy(readSpace, argv[argc-2]);
        strcpy(writeSpace, argv[argc-1]) ;
    }


    initMemoirePartageeLecteur(readSpace, &readZone);

    writeHeader.frameReader = 0 ;
    writeHeader.frameWriter = 0 ;
    writeHeader.largeur = readZone.header->largeur ; 
    writeHeader.hauteur = readZone.header->hauteur;
    writeHeader.fps = readZone.header->fps ; 
    writeHeader.canaux = channel;

    height = readZone.header->hauteur;
    width = readZone.header->largeur;
    
    sizeInput = readZone.header->canaux * height * width;
    sizeOutput = height * width;
    initMemoirePartageeEcrivain(writeSpace, &writeZone, sizeOutput, &writeHeader);
    prepareMemoire(sizeInput, 0);

    
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
    
    while(1){

        convertToGray(readZone.data, 
                height, 
                width,
                3,
                writeZone.data);



        readZone.header->frameReader++;
        pthread_mutex_unlock(&(readZone.header->mutex)) ;
        attenteLecteur(&readZone) ;
        
        writeZone.header->frameWriter++;
        pthread_mutex_unlock(&(writeZone.header->mutex)) ;
        attenteEcrivain(&writeZone) ;

    }

    return 0;
}
