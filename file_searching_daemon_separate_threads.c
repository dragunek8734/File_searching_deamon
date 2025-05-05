#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <pthread.h>

#define DEFAULT_INTERVAL 30 // basic interval

volatile sig_atomic_t scan_now = 0;
volatile sig_atomic_t scan_stop = 0;

char **patterns = NULL;
int patterns_count = 0;
unsigned int sleep_interval = DEFAULT_INTERVAL;
int verbose = 0;

typedef struct {
    const char *pattern; // every pattern has its own thread
} thread_data_t;

pthread_t *workers;
pthread_t supervisor;
thread_data_t *workers_data;

void create_demon()
{
    pid_t pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        exit(EXIT_FAILURE);
    }

    if (chdir("/") < 0) {
        exit(EXIT_FAILURE);
    }

    umask(0);

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

// throwing log when pattern found

void log_found(const char *path, const char *pattern)
{
    time_t now = time(NULL);
    char date_str[64];
    struct tm *tm_info = localtime(&now);

    strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", tm_info);
    syslog(LOG_INFO, "Date: %s | Found: %s | Pattern: %s", date_str, path, pattern);
}

void search_dir(const char *path, const char *pattern)
{
    DIR *dir = opendir(path);
    if (dir == NULL) {
        // brak uprawnień
        if(verbose)
            syslog(LOG_WARNING,"Can't open folder: %s",path);
        return;
    }

    if(scan_stop)
        {
            syslog(LOG_INFO,"BREAKPOINT");
            closedir(dir);
            return;
        }
           
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
       
        if(scan_stop )
        {
            //syslog(LOG_INFO,"BREAKPOINT");
            closedir(dir);
            return;
        }
           

        // skipping . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // full path of file/dir
        char fullpath[4096];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);

        struct stat sb;
        if (lstat(fullpath, &sb) == -1) {
            if(verbose)
                syslog(LOG_WARNING,"Can't obtain information about file: %s",fullpath);
            continue;
        }

        
        if (S_ISDIR(sb.st_mode)) {
            
            if (access(fullpath, R_OK | X_OK) == 0) {
                search_dir(fullpath,pattern); // Rekurencja
            }
            else
            {
                if(verbose)
                    syslog(LOG_WARNING, "Can't open folder: %s", fullpath);
            }
        } else {
            
            //printf("Znalazłem plik: %s\n", fullpath);

            if(strstr(entry->d_name,pattern) != NULL)
                log_found(fullpath,pattern);
        }

        if(scan_stop)
        {
            //syslog(LOG_INFO,"BREAKPOINT");
            closedir(dir);
            return;
        }
            
        
    }

    closedir(dir);
    
}

void *search_pattern(void *arg)
{
    thread_data_t *data = (thread_data_t *)arg;
    const char *pattern = data->pattern;

    while (1) {
        while (!scan_now) {
            sleep(1); // awaiting for sigusr1
        }

        if (scan_stop) {
            if (verbose)
                syslog(LOG_INFO, "Thread for pattern %s stopping scan", pattern);
            continue;
        }

        if (verbose)
            syslog(LOG_INFO, "Thread for pattern %s started scanning...", pattern);

        search_dir("/",pattern); // scan starts from main directory

        if (verbose)
            syslog(LOG_INFO, "Thread for pattern %s finished scanning ", pattern);

        
        // scan_now = 0;
        // scan_stop = 0;
    }

    return NULL;
}

void *supervisor_thread(void *arg)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sigaddset(&set, SIGUSR2);

    int sig;
    while (1) {
        sigwait(&set, &sig);
        if (sig == SIGUSR1) {
            if (verbose)
                syslog(LOG_INFO, "Supervisor received SIGUSR1: starting scan");

            scan_stop = 0; 
            scan_now = 1;  
        } 
        else if (sig == SIGUSR2) {
            if (verbose)
                syslog(LOG_INFO, "Supervisor received SIGUSR2: stopping scan");

            scan_now = 0; 
            scan_stop = 1; 
        }
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Uzycie: %s [-v] [-t czas] wzorzec1 wzorzec2 ...\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int opt;
    while ((opt = getopt(argc, argv, "vt:")) != -1) {
        switch (opt) {
            case 'v':
                verbose = 1;
                break;
            case 't':
                sleep_interval = atoi(optarg);
                break;
            default:
                printf("Uzycie: %s [-v] [-t czas] wzorzec1 wzorzec2 ...\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    patterns_count = argc - optind;
    patterns = &argv[optind];

    create_demon(); 
    openlog("demon", LOG_PID | LOG_CONS, LOG_DAEMON);
    
    syslog(LOG_INFO, "Daemon active");

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sigaddset(&set, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &set, NULL); // blocking signals in main thread

    workers = malloc(sizeof(pthread_t) * patterns_count);
    workers_data = malloc(sizeof(thread_data_t) * patterns_count);

    for (int i = 0; i < patterns_count; i++) {
        workers_data[i].pattern = patterns[i];
        pthread_create(&workers[i], NULL, search_pattern, &workers_data[i]);
    }

    pthread_create(&supervisor, NULL, supervisor_thread, NULL);

    for (int i = 0; i < patterns_count; i++) {
        pthread_join(workers[i], NULL);
    }
    pthread_join(supervisor, NULL);

    free(workers);
    free(workers_data);

    closelog();
    return 0;
}