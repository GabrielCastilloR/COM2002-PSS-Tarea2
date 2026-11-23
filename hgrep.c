#define _POSIX_C_SOURCE 200809L 
#include <errno.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <pthread.h> 
#include <unistd.h> 
#include <sys/wait.h>

#define N_THREAD 64

typedef struct {
    const char*patron;
    char **archivos;
    int cantidad;
    int siguiente;
    long *resultados;
    pthread_mutex_t mutex;
}DatosHilos;

static long contar_archivo(const char *nombre, const char *patron){
    FILE *archivo = fopen(nombre, "r");
    if (archivo == NULL){
	fprintf(stderr, "hgrep: %s: %s\n", nombre, strerror(errno));
	return -1;
    }
    char *linea = NULL;
    size_t capacidad = 0;
    long conteo = 0;
    while (getline(&linea, &capacidad, archivo) != -1){
	if (strstr(linea, patron) != NULL){
	    conteo++;
	}
    }
    if (!feof(archivo)){
	fprintf(stderr, "hgrep: %s: %s\n", nombre, strerror(errno));
	conteo = -1;
    }
    free(linea);
    fclose(archivo);
    return conteo;
}
static void *buscar_hilo(void *argumento){
    DatosHilos *datos = argumento;
    while (1) {
	pthread_mutex_lock(&datos->mutex);
	
	//tomar el siguiente archivo libre
	if (datos->siguiente == datos->cantidad){
	    pthread_mutex_unlock(&datos->mutex);
	    break;
	}
	int i = datos->siguiente;
	datos->siguiente++;
	pthread_mutex_unlock(&datos->mutex);
	datos->resultados[i] = contar_archivo(datos->archivos[i], datos->patron);
    }
    return NULL;
}
static void buscar_hijo(int escritura, char *argv[], int argc, int turno, int procesos)
{
    FILE *salida = fdopen(escritura, "w");
    if (salida == NULL) {
	perror("fdopen");
	close(escritura);
	_exit(2);
    }

    //Cada hijo toma los archivos de su turno
    for (int i = 4 + turno; i < argc; i += procesos){
	long conteo = contar_archivo(argv[i], argv[2]);
	if (fprintf(salida, "%ld\n", conteo) < 0){
	    _exit(2);
	}
    }
    if (fclose(salida) == EOF){
	_exit(2);
    }
    _exit(0);
}
int main(int argc, char* argv[]){
    if (argc > 1 && strcmp(argv[1], "-f") == 0){
	if (argc < 5) {
	    fprintf(stderr, "Uso: %s -f PATRON N ARCHIVO...\n", argv[0]);
	    return 2;

	}
	char *fin;
	long cantidad = strtol(argv[3], &fin, 10);

	if (fin == argv[3] || *fin != '\0' || cantidad < 1 || cantidad > 64){
	    fprintf(stderr, "El numero de procesos debe ser un entero entre 1 y 64.\n");
	    return 2;
	}
    int archivos = argc -4;
    int procesos = (int)cantidad;
    if (procesos > archivos){
	procesos = archivos;
    }
    long *resultados = malloc(archivos * sizeof(*resultados));
    if(resultados == NULL){
	perror("malloc");
	return 2;
    }
    for (int i = 0; i < archivos; i++){
	resultados[i] = -1;
    }
    pid_t hijos[64];
    int lecturas[64];
    int creados = 0;
    int error = 0;
    for (int k = 0; k < procesos; k++){
	int canal[2];
	if (pipe(canal) == -1){
	    perror("pipe");
	    error = 1;
	    break;
	}
	pid_t hijo = fork();
	if (hijo == -1){
	    perror("fork");
	    close(canal[0]);
	    close(canal[1]);
	    error = 1;
	    break;
	}
	if (hijo == 0){
	    close(canal[0]);
	    for (int j= 0; j < k; j++){
	        close(lecturas[j]);
	    }
	    buscar_hijo(canal[1], argv, argc,  k, procesos);
	}
	close(canal[1]);
	hijos[k] = hijo;
	lecturas[k] = canal[0];
	creados++;
    }

    //Guardar los conteos en el orden de los archivos
    for (int k = 0; k < creados; k++){
	FILE *entrada = fdopen(lecturas[k], "r");
	if (entrada == NULL){
	    perror("fdopen");
	    close(lecturas[k]);
	    error = 1;
	    continue;
	}
	int i = k;
	long conteo;
	while (fscanf(entrada, "%ld", &conteo) == 1){
	    if (i >= archivos){
	        error = 1;
		break;
	    }
	    resultados[i] = conteo;
	    i += procesos;
	}
	if (!feof(entrada) || i < archivos){
	    error = 1;
	}
	fclose(entrada);
    }
    for (int k = 0; k < creados; k++){
	int estado;
	if (waitpid(hijos[k], &estado, 0) == -1){
	    perror("waitpid");
	    error = 1;
	} else if (WIFSIGNALED(estado)){
	    fprintf(stderr, "hgrep: hijo %d termino por la señal %d\n",
		    k + 1, WTERMSIG(estado));
	    error = 1;
	} else if (!WIFEXITED(estado) || WEXITSTATUS(estado) != 0){
	    error = 1;
	}
    }
    int coincidencias = 0;
    for (int i = 0; i < archivos; i++){
	if (resultados[i] < 0){
	    error = 1;
	    continue;
	}
	if (archivos > 1){
	    printf("%s:", argv[4 + i]);
	}
	printf("%ld\n", resultados[i]);
	if (resultados[i] > 0){
	    coincidencias = 1;
	}
    }
    free(resultados);
    if (error){
        return 2;
    }
    if (coincidencias){
	return 0;
    }
    return 1;
}

    //Control de cantidad de argumentos
    if (argc < 4){
	fprintf(stderr, "Uso: %s PATRON N ARCHIVO...\n", argv[0]);
	return 2;
    }
    char *endPtr;
    long nThread = strtol(argv[2], &endPtr, 10);
    if (endPtr == argv[2] || *endPtr != '\0' || nThread < 1 || nThread > N_THREAD){
	fprintf(stderr, "Uso: %s PATRON N ARCHIVO CON N ENTRE 1 Y 64...\n", argv[0]);
	return 2;
    }
    
    DatosHilos datos;
    datos.patron = argv[1];
    datos.archivos = &argv[3];
    datos.cantidad = argc -3;
    datos.siguiente = 0;
    
    if (nThread > datos.cantidad){
	nThread = datos.cantidad;
    }
    datos.resultados = malloc(datos.cantidad * sizeof(*datos.resultados));
    if (datos.resultados == NULL){
	perror("malloc");
	return 2;
    }

    if (pthread_mutex_init(&datos.mutex, NULL) != 0){
	fprintf(stderr, "hgrep: no se pudo inicializar el mutex\n");
	free(datos.resultados);
	return 2;
    }
    pthread_t hilos[N_THREAD];
    int creados = 0;
    for (int i = 0; i < nThread; i++){
	int codigo = pthread_create(&hilos[i], NULL, buscar_hilo, &datos);
	if (codigo != 0){
	    fprintf(stderr, "hgrep: %s\n", strerror(codigo));
	    break;
	}
	creados++;
    }
    for (int i = 0; i < creados; i++){
	pthread_join(hilos[i], NULL);
    }
    int error = creados != nThread;
    int coincidencias = 0;
    if (creados == nThread){
	for (int i = 0; i < datos.cantidad; i++){
	    long conteo = datos.resultados[i];

	    if (conteo < 0){
	        error = 1;
		continue;
	    }
	    if (datos.cantidad > 1){
		printf("%s:", datos.archivos[i]);
	    }
	    printf("%ld\n", conteo);
	    if (conteo > 0){
		coincidencias = 1;
	    }
	}
    }
    pthread_mutex_destroy(&datos.mutex);
    free(datos.resultados);

    if (error){
	return 2;
    }
    if (coincidencias){
	return 0;
    }
return 1;
}
