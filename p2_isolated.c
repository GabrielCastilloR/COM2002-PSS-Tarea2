#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define N_THREAD 4

typedef struct threadArgs{
    char* target;
    int targetLen;
    char* fname;
    int ocurrencias;
}threadArgs;

void* contarOcurrencias(void* args){

    threadArgs* threadData = (threadArgs *)args;

    threadData->ocurrencias = 0; // Inicializa el contador
    char line[1024]; // Se fijo un tamaño maximo por conveniencia de 1024

    FILE *file = fopen(threadData->fname, "r");
    if (file == NULL){
        fprintf(stderr, "[ERROR] : No se pudo abrir el archivo %s\n", threadData->fname);
        pthread_exit(NULL);
    }

    while (fgets(line, sizeof(line), file) != NULL){
        int lineLen = strlen(line);
        if (threadData->targetLen > lineLen) continue; // Si la linea es mas corta que la palabra no puede haber una ocurrencia, la linea se salta.
        
        int inLine = 0; // bool para verificar que haya al menos una ocurrencia de la palabra en la linea, se asume inicialmente que no.
        for (int i = 0; i <= lineLen - threadData->targetLen; i++){//Itera sobre la linea, hasta el ultimo indice capaz de producir una ocurrencia.
            int isMatch = 1; // bool para verificar coincidencia de la palabra con el substring en la linea, se asume inicialmente verdadero
            for (int j = 0; j < threadData->targetLen; j++){// Itera sobre el substring
                if (threadData->target[j] != line[i+j]){ // Si no hay coincidencia, fija isMatch a 0 y termina la iteracion sobre el substring.
                    isMatch = 0;
                    break;
                }
            }
            if (isMatch == 1){ // Verificacion de ocurrencia, si hay una ocurrencia, no sigue buscando en la linea.
                inLine = 1;
                break;
            }
        }
        if (inLine == 1) threadData->ocurrencias++; // Cuenta la ocurrencia
    }
    fclose(file);
    return NULL;
}

int main(int argc, char* argv[]){

    //Control de cantidad de argumentos
    if (argc < 4){
        fprintf(stderr, "Uso: ./hgrep [palabra] [hilos] [archivos...]\n");
        return 1;
    }

    char* endPtr;
    int nThread = (int)strtol(argv[2], &endPtr, 10); //Convierte el argumento 2 a un int

    //Control de argumento 2
    if (*endPtr != '\0' || nThread > N_THREAD){
        fprintf(stderr, "[ERROR] : Argumento inválido, %s excede el máximod de hilos definidos.\n", argv[2]);
        return 1;
    }

    //thread array
    pthread_t IDS[N_THREAD];

    //malloc para los array de argumentos de IDS[i]
    threadArgs* threadDataArray = malloc(nThread * sizeof(threadArgs));
    if (threadDataArray == NULL){
        fprintf(stderr, "[ERROR] main - threadDataArray : Memoria insuficiente");
        return 1;
    }
    int numFiles = argc - 3;

    for (int i = 3; i < argc; i++){
        int fileIndex = i - 3;
        int threadSlot = fileIndex % nThread;

        if (fileIndex >= nThread){
            pthread_join(IDS[threadSlot], NULL);
            printf("%s:%d\n", threadDataArray[threadSlot].fname, threadDataArray[threadSlot].ocurrencias);
        }
        threadDataArray[threadSlot].target = argv[1];
        threadDataArray[threadSlot].targetLen = strlen(argv[1]);
        threadDataArray[threadSlot].fname = argv[i];

        if (pthread_create(&IDS[threadSlot], NULL, contarOcurrencias, &threadDataArray[threadSlot])){
            perror("pthread_create");
            return 1;
        }
    }
    for (int i = 0; i < nThread && i < numFiles; i++){
        pthread_join(IDS[i], NULL);
        printf("%s:%d\n", threadDataArray[i].fname, threadDataArray[i].ocurrencias);
    }
    free(threadDataArray);
    return 0;
}