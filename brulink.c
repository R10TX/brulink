#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <curl/curl.h>

#define MAX_URL_LENGTH 1024
#define MAX_PATH_LENGTH 1024
#define THREAD_COUNT 4  // Number of threads for parallel processing

// Define color codes for terminal output
#define COLOR_GREEN   "\033[0;32m"
#define COLOR_RED     "\033[0;31m"
#define COLOR_YELLOW  "\033[0;33m"
#define COLOR_BLUE    "\033[0;34m"
#define COLOR_MAGENTA "\033[0;35m"
#define COLOR_CYAN    "\033[0;36m"
#define COLOR_RESET   "\033[0m"

const char *colors[] = {COLOR_GREEN, COLOR_RED, COLOR_YELLOW, COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN};

volatile sig_atomic_t stop;

void handle_sigint(int sig) {
    stop = 1;
}

void print_animated_banner() {
    // Randomly select a color
    srand(time(NULL));
    const char *color = colors[rand() % 6];

    // Define the banner lines
    const char *banner_lines[] = {
        "██████╗  ██╗ ██████╗ ████████╗███████╗██╗  ██╗███████╗ ██████╗",
        "██╔══██╗███║██╔═████╗╚══██╔══╝██╔════╝╚██╗██╔╝██╔════╝██╔════╝",
        "██████╔╝╚██║██║██╔██║   ██║   █████╗   ╚███╔╝ █████╗  ██║     ",
        "██╔══██╗ ██║████╔╝██║   ██║   ██╔══╝   ██╔██╗ ██╔══╝  ██║     ",
        "██║  ██║ ██║╚██████╔╝   ██║   ███████╗██╔╝ ██╗███████╗╚██████╗",
        "╚═╝  ╚═╝ ╚═╝ ╚═════╝    ╚═╝   ╚══════╝╚═╝  ╚═╝╚══════╝ ╚═════╝"
    };
    int num_lines = sizeof(banner_lines) / sizeof(banner_lines[0]);

    // Print each line with delay for animation effect
    for (int i = 0; i < num_lines; i++) {
        printf("%s%s\n" COLOR_RESET, color, banner_lines[i]);
        usleep(200000); // 200ms delay
    }
}

void print_status(long response_code, const char *url) {
    switch (response_code) {
        case 200:
            printf(COLOR_GREEN "200 OK: %s\n" COLOR_RESET, url);
            break;
        case 403:
            printf(COLOR_RED "403 Forbidden: %s\n" COLOR_RESET, url);
            break;
        case 302:
            printf(COLOR_YELLOW "302 Found: %s\n" COLOR_RESET, url);
            break;
        case 404:
            printf(COLOR_BLUE "404 Not Found: %s\n" COLOR_RESET, url);
            break;
        case 500:
            printf(COLOR_MAGENTA "500 Internal Server Error: %s\n" COLOR_RESET, url);
            break;
        default:
            printf("Status %ld: %s\n", response_code, url);
            break;
    }
}

typedef struct {
    char base_url[MAX_URL_LENGTH];
    char wordlist_path[MAX_PATH_LENGTH];
    int start_index;
    int end_index;
} thread_data_t;

void *brute_force_thread(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    FILE *file = fopen(data->wordlist_path, "r");
    if (file == NULL) {
        perror("Failed to open wordlist");
        pthread_exit(NULL);
    }

    CURL *curl;
    CURLcode res;
    char word[MAX_PATH_LENGTH];
    char full_url[MAX_URL_LENGTH];
    int current_index = 0;

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Curl initialization failed\n");
        fclose(file);
        pthread_exit(NULL);
    }

    while (fgets(word, sizeof(word), file)) {
        if (stop) {
            printf("\nProcess interrupted by user.\n");
            break;
        }

        // Remove newline character
        word[strcspn(word, "\n")] = 0;
        current_index++;

        if (current_index < data->start_index || current_index >= data->end_index) {
            continue;
        }

        snprintf(full_url, sizeof(full_url), "%s/%s", data->base_url, word);
        
        curl_easy_setopt(curl, CURLOPT_URL, full_url);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1); // Only get the header, not the body
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1); // Follow redirects
        
        res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            long response_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            print_status(response_code, full_url);
        } else {
            fprintf(stderr, "Failed to access: %s\n", full_url);
        }
    }

    curl_easy_cleanup(curl);
    fclose(file);
    pthread_exit(NULL);
}

void start_bruteforce(const char *base_url, const char *wordlist_path) {
    pthread_t threads[THREAD_COUNT];
    thread_data_t thread_data[THREAD_COUNT];
    FILE *file = fopen(wordlist_path, "r");
    int line_count = 0;

    if (file == NULL) {
        perror("Failed to open wordlist");
        return;
    }

    // Count lines in wordlist
    char line[MAX_PATH_LENGTH];
    while (fgets(line, sizeof(line), file)) {
        line_count++;
    }
    fclose(file);

    int lines_per_thread = (line_count + THREAD_COUNT - 1) / THREAD_COUNT;

    // Create threads
    for (int i = 0; i < THREAD_COUNT; i++) {
        thread_data[i].start_index = i * lines_per_thread;
        thread_data[i].end_index = (i + 1) * lines_per_thread;
        thread_data[i].end_index = (thread_data[i].end_index > line_count) ? line_count : thread_data[i].end_index;
        strcpy(thread_data[i].base_url, base_url);
        strcpy(thread_data[i].wordlist_path, wordlist_path);

        if (pthread_create(&threads[i], NULL, brute_force_thread, &thread_data[i]) != 0) {
            fprintf(stderr, "Error creating thread\n");
            return;
        }
    }

    // Wait for threads to finish
    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_join(threads[i], NULL);
    }
}

int main() {
    char base_url[MAX_URL_LENGTH];
    char wordlist_path[MAX_PATH_LENGTH];

    // Set up signal handler
    signal(SIGINT, handle_sigint);

    // Print the animated banner
    print_animated_banner();

    // Input URL and wordlist path
    printf("Masukkan URL: ");
    fgets(base_url, sizeof(base_url), stdin);
    base_url[strcspn(base_url, "\n")] = 0;  // Remove newline character

    printf("Masukkan path wordlist: ");
    fgets(wordlist_path, sizeof(wordlist_path), stdin);
    wordlist_path[strcspn(wordlist_path, "\n")] = 0;  // Remove newline character

    start_bruteforce(base_url, wordlist_path);

    return EXIT_SUCCESS;
}
