// TODO: Put these values into a config file
#define PORT "2884"                                             // Port number to listen on. It must be a string!
#define DROP_AFTER 10                                           // Drop clients who don't send any packets in this many seconds
#define CURRENT_LOG_LEVEL LOG_LEVEL_DEBUG                       // Logging level. See LOG_LEVEL_* defines in server.h
#define LOGFILE "auth.log"          // Place of the log file
#define CLIENT_CONNECT_SCRIPT "/usr/local/sbin/ip_allow"    // Script to run when a client connects
#define CLIENT_DISCONNECT_SCRIPT "/usr/local/sbin/ip_block"  // Script to run when a client disconnects
#define BACKLOG 10
