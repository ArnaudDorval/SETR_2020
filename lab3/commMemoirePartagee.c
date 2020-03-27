#include "commMemoirePartagee.h"

int initMemoirePartageeLecteur(const char* identifiant,
                                struct memPartage *zone) {
	
	zone->fd = shm_open(identifiant, O_RDWR, 777);

	if (zone->fd < 0) {
		// TODO: Error while opening memory file
		return zone->fd;
	}

	//ftruncate(zone->fd, taille);

	// TODO
	return 0;
}

int initMemoirePartageeEcrivain(const char* identifiant,
                                struct memPartage *zone,
                                size_t taille,
                                struct memPartageHeader* headerInfos) {

	zone->fd = shm_open(identifiant, O_RDWR | O_CREAT, 777);

	if (zone->fd < 0) {
		printf("Could not shm_open the file\n");
		return zone->fd;
	}

	if (ftruncate(zone->fd, taille) < 0) {
		printf("Error using ftruncate.\n");
		return -1;
	}

	zone->data = (unsigned char*)mmap(NULL, taille, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, zone->fd, 0);
	
	if (zone->data == MAP_FAILED) {
		printf("Error creating mmap.\n");
		return -1;
	}

	zone->header = headerInfos;
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
	while(memStruct->copieCompteur == memStructHeader->frameReader);
	return 1; //pthread_mutex_trylock(&zone->header->mutex);
}