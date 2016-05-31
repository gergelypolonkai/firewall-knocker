/* Define this to get vasprintf() */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <stdarg.h>

#include "config.h"
#include "server.h"

/* FILE handle for the log file */
FILE *log_fd;
/* This will point to the beginning of the client list */
client_t *client_list;
/* This will hold all the available file descriptors */
fd_set master;

/*
 * log_message(level, format, ...)
 * Puts a message in the log file. If level is less than CURRENT_LOG_LEVEL (less means more importance), the message gets logged.
 * The log message should not end with a newline character.
 */
static void
log_message(int level, const char *format, ...)
{
	if (CURRENT_LOG_LEVEL >= level)
	{
		va_list ap;
		char *message;
		char date[100];
		time_t t;
		struct tm *tmp;

		t = time(NULL);
		tmp = localtime(&t);

		/* This should never happen */
		if (tmp == NULL)
		{
			// Cannot log the error here, as we caught error in the logger. Simply exit with an error status.
			exit(1);
		}

		// Start handling the variable-length parameters
		va_start(ap, format);
		/* XXX: This is a bit unportable, maybe it should be changed to something else. */
		// Create the formatted message string
		vasprintf(&message, format, ap);
		// End handling the variable-length parameters
		va_end(ap);

		// Zero-fill date's placeholder
		memset(&date, 0, 100);
		// Put the current time stamp into date
		strftime((char *)&date, 100, "%Y-%m-%d %H:%M:%S", tmp);

		// Put the timestamp and the message into the logfile
		fprintf(log_fd, "[%s] %s\n", (char *)&date, message);
		fflush(log_fd);
	}
}

/*
 * sigchld_handler(signal)
 * This is the signal handler for the SIGCHLD signal. It gets called every time a child finishes its work (even with failure)
 */
void
sigchld_handler(int s)
{
	// Log a debug message about the finished child
	log_message(LOG_LEVEL_DEBUG, "Found a hung child.");
	// Clean up all the dead children (otherwise they turn into zombie processes)
	while (waitpid(-1, NULL, WNOHANG) > 0);
}

/*
 * sigterm_handler(signal)
 * This is the signal handler for the SIGTERM signal. It gets called if someone kills the process with SIGTERM.
 */
void
sigterm_handler(int s)
{
	// Log this event as an information (it's not a real error, and shouldn't be a debug-only message)
	log_message(LOG_LEVEL_INFO, "Got SIGTERM, shutting down.");
	// TODO: clean shutdown! (Close client sockets, etc.)
	// Exit after the shutdown.
	exit(1);
}

/* 
 * execute(command, parameter)
 * Fork and execute the given program with exactly one parameter
 */
void
execute(char *command, char *parameter)
{
	pid_t pid;

	/* Do the fork() */
	pid = fork();

	if (pid == 0)
	{
		// If fork() returns zero, we are the child
		// Try to execute the script. This will give control to the executed script. exec() returns only when an error occurs (e.g the command cannot be found).
		log_message(LOG_LEVEL_DEBUG, "Executing '%s \"%s\"'...", command, parameter);
		execl(command, command, parameter, (char *)NULL);
		// If we get here, we got an error, which should be logged
		log_message(LOG_LEVEL_ERROR, "execl: %s", strerror(errno));
		// Close the log file
		fclose(log_fd);
		// TODO: Do a clean shutdown
		exit(1);
	}
}

/*
 * client_new(socket, remote_addr)
 * Create a new client structure with the given data, and fully reset timer
 */
void
client_new(int socket, struct sockaddr_in *remote_addr)
{
	client_t *client_data;
	client_t *temp;
	char *tmp_addr;

	// Allocate memory for the new client's data
	client_data = malloc(sizeof(client_t));
	if (client_data == NULL)
	{
		// Log an error message if allocation fails and exit with failure code
		log_message(LOG_LEVEL_ERROR, "malloc: %s", strerror(errno));
		exit(1);
	}

	// Allocate memory for the new client's IP address
	tmp_addr = malloc(16);
	if (tmp_addr == NULL)
	{
		// Log an error message if allocation fails and exit with failure code. Here we also free the previously allocated client_data
		log_message(LOG_LEVEL_ERROR, "malloc: %s", strerror(errno));
		free(client_data);
		exit(1);
	}
	// Zero-fill the address location, so the string will be surely nul-terminated
	memset(tmp_addr, 0, 16);
	// Get the numeric hostname (IP address) of the remote side
	getnameinfo((const struct sockaddr *)remote_addr, sizeof(struct sockaddr_in), (char *)tmp_addr, 15, NULL, 0, NI_NUMERICHOST);

	// Log the connection
	log_message(LOG_LEVEL_INFO, "New connection: %d (IP: %s)", socket, tmp_addr);

	// Fill the client_data struct
	client_data->socket = socket;
	client_data->ip = tmp_addr;
	client_data->last_reset = time(NULL);
	client_data->previous = NULL;
	client_data->next = NULL;

	// If the client list is empty (this is the first client)
	if (!client_list)
	{
		// The client_list should point to the newly allocated struct
		client_list = client_data;
	}
	// Otherwise (this is not the first client
	else
	{
		// Set temp to the last element of the client list
		for (temp = client_list; temp->next; temp = temp->next);
		// Set the last element's next pointer to point to the newly allocated struct
		temp->next = client_data;
		// Set the newly allocated struct's previous pointer to the (till here) last client's struct
		client_data->previous = temp;
	}

	// Execute the connect script
	execute(CLIENT_CONNECT_SCRIPT, client_data->ip);
}

/*
 * client_remove(socket)
 * Remove a client identified by its local socket number
 */
void
client_remove(int socket)
{
	client_t *temp;

	// If we don't have an allocated client_list, we simply return, as we won't find the named client. However, this should never happen
	if (!client_list)
		return;
	
	// Walk through the client list
	for (temp = client_list; temp; temp = temp->next)
	{
		// If this element's socket number is the one we are looking for
		if (temp->socket == socket)
		{
			// Logging a message about the disconnection
			log_message(LOG_LEVEL_INFO, "Connection lost: %d (IP: %s)", temp->socket, temp->ip);
			// Remove this client from the linked list
			if (temp->previous)
				temp->previous->next = temp->next;
			if (temp->next)
				temp->next->previous = temp->previous;

			// If this is the first client, set the client_list to point to the second item (or to NULL if this is also the last client)
			if (temp == client_list)
				client_list = temp->next;

			// If we don't have a previous, nor a next client in the list, then this was the last client, so client_list should be NULL
			// This is only a paranoia check, this should already happen in the previous instruction
			if ((!temp->previous) && (!temp->next))
				client_list = NULL;

			// Execute the disconnect script
			execute(CLIENT_DISCONNECT_SCRIPT, temp->ip);

			// Free the IP address' memory
			free(temp->ip);
			// Free the whols struct's memory
			free(temp);

			// Close the socket itself
			close(socket);
			// Remove this socket from the watched sockets' list
			FD_CLR(socket, &master);
		}
	}
}

/*
 * client_reset_timer(socket)
 * Reset a client's timer, identified by the local socket number
 */
void
client_reset_timer(int socket)
{
	client_t *temp;

	// Walk through the client list
	for (temp = client_list; temp; temp = temp->next)
		// If the current client is the one we are looking for
		if (temp->socket == socket)
			// Set its last reset time to the current timestamp
			temp->last_reset = time(NULL);
}

/*
 * check_timers()
 * Check all clients if they have sent data in the near past, and disconnect (thus, deauthenticate) them if not
 */
void
check_timers(void)
{
	client_t *temp;

	// Walk through the client list
	for (temp = client_list; temp; temp = temp->next)
		// If this client hasn't sent data in DROP_AFTER seconds
		if (time(NULL) - temp->last_reset > DROP_AFTER)
		{
			// Log the timeout event
			log_message(LOG_LEVEL_INFO, "Client timeout, dropping connection %d (IP: %s).", temp->socket, temp->ip);
			// And remove the client from the client list
			client_remove(temp->socket);
		}
}

/*
 * main()
 * The main function of the server program
 */
int
main(void)
{
	int sock_listen;
	struct addrinfo hints;
	struct addrinfo *servinfo;
	struct addrinfo *p;
	int yes = 1;
	int rv;
	fd_set read_fds;
	int fdmax;
	struct sigaction sa;

	// Initially set the client list to empty
	client_list = NULL;

	// Set the SIGCHLD handler (which will purge zombie children)
	sa.sa_handler = sigchld_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) < 0)
	{
		perror("sigaction");
		return 1;
	}

	// Set the SIGTERM handler
	sa.sa_handler = sigterm_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGTERM, &sa, NULL) < 0)
	{
		perror("sigaction");
		return 1;
	}

	// Set the hints to "any"
	memset (&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;	// use my IP

	// Check if our port number is already in use
	if ((rv = getaddrinfo (NULL, PORT, &hints, &servinfo)) != 0)
	{
		fprintf(stderr, "getaddrinfo: %s", gai_strerror(rv));
		return 1;
	}

	for (p = servinfo; p != NULL; p = p->ai_next)
	{
		if ((sock_listen = socket (p->ai_family, p->ai_socktype,
		p->ai_protocol)) == -1)
		{
			perror("socket");
			continue;
		}

		if (setsockopt (sock_listen, SOL_SOCKET, SO_REUSEADDR, &yes,
		sizeof (int)) == -1)
		{
			perror("setsockopt");
			exit (1);
		}

		if (bind (sock_listen, p->ai_addr, p->ai_addrlen) == -1)
		{
			close(sock_listen);
			perror("bind");
			continue;
		}

		break;
	}

	if (p == NULL)
	{
		perror("bind");
		return 2;
	}

	freeaddrinfo(servinfo);

	// Start listening on the listener socket
	if (listen(sock_listen, BACKLOG) == -1)
	{
		perror("listen");
		exit (1);
	}

	// Clear the list of the watched sockets
	FD_ZERO(&master);
	FD_ZERO(&read_fds);

	// Add the listener to the watched sockets
	FD_SET(sock_listen, &master);
	fdmax = sock_listen;

	// Try to open (or create) the log file
	if ((log_fd = fopen(LOGFILE, "a")) == NULL)
	{
		perror("fopen");
		exit(1);
	}

	// Try to go into the background
	if (daemon(0, 0) < 0)
	{
		perror("daemon");
		exit(1);
	}

	log_message(LOG_LEVEL_INFO, "Started.");

	while (1)
	{
		struct timeval tv;
		int t;

		// Reset the read_fds with the full list of watched sockets
		read_fds = master;

		// Set the timeout value
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		// Wait for incoming connection or incoming data. "Modified" sockets (ones with incoming connections or incoming data) will be put in read_fds
		t = select(fdmax + 1, &read_fds, NULL, NULL, &tv);

		// If select() returns a negative, it means an error
		if (t < 0)
		{
			// However, if select() was only interrupted by a signal, simply continue
			if (errno == EINTR)
				continue;

			// Otherwise log an error and exit
			log_message(LOG_LEVEL_ERROR, "select: %s", strerror(errno));

			return 1;
		}
		// If select() returns a positive, it means that there are incoming connections or incoming data
		else if (t != 0)
		{
			int sock;

			// Walk through the watched sockets (the lazy way, later this can cause some microseconds of hang up, as not all sockets are really monitored in this set [0..fdmax])
			for (sock = 0; sock <= fdmax; sock++)
			{
				// If the current socket exists in read_fds (it has data to read)
				if (FD_ISSET(sock, &read_fds))
				{
					// If the socket we found is the listener
					if (sock == sock_listen)
					{
						int new_socket;
						struct sockaddr_in remote_addr;
						socklen_t addrlen = sizeof(struct sockaddr_in);

						// Accept the new connection
						new_socket = accept(sock_listen, (struct sockaddr *)&remote_addr, &addrlen);

						// Add the new connection to the watched sockets
						FD_SET(new_socket, &master);
						if (new_socket > fdmax)
							fdmax = new_socket;

						// Create a new client entry for the new connection
						client_new(new_socket, &remote_addr);
					}
					else
					{
						// Otherwise it's an already existing socket which has data to read
						size_t read_len;
						char buf[128];

						// Read the data from the socket (in 128-bytes chunks)
						read_len = recv(sock, (char *)&buf, 128, 0);

						// If recv() returns a negative value, this means an error, so we should remove this client
						if (read_len < 0)
						{
							// In this case we also log an error
							log_message(LOG_LEVEL_ERROR, "recv: %s", strerror(errno));
							client_remove(sock);
						}
						// Otherwise if recv() returns 0, we just simply remove the client (0 means the client already closed the connection)
						else if (read_len == 0)
						{
							client_remove(sock);
						}
						// If recv() returns a positive, the client sent some data, which will be discarded, but the timer of the client is reset
						else
						{
							// Log a debugging message about the reset timer
							log_message(LOG_LEVEL_DEBUG, "Connection timer reset: %d", sock);
							client_reset_timer(sock);
						}
					}
				}
			}
		}

		// After we checked all the sockets, or there was no sockets to check (select() returned 0), we go and check if any clients timed out
		check_timers();
	}

	fclose(log_fd);

	return 0;
}

