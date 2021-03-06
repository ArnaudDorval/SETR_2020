#include "commMemoirePartagee.h"

#pragma GCC push_options
#pragma GCC optimize("O0")

int initMemoirePartageeLecteur(const char* identifiant,
                                struct memPartage *zone) {
	
	// 2.1 Ouvrir la zone memoire correspondante et attendre qu'elle soit prete. 

	zone->fd = shm_open(identifiant, O_RDWR, 777);
	if (zone->fd < 0) {
		// TODO: Error while opening memory file
		return zone->fd;
	}

	struct stat fdStat;
	
	do {
		fstat(zone->fd, &fdStat);
	} while(fdStat.st_size == 0);

	void* mmapData = mmap(NULL, fdStat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, zone->fd, 0);
	if (mmapData == MAP_FAILED) {
		printf("Error creating mmap.\n");
		return -1;
	}

	// 2.2 Recuperer les informations sur le flux d'entree.

	memPartageHeader* mmapHeader = (memPartageHeader*) mmapData;

	zone->header = mmapHeader;
	zone->tailleDonnees = fdStat.st_size - sizeof(memPartageHeader);
	zone->data = ((unsigned char*)mmapData) + sizeof(memPartageHeader);
	zone->copieCompteur = 0;

	return 0;
}

int initMemoirePartageeEcrivain(const char* identifiant,
                                struct memPartage *zone,
                                size_t taille,
                                struct memPartageHeader* headerInfos) {

	size_t memorySpace = sizeof(memPartageHeader) + taille;

	zone->fd = shm_open(identifiant, O_RDWR | O_CREAT, 777);

	if (zone->fd < 0) {
		printf("Could not shm_open the file\n");
		return zone->fd;
	}

	if (ftruncate(zone->fd, memorySpace) < 0) {
		printf("Error using ftruncate with identifiant: %s\n", identifiant);
		return -1;
	}

	void* mmapData = mmap(NULL, memorySpace, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, zone->fd, 0);
	if (mmapData == MAP_FAILED) {
		printf("Error creating mmap.\n");
		return -1;
	}

	memPartageHeader* mmapHeader = (memPartageHeader*)mmapData;
	mmapHeader->mutex = headerInfos->mutex;
	mmapHeader->frameWriter = headerInfos->frameWriter;
	mmapHeader->frameReader = headerInfos->frameReader;
	mmapHeader->largeur = headerInfos->largeur;
	mmapHeader->hauteur = headerInfos->hauteur;
	mmapHeader->canaux = headerInfos->canaux;
	mmapHeader->fps = headerInfos->fps;
	
	zone->data = ((unsigned char*)mmapData)+sizeof(memPartageHeader);
	zone->header = mmapHeader;
	zone->tailleDonnees = taille;
	zone->copieCompteur = 0;

	pthread_mutexattr_t attr; 
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	
	pthread_mutex_init(&mmapHeader->mutex, &attr);

	return 0;
}

int attenteLecteur(struct memPartage *zone) {
	while (zone->header->frameReader == zone->header->frameWriter) {
        usleep(3000);
    }
	return 1; //pthread_mutex_lock(&zone->header->mutex);
}

int attenteLecteurAsync(struct memPartage *zone) {
	//printf("frameRead is %d and frameWriter is %d\n", zone->header->frameReader, zone->header->frameWriter);
	if (zone->header->frameReader == zone->header->frameWriter) {
		return -1;
	}
	return pthread_mutex_trylock(&zone->header->mutex);
}

int attenteEcrivain(struct memPartage *zone) {
	while(zone->copieCompteur == zone->header->frameReader) {
        usleep(3000);
    }
	return 1; //pthread_mutex_lock(&zone->header->mutex);
}

#pragma GCC pop_options