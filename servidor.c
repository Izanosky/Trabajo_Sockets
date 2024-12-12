/*
 * Fichero: servidor.c
 * Autores:
 *	Izan Jiménez Chaves DNI 71049459k
 *	Victor Haro Crespo DNI 76076364T
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#define PUERTO 9459
#define ADDRNOTFOUND 0xffffffff /* return address for unfound host */
#define BUFFERSIZE 1024			/* maximum size of packets to be received */
#define TAM_BUFFER 516
#define MAXHOST 128

extern int errno;

/*
 *			M A I N
 *
 *	This routine starts the server.  It forks, leaving the child
 *	to do all the work, so it does not have to be run in the
 *	background.  It sets up the sockets.  It
 *	will loop forever, until killed by a signal.
 *
 */

void serverTCP(int s, struct sockaddr_in peeraddr_in);
void serverUDP(int s, char *buffer, struct sockaddr_in clientaddr_in);
void errout(char *); /* declare error out routine */
void combinar(char *file1, char *file2, char *outputFile);
void escribirFichero(char *file);

int FIN = 0; /* Para el cierre ordenado */
void finalizar() { FIN = 1; }
int semaforo;

// para identificar el numero de solicitud
int sol = 0;

// Estructura para almacenar las peticiones en udp para ver si estan duplicadas
typedef struct
{
	int client_port;
	char buffer[TAM_BUFFER]; // Almacenar la solicitud
} peticion;

peticion peticiones[100];
int peticion_count = 0;

void wS(int sem_id)
{
	struct sembuf operation = {0, -1, 0};
	if (semop(sem_id, &operation, 1) == -1)
	{
		perror("semop - wait");
		exit(EXIT_FAILURE);
	}
}

void sS(int sem_id)
{
	struct sembuf operation = {0, 1, 0};
	if (semop(sem_id, &operation, 1) == -1)
	{
		perror("semop - signal");
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char *argv[])
{
	int s_TCP, s_UDP; /* connected socket descriptor */
	int ls_TCP;		  /* listen socket descriptor */

	int cc; /* contains the number of bytes read */

	struct sigaction sa = {.sa_handler = SIG_IGN}; /* used to ignore SIGCHLD */

	struct sockaddr_in myaddr_in;	  /* for local socket address */
	struct sockaddr_in clientaddr_in; /* for peer socket address */
	int addrlen;

	fd_set readmask;
	int numfds, s_mayor;

	char buffer[BUFFERSIZE]; /* buffer for packets to be read into */

	char ultima[100];
	int ultimoPuerto = 0;
	memset(ultima, 0, 100);

	struct sigaction vec;

	semaforo = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
	if (semaforo == -1)
	{
		perror("semget");
		exit(EXIT_FAILURE);
	}
	if (semctl(semaforo, 0, SETVAL, 1) == -1)
	{
		perror("semctl");
		exit(EXIT_FAILURE);
	}

	/* Create the listen socket. */
	ls_TCP = socket(AF_INET, SOCK_STREAM, 0);
	if (ls_TCP == -1)
	{
		perror(argv[0]);
		fprintf(stderr, "%s: unable to create socket TCP\n", argv[0]);
		exit(1);
	}
	/* clear out address structures */
	memset((char *)&myaddr_in, 0, sizeof(struct sockaddr_in));
	memset((char *)&clientaddr_in, 0, sizeof(struct sockaddr_in));

	addrlen = sizeof(struct sockaddr_in);

	/* Set up address structure for the listen socket. */
	myaddr_in.sin_family = AF_INET;
	/* The server should listen on the wildcard address,
	 * rather than its own internet address.  This is
	 * generally good practice for servers, because on
	 * systems which are connected to more than one
	 * network at once will be able to have one server
	 * listening on all networks at once.  Even when the
	 * host is connected to only one network, this is good
	 * practice, because it makes the server program more
	 * portable.
	 */
	myaddr_in.sin_addr.s_addr = INADDR_ANY; // cualquier direccion
	myaddr_in.sin_port = htons(PUERTO);

	/* Bind the listen address to the socket. */
	if (bind(ls_TCP, (const struct sockaddr *)&myaddr_in, sizeof(struct sockaddr_in)) == -1)
	{
		perror(argv[0]);
		fprintf(stderr, "%s: unable to bind address TCP\n", argv[0]);
		exit(1);
	}
	/* Initiate the listen on the socket so remote users
	 * can connect.  The listen backlog is set to 5, which
	 * is the largest currently supported.
	 */
	if (listen(ls_TCP, 5) == -1)
	{
		perror(argv[0]);
		fprintf(stderr, "%s: unable to listen on socket\n", argv[0]);
		exit(1);
	}

	/* Create the socket UDP. */
	s_UDP = socket(AF_INET, SOCK_DGRAM, 0);
	if (s_UDP == -1)
	{
		perror(argv[0]);
		printf("%s: unable to create socket UDP\n", argv[0]);
		exit(1);
	}
	/* Bind the server's address to the socket. */
	if (bind(s_UDP, (struct sockaddr *)&myaddr_in, sizeof(struct sockaddr_in)) == -1)
	{
		perror(argv[0]);
		printf("%s: unable to bind address UDP\n", argv[0]);
		exit(1);
	}

	/* Now, all the initialization of the server is
	 * complete, and any user errors will have already
	 * been detected.  Now we can fork the daemon and
	 * return to the user.  We need to do a setpgrp
	 * so that the daemon will no longer be associated
	 * with the user's control terminal.  This is done
	 * before the fork, so that the child will not be
	 * a process group leader.  Otherwise, if the child
	 * were to open a terminal, it would become associated
	 * with that terminal as its control terminal.  It is
	 * always best for the parent to do the setpgrp.
	 */
	setpgrp();

	switch (fork())
	{
	case -1: /* Unable to fork, for some reason. */
		perror(argv[0]);
		fprintf(stderr, "%s: unable to fork daemon\n", argv[0]);
		exit(1);

	case 0: /* The child process (daemon) comes here. */

		/* Close stdin and stderr so that they will not
		 * be kept open.  Stdout is assumed to have been
		 * redirected to some logging file, or /dev/null.
		 * From now on, the daemon will not report any
		 * error messages.  This daemon will loop forever,
		 * waiting for connections and forking a child
		 * server to handle each one.
		 */
		fclose(stdin);
		fclose(stderr);

		/* Set SIGCLD to SIG_IGN, in order to prevent
		 * the accumulation of zombies as each child
		 * terminates.  This means the daemon does not
		 * have to make wait calls to clean them up.
		 */
		if (sigaction(SIGCHLD, &sa, NULL) == -1)
		{
			perror(" sigaction(SIGCHLD)");
			fprintf(stderr, "%s: unable to register the SIGCHLD signal\n", argv[0]);
			exit(1);
		}

		/* Registrar SIGTERM para la finalizacion ordenada del programa servidor */
		vec.sa_handler = (void *)finalizar;
		vec.sa_flags = 0;
		if (sigaction(SIGTERM, &vec, (struct sigaction *)0) == -1)
		{
			perror(" sigaction(SIGTERM)");
			fprintf(stderr, "%s: unable to register the SIGTERM signal\n", argv[0]);
			exit(1);
		}

		while (!FIN)
		{
			/* Meter en el conjunto de sockets los sockets UDP y TCP */
			FD_ZERO(&readmask);
			FD_SET(ls_TCP, &readmask);
			FD_SET(s_UDP, &readmask);
			/*
			Seleccionar el descriptor del socket que ha cambiado. Deja una marca en
			el conjunto de sockets (readmask)
			*/
			if (ls_TCP > s_UDP)
				s_mayor = ls_TCP;
			else
				s_mayor = s_UDP;

			if ((numfds = select(s_mayor + 1, &readmask, (fd_set *)0, (fd_set *)0, NULL)) < 0)
			{
				if (errno == EINTR)
				{
					FIN = 1;
					close(ls_TCP);
					close(s_UDP);
					perror("\nFinalizando el servidor. Se�al recibida en elect\n ");
				}
			}
			else
			{

				/* Comprobamos si el socket seleccionado es el socket TCP */
				if (FD_ISSET(ls_TCP, &readmask))
				{
					/* Note that addrlen is passed as a pointer
					 * so that the accept call can return the
					 * size of the returned address.
					 */
					/* This call will block until a new
					 * connection arrives.  Then, it will
					 * return the address of the connecting
					 * peer, and a new socket descriptor, s,
					 * for that connection.
					 */
					wS(semaforo);
					sol++;
					sS(semaforo);

					s_TCP = accept(ls_TCP, (struct sockaddr *)&clientaddr_in, &addrlen);

					if (s_TCP == -1)
						exit(1);
					switch (fork())
					{
					case -1: /* Can't fork, just exit. */
						exit(1);
					case 0:			   /* Child process comes here. */
						close(ls_TCP); /* Close the listen socket inherited from the daemon. */
						serverTCP(s_TCP, clientaddr_in);
						exit(0);
					default: /* Daemon process comes here. */
							 /* The daemon needs to remember
							  * to close the new accept socket
							  * after forking the child.  This
							  * prevents the daemon from running
							  * out of file descriptor space.  It
							  * also means that when the server
							  * closes the socket, that it will
							  * allow the socket to be destroyed
							  * since it will be the last close.
							  */
						close(s_TCP);
					}
				} /* De TCP*/
				/* Comprobamos si el socket seleccionado es el socket UDP */
				if (FD_ISSET(s_UDP, &readmask))
				{
					/* This call will block until a new
					for a null character.
					 */
					cc = recvfrom(s_UDP, buffer, BUFFERSIZE - 1, 0,
								  (struct sockaddr *)&clientaddr_in, &addrlen);

					if (cc == -1)
					{
						perror(argv[0]);
						printf("%s: recvfrom error\n", argv[0]);
						exit(1);
					}

					// if (buffer[cc - 1] != '\n' && buffer[cc - 2] != '\r')
					// {
					// 	// Si el mensaje no está completo, se salta esta ejecución
					// 	memset(buffer, 0, sizeof(buffer));
					// 	continue;
					// }

					/* Make sure the message received is
					 * null terminated.
					 */

					buffer[cc] = '\0';

					for (int i = 0; i < peticion_count; i++)
					{
						if (peticiones[i].client_port == ntohs(clientaddr_in.sin_port) &&
							strcmp(peticiones[i].buffer, buffer) == 0)
						{
							continue;
						}
					}

					peticiones[peticion_count].client_port = ntohs(clientaddr_in.sin_port);
					strncpy(peticiones[peticion_count].buffer, buffer, TAM_BUFFER);
					peticion_count++;
					if (peticion_count == 100)
					{
						peticion_count = 0;
					}

					// Actualiza los valores para la siguiente comparación
					strcpy(ultima, buffer);

					ultimoPuerto = ntohs(clientaddr_in.sin_port);
					wS(semaforo);
					sol++;
					sS(semaforo);

					serverUDP(s_UDP, buffer, clientaddr_in);
				}
				// *request arrives.Then, it will *return the address of the client,
				// 	*and a buffer containing its request.* BUFFERSIZE - 1 bytes are read so that * room is left at the end of the buffer
				// 																					   *
			}
		} /* Fin del bucle infinito de atenci�n a clientes */
		/* Cerramos los sockets UDP y TCP */
		close(ls_TCP);
		close(s_UDP);

		printf("\nFin de programa servidor!\n");
		if (semctl(semaforo, 0, IPC_RMID) == -1)
		{
			perror("semctl IPC_RMID");
			exit(EXIT_FAILURE);
		}

	default: /* Parent process comes here. */
		exit(0);
	}
}

/*
 *				S E R V E R T C P
 *
 *	This is the actual server routine that the daemon forks to
 *	handle each individual connection.  Its purpose is to receive
 *	the request packets from the remote client, process them,
 *	and return the results to the client.  It will also write some
 *	logging information to stdout.
 *
 */
void serverTCP(int s, struct sockaddr_in clientaddr_in)
{
	int reqcnt = 0;
	char buf[TAM_BUFFER];
	char hostname[MAXHOST];

	int len, len1, status;
	struct hostent *hp;
	long timevar;

	struct linger linger;

	int id = sol;

	status = getnameinfo((struct sockaddr *)&clientaddr_in, sizeof(clientaddr_in),
						 hostname, MAXHOST, NULL, 0, 0);
	if (status)
	{
		if (inet_ntop(AF_INET, &(clientaddr_in.sin_addr), hostname, MAXHOST) == NULL)
			perror(" inet_ntop \n");
	}

	time(&timevar);

	linger.l_onoff = 1;
	linger.l_linger = 1;
	if (setsockopt(s, SOL_SOCKET, SO_LINGER, &linger,
				   sizeof(linger)) == -1)
	{
		errout(hostname);
	}

	// bucle de recepcion de requests
	int flag = 1;

	while (len = recv(s, buf, TAM_BUFFER, 0))
	{
		if (len == -1)
		{
			errout(hostname);
		}

		if (len == 0)
		{
			break;
		}

		if (len > 0 && strncmp(&buf[len - 2], "\r\n", 2) == 0)
		{
			flag = 0;
		}
		else
		{
			flag = 1;
		}

		while (flag != 0 && len < TAM_BUFFER)
		{
			len1 = recv(s, &buf[len], TAM_BUFFER - len, 0);

			if (len1 == -1)
			{
				errout(hostname);
			}
			if (len1 == 0)
			{
				break;
			}

			len += len1;

			int length = strlen(buf);
			if (length > 0 && strncmp(&buf[len - 2], "\r\n", 2) == 0)
			{
				flag = 0;
			}
		}
		buf[len] = '\0';

		char cmn[TAM_BUFFER];
		char f1[50];
		char f2[50];
		char f3[50];
		FILE *f;
		char abuf[TAM_BUFFER];
		char usr[50];
		char nombre[50];
		char *aux;

		memset(usr, 0, sizeof(usr));
		memset(nombre, 0, sizeof(usr));
		memset(abuf, 0, sizeof(abuf));

		strncpy(usr, buf, len);
		// si el usuario solo contiene \r\n escribe en nombre "all"
		// si no, escribe el usuario en nombre
		if (strcmp(usr, "\r\n") == 0)
		{
			strncpy(nombre, "all", 3);
			strcat(nombre, "\0");
		}
		else
		{
			strncpy(nombre, usr, len);
			strcat(nombre, "\0");
		}

		// este if es para cuando ponemos ./cliente TCP @localhost, es decir, todos los usuarios activos
		if (strcmp(usr, "\r\n") == 0)
		{
			snprintf(f1, 50, "aux%d.txt", id);
			snprintf(f2, 50, "id%d.txt", id);
			snprintf(f3, 50, "salida%d.txt", id);

			char f4[50];
			snprintf(f4, 50, "./who%d.txt", id);

			snprintf(cmn, TAM_BUFFER, "who | awk '{print $1}' > %s", f4);
			system(cmn);

			f = fopen(f4, "r");
			if (f == NULL)
			{
				// printf("Error opening file!\n");
				if (send(s, "Error opening file!\r\n", TAM_BUFFER, 0) != TAM_BUFFER)
					errout(hostname);
				break;
			}

			

			while (fgets(usr, 50, f) != NULL)
			{
				// eliminamos el salto de
				if ((aux = strchr(usr, '\n')) != NULL)
					*aux = '\0';

				// ignoramos líneas vacías
				if (strlen(usr) <= 3)
					continue;

				snprintf(cmn, TAM_BUFFER,
						 "getent passwd | grep -iw %s | awk -F: '{print $1 \"|\" $5 \"|\" $6 \"|\" $7}' >> %s",
						 usr, f1);

				printf("Comando generado: %s\n", cmn);

				// Ejecutar el comando
				system(cmn);
			}
			fclose(f);

			snprintf(cmn, TAM_BUFFER, "touch %s", f1);
			system(cmn);

			f = fopen(f1, "r");
			if (f == NULL)
			{
				// printf("Error opening file!\n");
				if (send(s, "Error opening file!\r\n", TAM_BUFFER, 0) != TAM_BUFFER)
					errout(hostname);
				break;
			}

			memset(abuf, 0, sizeof(abuf));

			while (fgets(abuf, TAM_BUFFER, f) != NULL)
			{
				aux = strtok(abuf, "|");
				strcpy(usr, aux);

				snprintf(cmn, TAM_BUFFER,
						 "lastlog -u %s | tail -n +2 | awk '"
						 "{"
						 " term = ($2 ~ /^pts\\/|tty$/ ? $2 : \"\");"
						 " ip = ($3 ~ /^[0-9.]+$/ ? $3 : \"\");"
						 " time_start = (ip ? $4 : (term ? $3 : $2));"
						 " print \"|\" (term ? term : \"\") \"|\" (ip ? ip : \"\") \"|\" substr($0, index($0, time_start));"
						 "}' >> %s",
						 usr, f2);

				system(cmn);
			}
			fclose(f);

			combinar(f1, f2, f3);

			// enviamos la informacion al cliente
			f = fopen(f3, "r");
			if (f == NULL)
			{
				// printf("Error opening file!\n");
				if (send(s, "Error opening file!\r\n", TAM_BUFFER, 0) != TAM_BUFFER)
					errout(hostname);
				break;
			}
			int info = 0;
			while (fgets(abuf, TAM_BUFFER, f) != NULL)
			{
				if (strlen(abuf) == 1 && info == 0)
				{
					strcpy(abuf, "No existe el usuario\r\n");
				}
				strcpy(buf, abuf);
				if (send(s, buf, TAM_BUFFER, 0) != TAM_BUFFER)
					errout(hostname);
				info++;
			}
			if (info == 0)
			{
				strcpy(buf, "No existe el usuario\r\n");
				if (send(s, buf, TAM_BUFFER, 0) != TAM_BUFFER)
					errout(hostname);
			}

			fclose(f);
			remove(f1);
			remove(f2);
			remove(f4);
		}
		else // y este para cuando ponemos un usuario en concreto
		{
			// dividimos el buf para obtener solo el usuario
			aux = strtok(usr, "\r\n");
			strcpy(abuf, aux);
			strcpy(usr, abuf);

			snprintf(f1, 50, "./aux%d.txt", id);
			snprintf(f2, 50, "./id%d.txt", id);
			snprintf(f3, 50, "./salida%d.txt", id);

			// printf("longi: %lu\n", strlen(f1));

			// creamos el comando para obtener la informacion del usuario
			snprintf(cmn, TAM_BUFFER, "getent passwd | grep -iw  %s | awk -F: '{print $1 \"|\" $5 \"|\" $6 \"|\" $7}' > ./aux%d.txt", usr, id);
			system(cmn);

			// snprintf(cmn, TAM_BUFFER, "touch ./aux%d.txt", id);
			// system(cmn);
			f = fopen(f1, "r");
			if (f == NULL)
			{
				// printf("Error opening file!\n");
				if (send(s, "Error opening file!\r\n", TAM_BUFFER, 0) != TAM_BUFFER)
					errout(hostname);
				break;
			}
			// por cada usuario, obtenemos su lastlogin

			snprintf(cmn, TAM_BUFFER, "touch ./id%d.txt", id);
			system(cmn);
			while (fgets(abuf, TAM_BUFFER, f) != NULL)
			{
				aux = strtok(abuf, "|");
				strcpy(usr, aux);

				snprintf(cmn, TAM_BUFFER,
						 "lastlog -u %s | tail -n +2 | awk '"
						 "{"
						 " term = ($2 ~ /^pts\\/|tty$/ ? $2 : \"\");"
						 " ip = ($3 ~ /^[0-9.]+$/ ? $3 : \"\");"
						 " time_start = (ip ? $4 : (term ? $3 : $2));"
						 " print \"|\" (term ? term : \"\") \"|\" (ip ? ip : \"\") \"|\" substr($0, index($0, time_start));"
						 "}' >> id%d.txt",
						 usr, id);
				system(cmn);
			}
			fclose(f);

			combinar(f1, f2, f3);

			// enviamos la informacion al cliente
			f = fopen(f3, "r");
			if (f == NULL)
			{
				// printf("Error opening file!\n");
				if (send(s, "Error opening file!\r\n", TAM_BUFFER, 0) != TAM_BUFFER)
					errout(hostname);
				break;
			}
			int info = 0;
			memset(abuf, 0, sizeof(abuf));
			while (fgets(abuf, TAM_BUFFER, f) != NULL)
			{
				strcpy(buf, abuf);
				if (send(s, buf, TAM_BUFFER, 0) != TAM_BUFFER)
					errout(hostname);
				info++;
				memset(abuf, 0, sizeof(abuf));
			}
			if (info == 0)
			{
				strcpy(buf, "No existe el usuario\r\n");
				if (send(s, buf, TAM_BUFFER, 0) != TAM_BUFFER)
					errout(hostname);
			}
			fclose(f);
			remove(f1);
			remove(f2);
		}

		// logica del fichero
		wS(semaforo);

		f = fopen("peticiones.log", "a");
		if (f == NULL)
		{
			// printf("Error opening file!\n");
			if (send(s, "Error opening file!\r\n", TAM_BUFFER, 0) != TAM_BUFFER)
				errout(hostname);
			break;
		}

		int protocol;
		socklen_t optlen = sizeof(protocol);
		if (getsockopt(s, SOL_SOCKET, SO_PROTOCOL, &protocol, &optlen) == -1)
		{
			perror("getsockopt");
			protocol = -1; // Indica que no se pudo determinar
		}
		const char *protocolName;
		if (protocol == IPPROTO_TCP)
		{
			protocolName = "TCP";
		}
		else if (protocol == IPPROTO_UDP)
		{
			protocolName = "UDP";
		}
		else
		{
			protocolName = "Desconocido";
		}

		// abrimos el fichero con la informacion
		FILE *fl2 = fopen(f3, "r");
		if (fl2 == NULL)
		{
			// printf("Error opening file!\n");
			if (send(s, "Error opening file!\r\n", TAM_BUFFER, 0) != TAM_BUFFER)
				errout(hostname);
			break;
		}

		fprintf(f, "-- COMUNICACION REALIZADA --\n\t HOSTNAME: %s - IP: %s - PROTOCOLO: %s - PUERTO EFIMERO: %u\n",
				hostname, inet_ntoa(clientaddr_in.sin_addr), protocolName, ntohs(clientaddr_in.sin_port));
		fprintf(f, "-- ORDEN RECIBIDA --\n\t HOSTNAME: %s - IP: %s - PROTOCOLO: %s - PUERTO EFIMERO: %u - ORDEN RECIBIDA: %s\n",
				hostname, inet_ntoa(clientaddr_in.sin_addr), protocolName, ntohs(clientaddr_in.sin_port), nombre);
		fprintf(f, "-- RESPUESTA ENVIADA --\n\t HOSTNAME: %s - IP: %s - PROTOCOLO: %s - PUERTO EFIMERO: %u - RESPUESTA ENVIADA: \n",
				hostname, inet_ntoa(clientaddr_in.sin_addr), protocolName, ntohs(clientaddr_in.sin_port));

		while (fgets(abuf, TAM_BUFFER, fl2) != NULL)
		{
			fprintf(f, "\t\t\t%s", abuf);
		}

		fprintf(f, "-- COMUNICACION FINALIZADA --\n\t HOSTNAME: %s - IP: %s - PROTOCOLO: %s - PUERTO EFIMERO: %u\n",
				hostname, inet_ntoa(clientaddr_in.sin_addr), protocolName, ntohs(clientaddr_in.sin_port));
		fprintf(f, "------------------------------------------------------------------------------------------------------------------------------------\n");

		fclose(fl2);
		fclose(f);

		sS(semaforo);
		remove(f3);
		/* Increment the request count. */
		reqcnt++;
		/* This sleep simulates the processing of the
		 * request that a real server might do.
		 */
		sleep(1);
	}

	close(s);

	time(&timevar);

	// printf("Servidor: Completed %s port %u, %d requests, at %s\n",
	// 	   hostname, ntohs(clientaddr_in.sin_port), reqcnt, (char *)ctime(&timevar));
}

/*
 *	This routine aborts the child process attending the client.
 */
void errout(char *hostname)
{
	printf("Connection with %s aborted on error\n", hostname);
	exit(1);
}

/*
 *				S E R V E R U D P
 *
 *	This is the actual server routine that the daemon forks to
 *	handle each individual connection.  Its purpose is to receive
 *	the request packets from the remote client, process them,
 *	and return the results to the client.  It will also write some
 *	logging information to stdout.
 *
 */
void serverUDP(int s, char *buffer, struct sockaddr_in clientaddr_in)
{
	int id = sol;
	struct in_addr reqaddr; /* for requested host's address */
	struct hostent *hp;		/* pointer to host info for requested host */
	int nc, errcode;

	char hostname[MAXHOST];

	struct addrinfo hints, *res;

	int addrlen;

	char cmn[TAM_BUFFER];
	char f1[50];
	char f2[50];
	char f3[50];
	FILE *f;
	char abuf[TAM_BUFFER];
	char usr[50];
	char nombre[50];
	char *aux;

	addrlen = sizeof(struct sockaddr_in);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	/* Treat the message as a string containing a hostname. */
	/* Esta funci�n es la recomendada para la compatibilidad con IPv6 gethostbyname queda obsoleta. */
	errcode = getaddrinfo(buffer, hostname, &hints, &res);
	if (errcode != 0)
	{
		/* Name was not found.  Return a
		 * special value signifying the error. */
		reqaddr.s_addr = ADDRNOTFOUND;
	}
	else
	{
		/* Copy address of host into the return buffer. */
		reqaddr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
	}
	// ---
	// ---

	memset(usr, 0, sizeof(usr));
	memset(nombre, 0, sizeof(usr));
	memset(abuf, 0, sizeof(abuf));

	strncpy(usr, buffer, strlen(buffer));
	// si el usuario solo contiene \r\n escribe en nombre "all"
	// si no, escribe el usuario en nombre
	if (strcmp(usr, "\r\n") == 0)
	{

		strcpy(nombre, "all");
		strcat(nombre, "\0");
	}
	else
	{
		strncpy(nombre, usr, strlen(usr));
		strcat(nombre, "\0");
	}

	// este if es para cuando ponemos ./cliente TCP @localhost, es decir, todos los usuarios activos
	if (strcmp(usr, "\r\n") == 0)
	{
		snprintf(cmn, TAM_BUFFER, "who | awk '{print $1}' > id%d.txt", id);
		snprintf(f1, 50, "./id%d.txt", id);
		system(cmn);

		f = fopen(f1, "r");
		if (f == NULL)
		{
			// printf("Error opening file!\n");
			nc = sendto(s, "ms", TAM_BUFFER,
						0, (struct sockaddr *)&clientaddr_in, addrlen);
			if (nc == -1)
			{
				perror("serverUDP");
				printf("%s: sendto error\n", "serverUDP");
				freeaddrinfo(res);
				return;
			}
			return;
		}

		snprintf(f2, 50, "touch ./aux%d.txt", id);
		system(f2);

		while (fgets(usr, 50, f) != NULL)
		{
			// hacemos esto para eliminar el \n del final ya que si no, nunca encuentra usuarios
			if ((aux = strchr(usr, '\n')) != NULL)
				*aux = '\0';
			snprintf(cmn, TAM_BUFFER, "getent passwd | grep -iw  %s | awk -F: '{print $1 \"|\" $5 \"|\" $6 \"|\" $7}' >> ./aux%d.txt", usr, id);
			system(cmn);
		}

		fclose(f);
		remove(f1);

		snprintf(f1, 50, "./aux%d.txt", id);
		snprintf(f2, 50, "./id%d.txt", id);
		snprintf(f3, 50, "./salida%d.txt", id);
		f = fopen(f1, "r");
		if (f == NULL)
		{
			// printf("Error opening file!\n");
			nc = sendto(s, "ms", TAM_BUFFER,
						0, (struct sockaddr *)&clientaddr_in, addrlen);
			if (nc == -1)
			{
				perror("serverUDP");
				printf("%s: sendto error\n", "serverUDP");
				freeaddrinfo(res);
				return;
			}
			return;
		}

		snprintf(cmn, TAM_BUFFER, "touch ./id%d.txt", id);
		system(cmn);
		while (fgets(abuf, TAM_BUFFER, f) != NULL)
		{
			aux = strtok(abuf, "|");
			strcpy(usr, aux);

			snprintf(cmn, TAM_BUFFER,
					 "lastlog -u %s | tail -n +2 | awk '"
					 "{"
					 " term = ($2 ~ /^pts\\/|tty$/ ? $2 : \"\");"
					 " ip = ($3 ~ /^[0-9.]+$/ ? $3 : \"\");"
					 " time_start = (ip ? $4 : (term ? $3 : $2));"
					 " print \"|\" (term ? term : \"\") \"|\" (ip ? ip : \"\") \"|\" substr($0, index($0, time_start));"
					 "}' >> id%d.txt",
					 usr, id);
			system(cmn);
		}
		fclose(f);

		combinar(f1, f2, f3);

		// enviamos la informacion al cliente
		f = fopen(f3, "r");
		if (f == NULL)
		{
			// printf("Error opening file!\n");
			nc = sendto(s, "ms", TAM_BUFFER,
						0, (struct sockaddr *)&clientaddr_in, addrlen);
			if (nc == -1)
			{
				perror("serverUDP");
				printf("%s: sendto error\n", "serverUDP");
				freeaddrinfo(res);
				return;
			}
			return;
		}

		int info = 0;
		while (fgets(abuf, TAM_BUFFER, f) != NULL)
		{
			if (strlen(abuf) == 1 && info == 0)
			{
				strcpy(abuf, "No existe el usuario\r\n");
			}
			// mostramos el hosta quien enviamos

			strcpy(buffer, abuf);
			nc = sendto(s, buffer, TAM_BUFFER,
						0, (struct sockaddr *)&clientaddr_in, addrlen);

			if (nc == -1)
			{
				perror("serverUDP");
				printf("%s: sendto error\n", "serverUDP");
				freeaddrinfo(res);
				return;
			}
			info++;
		}
		if (info == 0)
		{
			strcpy(buffer, "No existe el usuario\r\n");
			nc = sendto(s, buffer, TAM_BUFFER,
						0, (struct sockaddr *)&clientaddr_in, addrlen);

			if (nc == -1)
			{
				perror("serverUDP");
				printf("%s: sendto error\n", "serverUDP");
				freeaddrinfo(res);
				return;
			}
		}

		fclose(f);
		remove(f1);
		remove(f2);
	}
	else // y este para cuando ponemos un usuario en concreto
	{
		// dividimos el buf para obtener solo el usuario
		aux = strtok(usr, "\r\n");
		strcpy(abuf, aux);
		strcpy(usr, abuf);

		snprintf(f1, 50, "./aux%d.txt", id);
		snprintf(f2, 50, "./id%d.txt", id);
		snprintf(f3, 50, "./salida%d.txt", id);

		// printf("longi: %lu\n", strlen(f1));

		// creamos el comando para obtener la informacion del usuario
		snprintf(cmn, TAM_BUFFER, "getent passwd | grep -iw  %s | awk -F: '{print $1 \"|\" $5 \"|\" $6 \"|\" $7}' > ./aux%d.txt", usr, id);
		system(cmn);

		// snprintf(cmn, TAM_BUFFER, "touch ./aux%d.txt", id);
		// system(cmn);
		f = fopen(f1, "r");
		if (f == NULL)
		{
			// printf("Error opening file!\n");
			perror("serverUDP");
			printf("%s: sendto error\n", "serverUDP");
			freeaddrinfo(res);
			return;
		}
		// por cada usuario, obtenemos su lastlogin

		snprintf(cmn, TAM_BUFFER, "touch ./id%d.txt", id);
		system(cmn);
		while (fgets(abuf, TAM_BUFFER, f) != NULL)
		{
			aux = strtok(abuf, "|");
			strcpy(usr, aux);

			snprintf(cmn, TAM_BUFFER,
					 "lastlog -u %s | tail -n +2 | awk '"
					 "{"
					 " term = ($2 ~ /^pts\\/|tty$/ ? $2 : \"\");"
					 " ip = ($3 ~ /^[0-9.]+$/ ? $3 : \"\");"
					 " time_start = (ip ? $4 : (term ? $3 : $2));"
					 " print \"|\" (term ? term : \"\") \"|\" (ip ? ip : \"\") \"|\" substr($0, index($0, time_start));"
					 "}' >> id%d.txt",
					 usr, id);
			system(cmn);
		}
		fclose(f);

		combinar(f1, f2, f3);

		// enviamos la informacion al cliente
		f = fopen(f3, "r");
		if (f == NULL)
		{
			// printf("Error opening file!\n");
			perror("serverUDP");
			printf("%s: sendto error\n", "serverUDP");
			freeaddrinfo(res);
			return;
		}

		int info = 0;
		memset(abuf, 0, sizeof(abuf));
		while (fgets(abuf, TAM_BUFFER, f) != NULL)
		{
			strcpy(buffer, abuf);
			nc = sendto(s, buffer, TAM_BUFFER,
						0, (struct sockaddr *)&clientaddr_in, addrlen);
			if (nc == -1)
			{
				perror("serverUDP");
				printf("%s: sendto error\n", "serverUDP");
				freeaddrinfo(res);
				return;
			}
			info++;
			memset(abuf, 0, sizeof(abuf));
			memset(buffer, 0, sizeof(buffer));
		}

		if (info == 0)
		{
			strcpy(buffer, "No existe el usuario\r\n");
			nc = sendto(s, buffer, TAM_BUFFER,
						0, (struct sockaddr *)&clientaddr_in, addrlen);
			if (nc == -1)
			{
				perror("serverUDP");
				printf("%s: sendto error\n", "serverUDP");
				freeaddrinfo(res);
				return;
			}
		}

		fclose(f);
		remove(f1);
		remove(f2);
	}

	// logica del fichero
	wS(semaforo);

	f = fopen("peticiones.log", "a");
	if (f == NULL)
	{
		perror("serverUDP");
		printf("%s: sendto error\n", "serverUDP");
		freeaddrinfo(res);
		return;
	}

	int protocol;
	socklen_t optlen = sizeof(protocol);
	if (getsockopt(s, SOL_SOCKET, SO_PROTOCOL, &protocol, &optlen) == -1)
	{
		perror("getsockopt");
		protocol = -1; // Indica que no se pudo determinar
	}
	const char *protocolName;
	if (protocol == IPPROTO_TCP)
	{
		protocolName = "TCP";
	}
	else if (protocol == IPPROTO_UDP)
	{
		protocolName = "UDP";
	}
	else
	{
		protocolName = "Desconocido";
	}

	// abrimos el fichero con la informacion
	FILE *fl2 = fopen(f3, "r");
	if (fl2 == NULL)
	{
		// printf("Error opening file!\n");
		perror("serverUDP");
		printf("%s: sendto error\n", "serverUDP");
		freeaddrinfo(res);
		return;
	}

	struct sockaddr_in *sa = &clientaddr_in;
	if (getnameinfo((struct sockaddr *)sa, addrlen, hostname, sizeof(hostname), NULL, 0, NI_NAMEREQD) != 0)
	{
		perror("getnameinfo");
		return;
	}

	fprintf(f, "-- COMUNICACION REALIZADA --\n\t HOSTNAME: %s - IP: %s - PROTOCOLO: %s - PUERTO EFIMERO: %u\n",
			hostname, inet_ntoa(clientaddr_in.sin_addr), protocolName, ntohs(clientaddr_in.sin_port));
	fprintf(f, "-- ORDEN RECIBIDA --\n\t HOSTNAME: %s - IP: %s - PROTOCOLO: %s - PUERTO EFIMERO: %u - ORDEN RECIBIDA: %s\n",
			hostname, inet_ntoa(clientaddr_in.sin_addr), protocolName, ntohs(clientaddr_in.sin_port), nombre);
	fprintf(f, "-- RESPUESTA ENVIADA --\n\t HOSTNAME: %s - IP: %s - PROTOCOLO: %s - PUERTO EFIMERO: %u - RESPUESTA ENVIADA: \n",
			hostname, inet_ntoa(clientaddr_in.sin_addr), protocolName, ntohs(clientaddr_in.sin_port));

	while (fgets(abuf, TAM_BUFFER, fl2) != NULL)
	{
		fprintf(f, "\t\t\t%s", abuf);
	}

	fprintf(f, "-- COMUNICACION FINALIZADA --\n\t HOSTNAME: %s - IP: %s - PROTOCOLO: %s - PUERTO EFIMERO: %u\n",
			hostname, inet_ntoa(clientaddr_in.sin_addr), protocolName, ntohs(clientaddr_in.sin_port));
	fprintf(f, "------------------------------------------------------------------------------------------------------------------------------------\n");

	fclose(fl2);
	fclose(f);

	sS(semaforo);
	remove(f3);

	return;
}

void combinar(char *file1, char *file2, char *outputFile)
{
	FILE *f1 = fopen(file1, "r");
	FILE *f2 = fopen(file2, "r");
	FILE *out = fopen(outputFile, "w");

	if (!f1 || !f2 || !out)
	{
		perror("Error opening file");
		exit(EXIT_FAILURE);
	}

	char line1[TAM_BUFFER];
	char line2[TAM_BUFFER];

	while (1)
	{
		int hasLine1 = (fgets(line1, sizeof(line1), f1) != NULL);
		int hasLine2 = (fgets(line2, sizeof(line2), f2) != NULL);

		if (!hasLine1 && !hasLine2)
		{
			break; // Exit the loop when both files are exhausted
		}

		// Remove newline characters, if present
		if (hasLine1)
		{
			line1[strcspn(line1, "\n")] = '\0';
		}
		if (hasLine2)
		{
			line2[strcspn(line2, "\n")] = '\0';
		}

		// Write the lines combined into the output file
		if (hasLine1 && hasLine2)
		{
			fprintf(out, "%s %s\r\n", line1, line2);
		}
	}

	// Close the files
	fclose(f1);
	fclose(f2);
	fclose(out);
}