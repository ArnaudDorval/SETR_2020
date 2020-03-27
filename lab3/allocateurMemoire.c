#include "allocateurMemoire.h"

// Entre 5 et 15, atelier 7 p.6
#define N_ALLOC 5

static void* data[N_ALLOC];
static int  dataTable[N_ALLOC];
size_t allocSize;

int prepareMemoire(size_t tailleImageEntree, size_t tailleImageSortie) {
	allocSize = (tailleImageEntree >= tailleImageSortie) ? tailleImageEntree : tailleImageSortie;
	
	for (int i=0; i<N_ALLOC; i++) {
		data[i] = malloc(allocSize);
		mlock(data[i], allocSize);
		dataTable[i] = 1; // 1 meaning free, 0 meaning being used
	}

	return 0;
}

void* tempsreel_malloc(size_t taille) {
	if (taille > allocSize) {
		printf("Error: space required was too big.\n");
		return NULL;
	}

	for (int i=0; i<N_ALLOC; i++) {
		if (dataTable[i] == 1) {
			dataTable[i] = 0;
			return data[i];
		}
	}
	printf("No free space !\n");
	return NULL; 
}

void tempsreel_free(void* ptr) {
	for (int i=0; i<N_ALLOC; i++){
		if (ptr == data[i]) {
			dataTable[i] = 1;
			return;
		}
	}
	printf("Cannot find the memory pointer to free.\n");
	return;
}