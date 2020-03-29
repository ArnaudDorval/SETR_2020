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
    
    // Écrivez le code permettant de filtrer une image (en utilisant les fonctions précodées
    // dans utils.c). Votre code doit lire une image depuis une zone mémoire partagée et
    // envoyer le résultat sur une autre zone mémoire partagée.
    // N'oubliez pas de respecter la syntaxe de la ligne de commande présentée dans l'énoncé.

    int filtreType = 0, opt, sched_policy;
    char espaceLecture[30] = "";
    char espaceEcriture[30] = "";
    uint32_t canaux = 0 ;
    uint32_t hauteurEntree = 0 ;
    uint32_t largeurEntree = 0 ;

    while(1){
		static struct option long_options[] =
		{
			{"debug", no_argument, &debug_flag, 1},
			{0, 0, 0, 0}
		};
		int option_index;

		opt = getopt_long(argc, argv, "t:", long_options, &option_index);
		if(opt == -1) {
				break;
		}
		switch(opt){
            case 't' :
                filtreType = atoi(optarg);
		}
	}
    if(debug_flag){
        strcpy(espaceLecture, "/mem1") ;
        strcpy(espaceEcriture, "/mem2");
        filtreType = 0 ; // filtre passe-bas ? 
    }
    else{
        strcpy(espaceLecture, argv[argc-2]);
        strcpy(espaceEcriture, argv[argc-1]) ;
    }

  struct memPartage zone_lecteur ;
  initMemoirePartageeLecteur(espaceLecture, &zone_lecteur);
  struct memPartage zone_ecrivain;
  struct memPartageHeader zone_header_ecrivain;
  zone_header_ecrivain.frameReader = 0 ;
  zone_header_ecrivain.frameWriter = 0 ;
  zone_header_ecrivain.largeur = zone_lecteur.header->largeur ; 
  zone_header_ecrivain.hauteur = zone_lecteur.header->hauteur ;
  zone_header_ecrivain.fps = zone_lecteur.header->fps ; 
  zone_header_ecrivain.canaux = zone_lecteur.header->canaux;
  hauteurEntree = zone_lecteur.header->hauteur;
  largeurEntree = zone_lecteur.header->largeur;
  canaux = zone_lecteur.header->canaux ;
  size_t tailleImageSortie;

  tailleImageSortie = canaux * hauteurEntree * largeurEntree ;

  initMemoirePartageeEcrivain(espaceEcriture, &zone_ecrivain, tailleImageSortie, &zone_header_ecrivain);
  prepareMemoire(tailleImageSortie, 0);

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

  while(1){
        if(filtreType){
            highpassFilter(zone_lecteur.header->hauteur,
            zone_lecteur.header->largeur, 
            zone_lecteur.data,
            zone_ecrivain.data,
            3,
            5,
            zone_lecteur.header->canaux) ;
        }
        else{
            lowpassFilter(zone_lecteur.header->hauteur,
            zone_lecteur.header->largeur, 
            zone_lecteur.data,
            zone_ecrivain.data,
            3,
            5,
            zone_lecteur.header->canaux) ;
        }
        zone_lecteur.header->frameReader++;
        pthread_mutex_unlock(&(zone_lecteur.header->mutex)) ;
        attenteLecteur(&zone_lecteur) ;

        zone_ecrivain.header->frameWriter++;
        pthread_mutex_unlock(&(zone_ecrivain.header->mutex)) ;
        attenteEcrivain(&zone_ecrivain) ;

    }
    return 0;
}
