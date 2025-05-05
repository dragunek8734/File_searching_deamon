# File_searching_deamon
File-searching daemon (single-threaded version) –
The daemon runs in the background, scanning files for the presence of any specified string patterns. After completing a scan, it sleeps for a defined interval and then repeats the process cyclically. It recursively enters subdirectories it has access to; inaccessible directories are skipped. Found files are logged to the system log, including their full file paths.
