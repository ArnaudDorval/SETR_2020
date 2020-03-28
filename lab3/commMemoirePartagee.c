#include "commMemoirePartagee.h"

int initMemoirePartageeLecteur(const char* identifiant,
                                struct memPartage *zone) {
	
	zone->fd = shm_open(identifiant, O_RDWR, 777);

	if (zone->fd < 0) {
		// TODO: Error while opening memory file
		return zone->fd;
	}

	struct stat fdStat;
	fstat(zone->fd, &fdStat);

	void* mmapData = mmap(NULL, fdStat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, zone->fd, 0);
	if (mmapData == MAP_FAILED) {
		printf("Error creating mmap.\n");
		return -1;
	}

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
		printf("Error using ftruncate.\n");
		return -1;
	}

	void* mmapData = mmap(NULL, memorySpace, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, zone->fd, 0);
	if (mmapData == MAP_FAILED) {
		printf("Error creating mmap.\n");
		return -1;
	}

	memPartageHeader* mmapHeader = (memPartageHeader*)mmapData;
	mmapHeader = headerInfos;
	
	zone->data = ((unsigned char*)mmapData)+sizeof(memPartageHeader);
	zone->header = mmapHeader;
	zone->tailleDonnees = taille;
	zone->copieCompteur = 0;

	pthread_mutex_init(&zone->header->mutex, NULL);
	//pthread_mutexattr_setpshared(&zone->header->mutex, PTHREAD_PROCESS_SHARED);

	return 0;
}

int attenteLecteur(struct memPartage *zone) {
	while (zone->header->frameReader == zone->header->frameWriter);
	return 1; //pthread_mutex_lock(&zone->header->mutex);
}

int attenteLecteurAsync(struct memPartage *zone) {
	if (zone->header->frameReader == zone->header->frameWriter) {
		return -1;
	}
	return 1; //pthread_mutex_trylock(&zone->header->mutex);
}

int attenteEcrivain(struct memPartage *zone) {
	while(zone->copieCompteur == zone->header->frameReader);
	return 1; //pthread_mutex_trylock(&zone->header->mutex);
}