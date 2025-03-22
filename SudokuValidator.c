#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <omp.h> // Incluir OpenMP paso 4

#define SIZE 9

int sudokuGrid[SIZE][SIZE];
int valid = 1; // Variable global para indicar si la solución es válida

// Función para verificar filas
int checkRow(int row) {
    int numbers[SIZE + 1] = {0};
    for (int col = 0; col < SIZE; col++) {
        int num = sudokuGrid[row][col];
        if (num < 1 || num > SIZE || numbers[num] != 0) {
            return 0; // Número repetido o fuera de rango
        }
        numbers[num] = 1;
    }
    return 1; // Fila válida
}

// Función para verificar columnas (ejecutada en un hilo)
void* checkColumnThread(void* arg) {
    int col = *((int*)arg);
    int numbers[SIZE + 1] = {0};
    for (int row = 0; row < SIZE; row++) {
        int num = sudokuGrid[row][col];
        if (num < 1 || num > SIZE || numbers[num] != 0) {
            #pragma omp critical
            {
                valid = 0; // Marcar la solución como inválida de manera segura
            }
            break;
        }
        numbers[num] = 1;
    }
    printf("Hilo de columna %d, TID: %ld\n", col, syscall(SYS_gettid));
    pthread_exit(0);
}

// Función para verificar subcuadrícula 3x3
int checkSubgrid(int startRow, int startCol) {
    int numbers[SIZE + 1] = {0};
    for (int row = startRow; row < startRow + 3; row++) {
        for (int col = startCol; col < startCol + 3; col++) {
            int num = sudokuGrid[row][col];
            if (num < 1 || num > SIZE || numbers[num] != 0) {
                return 0; // Número repetido o fuera de rango
            }
            numbers[num] = 1;
        }
    }
    return 1; // Subcuadrícula válida
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <archivo_sudoku>\n", argv[0]);
        return 1;
    }

    // Configurar el número de hilos en OpenMP
    omp_set_num_threads(4); // Limitar el número de hilos a uno

    // Abrir el archivo
    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        perror("Error al abrir el archivo");
        return 1;
    }

    // Mapear el archivo a memoria
    char *fileContent = mmap(NULL, 81, PROT_READ, MAP_PRIVATE, fd, 0);
    if (fileContent == MAP_FAILED) {
        perror("Error al mapear el archivo");
        close(fd);
        return 1;
    }

    // Llenar la matriz 9x9
    for (int row = 0; row < SIZE; row++) {
        for (int col = 0; col < SIZE; col++) {
            sudokuGrid[row][col] = fileContent[row * SIZE + col] - '0';
        }
    }

    // Obtener el PID del proceso padre
    pid_t parentPid = getpid();
    printf("PID del proceso padre: %d\n", parentPid);

    // Crear un proceso hijo para ejecutar ps -p <PID> -lLf
    pid_t pid = fork();
    if (pid == 0) {
        // Proceso hijo
        char pidStr[20];
        sprintf(pidStr, "%d", parentPid); // Convertir el PID a texto
        execlp("ps", "ps", "-p", pidStr, "-lLf", NULL); // Ejecutar ps
        perror("Error al ejecutar ps");
        exit(1);
    } else if (pid > 0) {
        // Proceso padre
        // Crear un hilo para verificar columnas
        pthread_t thread;
        int colToCheck = 0; // Verificar la columna 0
        pthread_create(&thread, NULL, checkColumnThread, &colToCheck);

        // Esperar a que el hilo termine
        pthread_join(thread, NULL);

        // Mostrar el TID del hilo
        printf("TID del hilo de verificación de columnas: %ld\n", syscall(SYS_gettid));

        // Esperar a que el proceso hijo termine
        wait(NULL);

        // Verificar filas y subcuadrículas con OpenMP
        #pragma omp parallel for schedule(dynamic)
        for (int i = 0; i < SIZE; i++) {
            if (!checkRow(i)) {
                #pragma omp critical
                {
                    valid = 0; // Marcar la solución como inválida de manera segura
                }
            }
        }

        #pragma omp parallel for schedule(dynamic)
        for (int row = 0; row < SIZE; row += 3) {
            for (int col = 0; col < SIZE; col += 3) {
                if (!checkSubgrid(row, col)) {
                    #pragma omp critical
                    {
                        valid = 0; // Marcar la solución como inválida de manera segura
                    }
                }
            }
        }

        if (valid) {
            printf("La solución del Sudoku es válida.\n");
        } else {
            printf("La solución del Sudoku no es válida.\n");
        }

        // Crear otro proceso hijo para ejecutar ps -p <PID> -lLf
        pid_t pid2 = fork();
        if (pid2 == 0) {
            // Proceso hijo
            char pidStr[20];
            sprintf(pidStr, "%d", parentPid); // Convertir el PID a texto
            execlp("ps", "ps", "-p", pidStr, "-lLf", NULL); // Ejecutar ps
            perror("Error al ejecutar ps");
            exit(1);
        } else if (pid2 > 0) {
            // Proceso padre
            wait(NULL); // Esperar a que el segundo hijo termine
        } else {
            perror("Error al hacer fork");
        }
    } else {
        perror("Error al hacer fork");
    }

    // Liberar recursos
    munmap(fileContent, 81);
    close(fd);

    return 0;
}