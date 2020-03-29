// Gestion des ressources et permissions
#include <sys/resource.h>

// Nécessaire pour pouvoir utiliser sched_setattr et le mode DEADLINE
#include <sched.h>
#include "schedsupp.h"

#include "allocateurMemoire.h"
#include "commMemoirePartagee.h"
#include "utils.h"

#include "jpgd.h"

// Définition de diverses structures pouvant vous être utiles pour la lecture d'un fichier ULV
#define HEADER_SIZE 4
const char header[] = "SETR";

struct videoInfos{
        uint32_t largeur;
        uint32_t hauteur;
        uint32_t canaux;
        uint32_t fps;
};

/******************************************************************************
* FORMAT DU FICHIER VIDEO
* Offset     Taille     Type      Description
* 0          4          char      Header (toujours "SETR" en ASCII)
* 4          4          uint32    Largeur des images du vidéo
* 8          4          uint32    Hauteur des images du vidéo
* 12         4          uint32    Nombre de canaux dans les images
* 16         4          uint32    Nombre d'images par seconde (FPS)
* 20         4          uint32    Taille (en octets) de la première image -> N
* 24         N          char      Contenu de la première image (row-first)
* 24+N       4          uint32    Taille (en octets) de la seconde image -> N2
* 24+N+4     N2         char      Contenu de la seconde image
* 24+N+N2    4          uint32    Taille (en octets) de la troisième image -> N2
* ...                             Toutes les images composant la vidéo, à la suite
*            4          uint32    0 (indique la fin du fichier)
******************************************************************************/


int main(int argc, char* argv[]){
    
    // Écrivez le code de décodage et d'envoi sur la zone mémoire partagée ici!
    // N'oubliez pas que vous pouvez utiliser jpgd::decompress_jpeg_image_from_memory()
    // pour décoder une image JPEG contenue dans un buffer!
    // N'oubliez pas également que ce décodeur doit lire les fichiers ULV EN BOUCLE

	char *fichier_entree, *flux_sortie;

    int opt; 
    int schedType = 0; 
    char deadlineOpts[32];

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
                //printf("%s\n", schedType);
    			break;
    		case 'd':
                strcpy(deadlineOpts, optarg);
    			break;
    		default:
                return -1;
    			break;
    	}
    }
    
    fichier_entree = argv[optind++];
    flux_sortie = argv[optind++];

    // 2. Ouvrir et faire un mmap sur le fichier ULV d'entree

    int videoInput = open(fichier_entree, O_RDONLY);
    if (videoInput < 0) {
    	printf("Could not open input file.\n");
    	return videoInput;
    }

    // get file stats to get size
    struct stat videoStats;
    fstat(videoInput, &videoStats);

    char *videoMmap = (char *)mmap(NULL, videoStats.st_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, videoInput, 0);

    // 3. Intialiser la zone memoire de sortie (celle sur laquelle on ecrit les trames)
    // 3.1 Creer la zone memoire

    // Let's validate the header now
    if (strncmp(header, videoMmap, 4) != 0) {
    	printf("Not a valid file.\n");
    	return -1;
    }
    // Assuming the ulv is valid, let's get the rest of the header info. 
    videoInfos videoInfo;
    memcpy(&videoInfo.largeur, videoMmap + 4, 4);
    memcpy(&videoInfo.hauteur, videoMmap + 8, 4);
    memcpy(&videoInfo.canaux, videoMmap + 12, 4);
    memcpy(&videoInfo.fps, videoMmap + 16, 4);

    // Get size of a single frame, used for initMemoirePartageeEcrivain
    uint32_t frameSize = videoInfo.largeur * videoInfo.hauteur * videoInfo.canaux;

    memPartage* memStruct = (memPartage*)malloc(sizeof(memPartage));
    memPartageHeader* memStructHeader = (memPartageHeader*)malloc(sizeof(memPartageHeader));

    // Would do this inside init function, but it would add useless lines because of the structure, so doing here
    memStructHeader->frameWriter = 0;
    memStructHeader->frameReader = 0;
    memStructHeader->largeur = videoInfo.largeur;
    memStructHeader->hauteur = videoInfo.hauteur;
    memStructHeader->canaux = videoInfo.canaux;
    memStructHeader->fps = videoInfo.fps;

    if (initMemoirePartageeEcrivain(flux_sortie, memStruct, frameSize, memStructHeader) < 0) {
    	printf("Error while initializating /dev/shm/mem.\n");
    	return -1;
    }

    // 3.2 Ecrire les informations sur le flux de sortie
    // Already done in initMemoirePartageeEcrivain ?


    // 4. Initialiser l'allocateur memoire et fixer les zones alloues (mlock)

    if (prepareMemoire(0, frameSize) < 0) {
    	printf("Error while preparing memory.\n");
    	return -1;
    }

    // 5. Ajuster les parametres de l'ordonnanceur
    

    // 6. Boucle principale de traitement

    // start at 20 because this is where the first image data is. 
    int index = 20; 

    // Putting this outside loop to "save" time
    int width, height, channels = 0; 
    uint32_t currentFrame; 

    while (1) {
    	// Get frame size
    	memcpy(&currentFrame, videoMmap + index, 4);
        index += 4;

        //Checking if we're done. Must loop back if so.
        if (currentFrame == 0x00) {
        	// We're done, must loop back. Ugly code, but easy code. 
        	memcpy(&currentFrame, videoMmap + 20, 4);
        	index = 24; 
        }

        // TODO: Check difference between actual_comps and real_comps ?
    	unsigned char *frame = jpgd::decompress_jpeg_image_from_memory((const unsigned char*)(videoMmap+index),
    													               currentFrame,
    													               &width,
    													               &height,
    													               &channels,
    													               memStruct->header->canaux);

        // 6.3 Ecrire l'image traitee sur la zone memoire de sortie (avec synchronisation)

    	// Test to see if it works
        //printf("Current frame: height is %d, width is %d, fps is %d\n", height, width, fps);
    	/*enregistreImage(frame,
    					height,
    					width,
    					fps,
    					"image.ppm");*/
        //break;

        // Do we need the mutex before ?
        pthread_mutex_lock(&memStruct->header->mutex);

        memStruct->header->frameWriter++;

        memcpy(memStruct->data, frame, frameSize);
        index += currentFrame;

        tempsreel_free(frame);

        memStruct->copieCompteur = memStruct->header->frameReader;

        //Now that we're done, free the mutex. 
        pthread_mutex_unlock(&memStruct->header->mutex);
        // Is there a better alternative ? 
        attenteEcrivain(memStruct);
    }
    return 0;
}