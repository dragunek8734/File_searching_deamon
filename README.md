# File_searching_deamon
The daemon runs in the background, scanning files for the presence of any specified string patterns. After completing a scan, it sleeps for a defined interval and then repeats the process cyclically. It recursively enters subdirectories it has access to; inaccessible directories are skipped. Found files are logged to the system log, including their full file paths.

The daemon is available in two versions:

Single-threaded, interval-based version
In this mode, the daemon scans periodically at a specified interval using a single thread.

Sending SIGUSR1 interrupts any ongoing scan and immediately restarts the scanning process from the beginning.

Sending SIGUSR2 interrupts the scan and puts the daemon back into a sleep state.

Multi-threaded, signal-controlled version
In this mode, the daemon creates one thread per search pattern provided as an argument, along with a supervising thread responsible for handling user signals.

SIGUSR1 triggers the start of the scanning process.

SIGUSR2 interrupts and stops the current scan.

There is no cyclic scanning; the scan is controlled directly via signals.
