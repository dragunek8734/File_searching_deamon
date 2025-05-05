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


#define DEFAULT_INTERVAL 30     // basic interval

// variables to control scanning
volatile sig_atomic_t scan_now = 0;
volatile sig_atomic_t scan_stop = 0;

// not used
//volatile sig_atomic_t scanning = 0;



char **patterns = NULL;
int patterns_count = 0;
unsigned int sleep_interval = DEFAULT_INTERVAL;
int verbose = 0;


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

// sigusr signals handler
void signal_handler(int signum)
{
    if (signum == SIGUSR1) {
        scan_now = 1;
        if(verbose)
            syslog(LOG_INFO,"SIGUSR1 received: scan begun");
    }
    else if (signum == SIGUSR2) {
        scan_stop = 1;
        if(verbose)
            syslog(LOG_INFO,"SIGUSR2 received: scan stopped");
        
    }
}

void log_found(const char *path, const char *pattern)
{
    time_t now = time(NULL);
    char date_str[64];
    //actual date and time
    struct tm *tm_info = localtime(&now);

    //date format
    strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", tm_info);
    syslog(LOG_INFO, "Date: %s | Found: %s | Pattern: %s",date_str, path, pattern);
}



// matching pattern with called arguments
int match_patterns(const char *name)
{
    for( int i = 0; i < patterns_count; i++)
    {
        if(strstr(name, patterns[i]) != NULL)
            return i;
    }
    return -1;
}


void search(const char *path)
{
   

    DIR *dir = opendir(path);
    if (dir == NULL) {
        // unauthorized
        if(verbose)
            syslog(LOG_WARNING,"Can't open folder: %s",path);
        return;
    }

    if(scan_stop || scan_now)
        {
            //syslog(LOG_INFO,"BREAKPOINT");
            return;
        }
           

    


    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
       
        if(scan_stop || scan_now)
        {
            //syslog(LOG_INFO,"BREAKPOINT");
            return;
        }
           

        // skipping . and .. routes in directories
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
            // if its dir, checking if it can be accessed
            if (access(fullpath, R_OK | X_OK) == 0) {
                search(fullpath); // rekursive
            }
            else
            {
                if(verbose)
                    syslog(LOG_WARNING, "Can't open folder: %s", fullpath);
            }
        } else {
            // if its file
            //printf("Znalazłem plik: %s\n", fullpath);

            int pattern_index = match_patterns(entry->d_name);

            if(verbose)
                syslog(LOG_INFO,"Matching patterns with %s",entry->d_name);

            if(pattern_index != -1)
                log_found(fullpath,patterns[pattern_index]);
            else
                if(verbose)
                    syslog(LOG_INFO,"Patterns don't match");
        }

        if(scan_stop || scan_now)
        {
            syslog(LOG_INFO,"BREAKPOINT");
            return;
        }
            
        
    }

    closedir(dir);
    
}



int main(int argc, char* argv[])
{
    if(argc < 2)
    {
        printf("Uzycie: %s [-v] [-t czas] wzorzec1 wzorzec2 ...\n",argv[0]);
        exit(EXIT_FAILURE);
    }

    // VERBOSE - additional logs during active phase of deamon
    int opt;

    while((opt = getopt(argc,argv,"vt:")) != -1)
    {
        switch(opt)
        {
            case 'v':
                verbose = 1;
                break;
            case 't':
                sleep_interval = atoi(optarg);
                break;
            default:
                printf("Uzycie: %s [-v] [-t czas] wzorzec1 wzorzec2 ...\n",argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    patterns_count = argc - optind;
    patterns = &argv[optind];

    openlog("demon", LOG_PID | LOG_CONS, LOG_DAEMON);
    
    create_demon();
    syslog(LOG_INFO, "Daemon active");

    // initializing signals
    signal(SIGUSR1, signal_handler);
    signal(SIGUSR2, signal_handler);

    // main loop
    while (1) {
        if (scan_now) {
            
            syslog(LOG_INFO, "Scan started...");
            scan_now = 0;
            scanning = 1;
            search("/"); // search starting from main dir
            syslog(LOG_INFO, "Scan complete");
        }

        // deamon sleeping for specified interval
        if (verbose) {
            syslog(LOG_INFO, "Demon sleeping for %u seconds", sleep_interval);
        }
        
        for (unsigned int slept = 0; slept < sleep_interval; slept++) {
            // sigusr1 - break loop
            if (scan_now) {
                break;  
            }
            sleep(1);  // sleep for set interval
        }
        scan_now = 1;
        scan_stop = 0;
        
    }

    closelog();

    return 0;
}