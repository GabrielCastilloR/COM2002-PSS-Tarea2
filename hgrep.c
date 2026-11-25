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
	// Para modo -f
    const char*patron;
    char **archivos;
    int cantidad;
    int siguiente;
    long *resultados;
    pthread_mutex_t mutex;

	// Para modo -p
	int boolP;
	int nThread;
	int captured;
	long totalLines;
	pthread_cond_t cond;
}DatosHilos;

typedef struct{
	DatosHilos* datos;
	int id;
}ArgThread;

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
	ArgThread *arg = argumento;
    DatosHilos *datos = arg->datos; // Si el no está el flag -p funciona normalmente
    long subtotal = 0;

	while (1) {
		// Particion dinamica de archivos
		pthread_mutex_lock(&datos->mutex);
		
		//tomar el siguiente archivo libre
		if (datos->siguiente == datos->cantidad){
			pthread_mutex_unlock(&datos->mutex);
			break;
		}
		int i = datos->siguiente;
		datos->siguiente++;
		pthread_mutex_unlock(&datos->mutex);

		long count = contar_archivo(datos->archivos[i], datos->patron);
		datos->resultados[i] = count;
		if (count > 0) subtotal += count;
    }

	// Implementación -p
	if (datos->boolP){
		pthread_mutex_lock(&datos->mutex);
		datos->totalLines += subtotal;
		datos->captured++;
		
		if (datos->captured == datos->nThread){
			pthread_cond_broadcast(&datos->cond);
		} else{
			while (datos->captured < datos->nThread){
				pthread_cond_wait(&datos->cond, &datos->mutex);
			}
		}
		long total = datos->totalLines;
		pthread_mutex_unlock(&datos->mutex);

		double porcentaje = (total > 0) ? (100.0 * (double)subtotal / (double)total) : 0.0;
		printf("hilo %d: %ld lineas (%.1f%% completado)\n", arg->id, subtotal, porcentaje);
	}

    return NULL;
}

static void buscar_hijo(int escritura, char *argv[], int argc, int turno, int procesos){
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

// Funcion para imprimir resultados y evitar repeticiones
static int printResultados(char **archivos, const long *resultados, int cantidad, int error){
	int coincidencias = 0;
	for (int i = 0; i < cantidad; i++){
		if (resultados[i] < 0){
			error = 1;
			continue;
		}
		if (cantidad > 1) printf("%s:", archivos[i]);
		printf("%ld\n", resultados[i]);
		if (resultados[i] > 0) coincidencias = 1;
	}
	if (error) return 2;
	if (coincidencias) return 0;
	return 1;
}

// Ejecucion en modo -f
static int execProcesos(int argc, char *argv[]){
	if (argc < 5){
		fprintf(stderr, "Uso: %s -f PATRON N ARCHIVO...\n", argv[0]);
		return 2;
	}

	char *fin;
	long cantidad = strtol(argv[3], &fin, 10);
	if (fin == argv[3] || *fin != '\0' || cantidad < 1 || cantidad > N_THREAD){
		fprintf(stderr, "El numero de procesos de ser un entero entre 1 y 64.\n");
		return 2;
	}

	int archivos = argc - 4;
	int procesos = (int)cantidad;
	if (procesos > archivos) procesos = archivos; // Asegura que existan multiples procesos para un solo archivo
	
	long *resultados = malloc(archivos * sizeof(*resultados));
	if (resultados == NULL){
		perror("malloc");
		return 2;
	}
	for (int i = 0; i < archivos; i++) resultados[i] = -1; // "vacia" el arreglo resultados sin liberar memoria

	pid_t hijos[N_THREAD];
	int lecturas[N_THREAD];
	int creados = 0;
	int error = 0;

	// Creacion procesos
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
			for (int j = 0; j < k; j++) close(lecturas[j]);
			buscar_hijo(canal[1], argv, argc, k, procesos);
		}
		close(canal[1]);
		hijos[k] = hijo;
		lecturas[k] = canal[0];
		creados++;
	}

	// Asignacion
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
		if (!feof(entrada) || i < archivos) error = 1;
		fclose(entrada);
	}

	// Clean up
	for (int k = 0; k < creados; k++){
		int estado;
		if (waitpid(hijos[k], &estado, 0) == -1){
			perror("waitpid");
			error = 1;
		} else if (WIFSIGNALED(estado)){
			fprintf(stderr, "hgrep: hijo %d termino por la señal %d.\n", k + 1, WTERMSIG(estado));
			error = 1;
		} else if (!WIFEXITED(estado) || WEXITSTATUS(estado) != 0) error = 1;
	}

	int codigoSalida = printResultados(&argv[4], resultados, archivos, error);
	free(resultados);
	return codigoSalida;
}

static int execHilos(int argc, char *argv[], int boolP){
	int offset = boolP ? 1 : 0; // Pregunta si existe el flag -p y ajusta la cantidad de argumentos minimos esperados

	// control de argumentos
	if (argc < 4 + offset){
		fprintf(stderr, "Uso: %s [-p | -f] PATRON N ARCHIVO...\n", argv[0]);
		return 2;
	}

	char *endPtr;
	long nThread = strtol(argv[2 + offset], &endPtr, 10);
	if (endPtr == argv[2 + offset] || *endPtr != '\0' || nThread < 1 || nThread > N_THREAD){
		fprintf(stderr, "Uso: %s [-p | -f] PATRON N ARCHIVO... (N entre 1 y 64)\n", argv[0]);
		return 2;
	}

	DatosHilos datos;
	datos.patron = argv[1 + offset];
	datos.archivos = &argv[3 + offset];
	datos.cantidad = argc - (3 + offset);
	datos.siguiente = 0;
	datos.boolP = boolP;
	datos.captured = 0;
	datos.totalLines = 0;

	if (nThread > datos.cantidad) nThread = datos.cantidad;
	datos.nThread = (int)nThread;

	datos.resultados = malloc(datos.cantidad * sizeof(*datos.resultados));
	if (datos.resultados == NULL){
		perror("malloc");
		return 2;
	}

	if (pthread_mutex_init(&datos.mutex, NULL) != 0){
		fprintf(stderr, "hgrep: no se pudo inicializar la variable condición(mutex)\n");
		pthread_mutex_destroy(&datos.mutex);
		free(datos.resultados);
		return 2;
	}

	pthread_t hilos[N_THREAD];
	ArgThread args[N_THREAD];
	int creados = 0;
	for (int i = 0; i < nThread; i++){
		args[i].datos = &datos;
		args[i].id = i+1;
		int codigo = pthread_create(&hilos[i], NULL, buscar_hilo, &args[i]);
		if (codigo != 0){
			fprintf(stderr, "hgrep: %s\n", strerror(codigo));
			break;
		}
		creados++;
	}

	// Evitar bloque por fallo pthread_create con -p
	if (boolP && creados < nThread){
		pthread_mutex_lock(&datos.mutex);
		datos.nThread = creados;
		if (datos.captured == datos.nThread) pthread_cond_broadcast(&datos.cond);
		pthread_mutex_unlock(&datos.mutex);
	}

	for (int i = 0; i < creados; i++) pthread_join(hilos[i], NULL);

	int codigoSalida = 2;
	if (creados == nThread){
		codigoSalida = printResultados(datos.archivos, datos.resultados, datos.cantidad, 0);
	}

	pthread_cond_destroy(&datos.cond);
	pthread_mutex_destroy(&datos.mutex);
	free(datos.resultados);
	return codigoSalida;
}

int main(int argc, char* argv[]){
	if (argc > 1 && strcmp(argv[1], "-f") == 0){
		return execProcesos(argc, argv);
	}
	int boolP = (argc > 1 && strcmp(argv[1], "-p") == 0);
	return execHilos(argc, argv, boolP);
}