/* TP2 2020 */
/*
 * Squelette d'implementation du système de fichier setrFS
 * Adapté de l'exemple "passthrough" de libfuse : https://github.com/libfuse/libfuse/blob/master/example/passthrough.c
 * Distribué sous licence GPL
 *
 *
*/

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif


#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

// Sockets UNIX
#include <sys/socket.h>
#include <sys/un.h>

// pthread pour les mutex
#include <pthread.h>

#include "communications.h"

#include "fstools.h"


const char unixSockPath[] = "/tmp/unixsocket";

// Cette fonction initialise le cache et l'insère dans le contexte de FUSE, qui sera
// accessible à toutes les autres fonctions.
// Elle est déjà implémentée pour vous, mais vous pouvez la modifier au besoin.
void* setrfs_init(struct fuse_conn_info *conn){
	struct cacheData cache;
	cache.rootDirIndex = NULL;
	cache.firstFile = NULL;
	pthread_mutex_init(&(cache.mutex), NULL);
	char *cachePtr = malloc(sizeof(cache));
	memcpy(cachePtr, &cache, sizeof(cache));
	return (void*)cachePtr;
}



//fonction retourne si fichier est un directory
int isFileDir(const char *path)
{
	if (strcmp(path, "/") == 0)
	{
		return 1;
	}else {
		return 0;
	}
}
// Cette fonction est appelée pour obtenir les attributs d'un fichier
// Voyez la page man stat(2) pour des détails sur la structure 'stat' que vous devez remplir
// Beaucoup de champs n'ont pas de sens dans le contexte du TP, par exemple le numéro d'inode ou le numéro du device :
// dans ces cas, initialisez ces valeurs à 0.
// Il est TRÈS important de remplir correctement les champs suivants :
// - st_mode : il contient les permissions du fichier ou dossier (accordez simplement toutes les permissions à tous),
//				mais aussi le type de fichier. C'est ici que vous devez indiquer à FUSE si le chemin qu'il vous donne
//				est un dossier ou un fichier.
// - st_size : de manière générale, vous pouvez renvoyer une valeur par défaut (par exemple 1) dans ce champ. Toutefois,
//				si le fichier est ouvert et en cours de lecture, vous _devez_ renvoyer la vraie taille du fichier, car
//				FUSE utilise cette information pour déterminer si la lecture est terminée, _peu importe_ ce que renvoie
//				votre fonction read() implémentée plus bas...
//				Utilisez le fichier mis en cache pour obtenir sa taille en octets dans ce dernier cas.
//
// Cette fonction montre également comment récupérer le _contexte_ du système de fichiers. Vous pouvez utiliser ces
// lignes dans d'autres fonctions.
static int setrfs_getattr(const char *path, struct stat *stbuf)
{
	// On récupère le contexte
	struct fuse_context *context = fuse_get_context();

	struct cacheData *cache = (struct cacheData*)context->private_data;

	//config a 0 des des param inutilisé
	stbuf->st_dev = 0;
	stbuf->st_ino = 0;
	stbuf->st_mode = 0;
	stbuf->st_nlink = 0;
	stbuf->st_rdev = 0;
	stbuf->st_blksize = 0;
	stbuf->st_blocks = 0;
	stbuf->st_atime = 0;
	stbuf->st_ctime = 0;

	// Si vous avez enregistré dans données dans setrfs_init, alors elles sont disponibles dans context->private_data
	// Ici, voici un exemple où nous les utilisons pour donner le bon propriétaire au fichier (l'utilisateur courant)
	stbuf->st_uid = context->uid;		// On indique l'utilisateur actuel comme proprietaire
	stbuf->st_gid = context->gid;		// Idem pour le groupe

	// TODO
	if(isFileDir(path)){
		//IFMT cest le mask du type de file, 0777 cest bit de permission
		stbuf->st_mode = (__S_IFDIR & __S_IFMT) | 0777;
	}else {

		stbuf->st_mode = (__S_IFREG & __S_IFMT) | 0777;
	}

	if(VERBOSE)
		printf("st_mode : %i", stbuf->st_mode);

	//check si fichier en cache
	char *baseDir = strrchr(path, '/');
	const char *filename = baseDir ? baseDir + 1 : path;

	pthread_mutex_lock(&(cache->mutex));
	struct cacheFichier *enterFileCache = trouverFichierEnCache(filename, cache);
	pthread_mutex_unlock(&(cache->mutex));

	if (enterFileCache == NULL){
		stbuf->st_size = 1;
	}else{

		if(VERBOSE){
			printf("fichier : %s taille : %i \n", filename, enterFileCache->len);
		}
		stbuf->st_size = enterFileCache->len;
	}

	return 0;
}


// Cette fonction est utilisée pour lister un dossier. Elle est déjà implémentée pour vous
static int setrfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	DIR *dp;
	struct dirent *de;
	printf("setrfs_readdir : %s\n", path);
	(void) offset;
	(void) fi;

	struct fuse_context *context = fuse_get_context();
	struct cacheData *cache = (struct cacheData*)context->private_data;
	if(cache->rootDirIndex == NULL){
		// Le listing du repertoire n'est pas en cache
		// On doit faire une requete
		// On ouvre un socket
		int sock = socket(AF_UNIX, SOCK_STREAM, 0);
	    if(sock == -1){
	        perror("Impossible d'initialiser le socket UNIX");
	        return -1;
	    }

	    // Ecriture des parametres du socket
	    struct sockaddr_un sockInfo;
	    memset(&sockInfo, 0, sizeof(sockInfo));
	    sockInfo.sun_family = AF_UNIX;
	    strncpy(sockInfo.sun_path, unixSockPath, sizeof(sockInfo.sun_path) - 1);

		// Connexion
	    if(connect(sock, (const struct sockaddr *) &sockInfo, sizeof(sockInfo)) < 0){
	        perror("Erreur connect");
	        exit(1);
	    }

		// Formatage et envoi de la requete
		//size_t len = strlen() + 1;		// +1 pour le caractere NULL de fin de chaine
	    struct msgReq req;
	    req.type = REQ_LIST;
	    req.sizePayload = 0;
		int octetsTraites = envoyerMessage(sock, &req, NULL);

		// On attend et on recoit le fichier demande
		struct msgRep rep;
		octetsTraites = read(sock, &rep, sizeof(rep));
		if(octetsTraites == -1){
			perror("Erreur en effectuant un read() sur un socket pret");
			exit(1);
		}
		if(VERBOSE)
			printf("Lecture de l'en-tete de la reponse sur le socket %i\n", sock);

		pthread_mutex_lock(&(cache->mutex));
		cache->rootDirIndex = malloc(rep.sizePayload + 1);
		cache->rootDirIndex[rep.sizePayload] = 0;		// On s'assure d'avoir le caractere nul a la fin de la chaine
		unsigned int totalRecu = 0;
		// Il se peut qu'on ait a faire plusieurs lectures si le fichier est gros
		while(totalRecu < rep.sizePayload){
			octetsTraites = read(sock, cache->rootDirIndex + totalRecu, rep.sizePayload - totalRecu);
			totalRecu += octetsTraites;
		}
		pthread_mutex_unlock(&(cache->mutex));
	}

	// On va utiliser strtok, qui modifie la string
	// On utilise donc une copie
	//pthread_mutex_lock(&(cache->mutex));
	char *indexStr = malloc(strlen(cache->rootDirIndex) + 1);
	strcpy(indexStr, cache->rootDirIndex);
	//pthread_mutex_lock(&(cache->mutex));

	// FUSE s'occupe deja des pseudo-fichiers "." et "..",
	// donc on se contente de lister le fichier d'index qu'on vient de recevoir

	char *nomFichier = strtok(indexStr, "\n");		// On assume des fins de lignes UNIX
	int countInode = 1;
	while(nomFichier != NULL){
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = 1; 			// countInode++;
		st.st_mode = (S_IFREG & S_IFMT) | 0777;	// Fichier regulier, permissions 777
		if(VERBOSE)
			printf("Insertion du fichier %s dans la liste du repertoire\n", nomFichier);
		if (filler(buf, nomFichier, &st, 0)){
			perror("Erreur lors de l'insertion du fichier dans la liste du repertoire!");
			break;
		}
		nomFichier = strtok(NULL, "\n");
	}
	free(nomFichier);

	return 0;
}

size_t queryServer(struct msgReq pReq, const char *pPath, struct cacheData *pCache, char **pDest){

		char *path = (char*)pPath;
		// On ouvre un socket
		int sock = socket(AF_UNIX, SOCK_STREAM, 0);
	    if(sock == -1){
	        perror("Impossible d'initialiser le socket UNIX");
	        return -1;
	    }

	    // Ecriture des parametres du socket
	    struct sockaddr_un sockInfo;
	    memset(&sockInfo, 0, sizeof(sockInfo));
	    sockInfo.sun_family = AF_UNIX;
	    strncpy(sockInfo.sun_path, unixSockPath, sizeof(sockInfo.sun_path) - 1);

		// Connexion
	    if(connect(sock, (const struct sockaddr *) &sockInfo, sizeof(sockInfo)) < 0){
	        perror("Erreur connect");
	        exit(1);
	    }

		int octetsTraites = envoyerMessage(sock, &pReq, path);

		// On attend et on recoit le fichier demande
		struct msgRep rep;
		octetsTraites = read(sock, &rep, sizeof(rep));
		if(octetsTraites == -1){
			perror("Erreur en effectuant un read() sur un socket pret");
			exit(1);
		}
		if(VERBOSE)
			printf("Lecture de l'en-tete de la reponse sur le socket %i\n", sock);

		pthread_mutex_lock(&(pCache->mutex));

		//(*pDest)[rep.sizePayload] = 0;		// On s'assure d'avoir le caractere nul a la fin de la chaine
		unsigned int totalRecu = 0;
		// Il se peut qu'on ait a faire plusieurs lectures si le fichier est gros
		while(totalRecu < rep.sizePayload){
			octetsTraites = read(sock, *pDest + totalRecu, rep.sizePayload - totalRecu);
			totalRecu += octetsTraites;
		}
		pthread_mutex_unlock(&(pCache->mutex));

		if(VERBOSE)
			printf("reponse size payload %i\n", rep.sizePayload);
		
		return rep.sizePayload;
}
// Cette fonction est appelée lorsqu'un processus ouvre un fichier. Dans ce cas-ci, vous devez :
// 1) si le fichier est déjà dans le cache, retourner avec succès en mettant à jour le file handle (champ fh)
//		dans la structure fuse_file_info
// 2) si le fichier n'est pas dans le cas, envoyer une requête au serveur pour le télécharger, puis l'insérer dans
//		le cache et effectuer l'étape 1).
// 3) il se peut que le fichier n'existe tout simplement pas. Dans ce cas, vous devez renvoyer le code d'erreur approprié.
//
// Voir man open(2) pour les détails concernant ce que cette fonction doit faire et comment renvoyer une erreur
// (par exemple dans le cas d'un fichier non existant).
//
// Notez en particulier que le file handle (aussi appelé file descriptor) doit satisfaire les conditions suivantes :
//	* Il doit être un entier non signé
//	* Il doit être _différent_ pour chaque fichier ouvert. Si un fichier est fermé (en utilisant close), alors il
//		est valide de réutiliser son file descriptor (mais vous n'y êtes pas obligés)
//	* Il ne doit pas valoir 0, 1 ou 2, qui sont déjà utilisés pour stdin, stdout et stderr.
// Petit truc : la signification exacte du file handle est laissée à la discrétion du système de fichier (vous).
// Vous pouvez donc le choisir de la manière qui vous arrange le plus, en autant que cela respecte les conditions
// énoncées plus haut. Rappelez-vous en particulier qu'un pointeur est unique...
static int setrfs_open(const char *path, struct fuse_file_info *fi)
{
		// TODO
	struct msgReq request;

	struct fuse_context *context = fuse_get_context();
	struct cacheData *cache = (struct cacheData*)context->private_data;

	char *baseDir = strrchr(path, '/');
	const char *filename = baseDir ? baseDir + 1 : path;

	if(VERBOSE)
		printf("file name to open %s", filename);

	pthread_mutex_lock(&(cache->mutex));
	struct cacheFichier *file= incrementeCompteurFichier(filename, cache, 1);
	pthread_mutex_unlock(&(cache->mutex));

	if(file != NULL){
		if(VERBOSE)
			printf("file id : %i \n", (uint32_t)file);
		fi->fh = (uint32_t)file;
		return 0;
	}

	if (cache->rootDirIndex == NULL){
		
		request.type = REQ_LIST;
		request.sizePayload = 0;

		queryServer(request, NULL, cache, &(cache->rootDirIndex));
		if(VERBOSE)
			printf("root index : %s \n", cache->rootDirIndex);
	}

	if(strstr(cache->rootDirIndex, filename) == NULL){
		perror("Fichier existe pas");
		return -1;
	}

	request.type = REQ_READ;
	request.sizePayload = strlen(filename) + 1;
	
	struct cacheFichier *recieved = malloc(sizeof(struct cacheFichier));
	memset(recieved, 0, sizeof(struct cacheFichier));

	size_t response = queryServer(request, filename, cache, &(recieved->data));

	recieved->nom = malloc(request.sizePayload);
	strcpy(recieved->nom, filename);
	recieved->len = response;
	recieved->offset = 0;
	recieved->countOpen += 1;

	pthread_mutex_lock(&(cache->mutex));
	insererFichier(recieved, cache);
	pthread_mutex_unlock(&(cache->mutex));

	fi->fh = (uint32_t)recieved;

	return 0;


}


// Cette fonction est appelée lorsqu'un processus lit un fichier après l'avoir ouvert.
// FUSE s'occupe déjà de vous redonner la structure fuse_file_info que vous devez avoir remplie dans setrfs_open()
// Les autres paramètres d'entrée sont la taille maximale à lire et le décalage (offset) du début de la lecture
// par rapport au début du fichier.
// Vous devez :
// 1) Vérifier que la taille de lecture demandée ne dépasse pas les limites du fichier, compte tenu du décalage
// 2) Si oui, réduire la taille pour lire le reste du fichier
// 3) Copier le même nombre d'octets depuis le cache vers le pointeur buf
// 4) Retourner le nombre d'octets copiés
//
// Voir man read(2) pour plus de détails sur cette fonction. En particulier, notez que cette fonction peut retourner
// _moins_ d'octets que ce que demandé, mais ne peut en aucun cas en retourner _plus_. Par ailleurs, comme indiqué
// dans la documentation, cette fonction doit avancer le pointeur de position dans le fichier. Vous pouvez utiliser
// le champ offset de la structure cacheFichier pour retenir cette position entre les différents appels à read().
//
// N'oubliez pas que vous recevez le file handle dans la structure fuse_file_info. Vous n'êtes pas forcés de l'utiliser,
// mais si vous y avez mis quelque chose d'utile, il est facile de le récupérer!
static int setrfs_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
		// TODO
		//on peut pogner le file id pour get le fichier
		uint32_t handler = fi->fh;
		struct cacheFichier *file = (struct cacheFichier *)handler;

		if (VERBOSE)
			printf("file handler : %i file len : %i offset: %i", handler, file->len, offset);


		if (offset > file->len){
			return 0;
		}

		if ((size + offset) > file->len)
			size = (file->len) - offset;
			
		//check addresse de début de lecture et met le nouveau offset dans le fichier apres on peut copier le nb d'octet vers buf
		char *beginAddr = (file->data) + offset;
		file->offset = size + offset - 1;

		if (VERBOSE)
			printf("file offset: %i size : %i beginAddr : %i", file->offset, size, beginAddr);

		memcpy(buf,beginAddr, size);

		return (int)size;

}


// Cette fonction est appelée lorsqu'un processus ferme un fichier (close).
// Vous n'avez rien de particulier à produire comme résultat, mais vous devez vous assurer de libérer toute la mémoire
// utilisée pour stocker ce fichier (pensez au buffer contenant son cache, son nom, etc.)
static int setrfs_release(const char *path, struct fuse_file_info *fi)
{
		// TODO
		struct fuse_context *context = fuse_get_context();
		struct cacheData *cache = (struct cacheData*) context->private_data;
		uint32_t handler = fi->fh;
		struct cacheFichier *file = (struct cacheFichier *)handler;
		
		if (VERBOSE){
			printf("close file : %s openCount : %i", file->nom, file->countOpen);
		}
		if(file->countOpen > 0){
			pthread_mutex_lock(&(cache->mutex));
			file->countOpen = file->countOpen - 1;
			pthread_mutex_unlock(&(cache->mutex));
		}
		
		if(file->countOpen == 0){
			pthread_mutex_lock(&(cache->mutex));
			retireFichier(file, cache);
			pthread_mutex_unlock(&(cache->mutex));
		}
}


///////////////////////////////////////
// Plus rien à faire à partir d'ici! //
// Les fonctions restantes n'ont pas //
// à être implémentées pour que le   //
// système de fichiers fonctionne.   //
///////////////////////////////////////

static int setrfs_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	int fd;
	int res;
	printf("#### NOT IMPLEMENTED! ####");
	printf("setrfs_write\n");

	return 0;
}


static int setrfs_access(const char *path, int mask)
{
	int res;
	printf("#### NOT IMPLEMENTED! ####");
	printf("setrfs_access for path %s\n", path);
	return 0;
}

static int setrfs_readlink(const char *path, char *buf, size_t size)
{
	int res;
	printf("#### NOT IMPLEMENTED! ####");
	printf("setrfs_readlink\n");
	return 0;
}

static int setrfs_statfs(const char *path, struct statvfs *stbuf)
{
	int res;
	printf("#### NOT IMPLEMENTED! ####");
	printf("setrfs_statfs\n");

	return 0;
}


static int setrfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;
	printf("#### NOT IMPLEMENTED! ####");
	printf("setrfs_mknod\n");

	return 0;
}

static int setrfs_mkdir(const char *path, mode_t mode)
{
	int res;
	printf("#### NOT IMPLEMENTED! ####");
	printf("setrfs_mkdir\n");


	return 0;
}

static int setrfs_unlink(const char *path)
{
	int res;
	printf("#### NOT IMPLEMENTED! ####");
	printf("setrfs_unlink\n");

	return 0;
}

static int setrfs_rmdir(const char *path)
{
	int res;
	printf("#### NOT IMPLEMENTED! ####");
	printf("setrfs_rmdir\n");


	return 0;
}

static int setrfs_symlink(const char *from, const char *to)
{
	int res;
	printf("#### NOT IMPLEMENTED! ####");
	printf("setrfs_symlink\n");

	return 0;
}

static int setrfs_rename(const char *from, const char *to)
{
	int res;
	printf("#### NOT IMPLEMENTED! ####");
	printf("setrfs_rename\n");

	return 0;
}

static int setrfs_link(const char *from, const char *to)
{
	int res;
	printf("#### NOT IMPLEMENTED! ####");
	printf("setrfs_link\n");

	return 0;
}

static int setrfs_chmod(const char *path, mode_t mode)
{
	int res;
	printf("#### NOT IMPLEMENTED! ####");
	printf("setrfs_chmod\n");

	return 0;
}

static int setrfs_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;
	printf("#### NOT IMPLEMENTED! ####");
	printf("setrfs_chown\n");

	return 0;
}

static int setrfs_truncate(const char *path, off_t size)
{
	int res;
	printf("#### NOT IMPLEMENTED! ####");
	printf("setrfs_truncate\n");

	return 0;
}

#ifdef HAVE_UTIMENSAT
static int setrfs_utimens(const char *path, const struct timespec ts[2])
{
	int res;
	printf("#### NOT IMPLEMENTED! ####");
	printf("setrfs_utimens\n");

	return 0;
}
#endif

static int setrfs_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) isdatasync;
	(void) fi;
	printf("setrfs_fsync\n");
	printf("#### NOT IMPLEMENTED! ####");
	return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int setrfs_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
	int fd;
	int res;
	printf("#### NOT IMPLEMENTED! ####");
	printf("setrfs_fallocate\n");
	return 0
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int setrfs_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
printf("#### NOT IMPLEMENTED! ####");
printf("setrfs_setxattr\n");

	return 0;
}

static int setrfs_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
printf("#### NOT IMPLEMENTED! ####");
printf("setrfs_getxattr\n");

	return res;
}

static int setrfs_listxattr(const char *path, char *list, size_t size)
{
printf("#### NOT IMPLEMENTED! ####");
printf("setrfs_listxattr\n");

	return res;
}

static int setrfs_removexattr(const char *path, const char *name)
{
printf("#### NOT IMPLEMENTED! ####");
printf("setrfs_removexattr\n");

	return 0;
}
#endif /* HAVE_SETXATTR */

static struct fuse_operations setrfs_oper = {
	.init 		= setrfs_init,
	.getattr	= setrfs_getattr,
	.access		= setrfs_access,
	.readlink	= setrfs_readlink,
	.readdir	= setrfs_readdir,
	.mknod		= setrfs_mknod,
	.mkdir		= setrfs_mkdir,
	.symlink	= setrfs_symlink,
	.unlink		= setrfs_unlink,
	.rmdir		= setrfs_rmdir,
	.rename		= setrfs_rename,
	.link		= setrfs_link,
	.chmod		= setrfs_chmod,
	.chown		= setrfs_chown,
	.truncate	= setrfs_truncate,
#ifdef HAVE_UTIMENSAT
	.utimens	= setrfs_utimens,
#endif
	.open		= setrfs_open,
	.read		= setrfs_read,
	.write		= setrfs_write,
	.statfs		= setrfs_statfs,
	.release	= setrfs_release,
	.fsync		= setrfs_fsync,
#ifdef HAVE_POSIX_FALLOCATE
	.fallocate	= setrfs_fallocate,
#endif
#ifdef HAVE_SETXATTR
	.setxattr	= setrfs_setxattr,
	.getxattr	= setrfs_getxattr,
	.listxattr	= setrfs_listxattr,
	.removexattr	= setrfs_removexattr,
#endif
};

int main(int argc, char *argv[])
{
	umask(0);
	return fuse_main(argc, argv, &setrfs_oper, NULL);
}
