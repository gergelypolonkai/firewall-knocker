// TODO: Put these values into a config file

// Port number to listen on. It must be a string!
#define PORT "2884"

// Drop clients who don't send any packets in this many seconds
#define DROP_AFTER 10

// Logging level. See LOG_LEVEL_* defines in server.h
#define CURRENT_LOG_LEVEL LOG_LEVEL_DEBUG

// Place of the log file
#define LOGFILE "auth.log"

// Script to run when a client connects
#define CLIENT_CONNECT_SCRIPT "/usr/local/sbin/ip_allow"

// Script to run when a client disconnects
#define CLIENT_DISCONNECT_SCRIPT "/usr/local/sbin/ip_block"

// Number of concurrent incoming (non-accepted) connections
#define BACKLOG 10
