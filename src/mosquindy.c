/*
 * pcmb_daemon.c:	Server program
 *					to demonstrate interprocess commnuication
 *					with POSIX message queues
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <assert.h>
#include <sys/time.h>

#include <fcntl.h>
#include <sys/stat.h>

#include <sys/socket.h>
#include <pthread.h>
#include <arpa/inet.h>

#include <stdbool.h>
#include <signal.h>

#ifndef WIN32
#include <unistd.h>
#else
#include <process.h>
#include <winsock2.h>
#endif

#include <zlib.h>

#include <mosquitto.h>
#include "client_shared.h"
#include "utils.h"

#define MAX_RECV_SIZE 1048576 /* 1M */

#define CONF_FILE "/etc/pcmb_daemon.conf"

/* sub */
#define STR_TIME		128 /* Time string len */

#define MAX_PAYLOAD 1024*1024

#define PROCESS_NAME "mosquitto_client"

static int save_message(const char *filename, char *payload, size_t payloadlen);
static void exec_shell(const char *filename);

struct pthread_arg
{
	int sock;
	struct mosquitto	*mosq;
	struct mosquitto	*mosq_2;
};

static bool		quiet = false;
static char		*username = NULL;
static char		*password = NULL;
static int		listen_port = 5088;
static char		*g_sub_topic = "topic/client";

static int running = 1;

/* Delete white-space */
char *del_space(char *in);  

/* Get configuration value */
int get_config(struct mosq_config *cfg);

void my_connect_callback(struct mosquitto *mosq, void *obj, int result)
{
	struct mosq_config *cfg;

	assert(obj);
	cfg = (struct mosq_config *)obj;

	LOGI("Connect to MQTT server success.\n");

	if(!result){
		mosquitto_subscribe(mosq, NULL, g_sub_topic, cfg->qos);
	}else{
		if(result && !cfg->quiet){
			LOGE("%s\n", mosquitto_connack_string(result));
		}
	}
}

void my_disconnect_callback(struct mosquitto *mosq, void *obj, int rc)
{
	/* connected = false; */
}

void my_publish_callback(struct mosquitto *mosq, void *obj, int mid)
{
	/* Do nothing */
}

void my_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
	int ret = 0;
	char filename[256] = {0};

	unsigned char *msg = NULL;
	size_t msg_len = MAX_PAYLOAD;

	if (message->payloadlen && message->payloadlen < MAX_PAYLOAD) {
		/* check data is compressed or not */
		if (strncmp(message->payload, "#", 1)) {
			msg = (unsigned char *)malloc(MAX_PAYLOAD);
			ret = uncompress(msg, &msg_len, message->payload, message->payloadlen);
			if (ret < 0) {
				LOGI("failed to uncomress payload.\n");
				goto error;
			}
		} else {
			//strncpy(msg, message->payload, message->payloadlen);
			msg = message->payload;
			msg_len = message->payloadlen;
		}

		/* check is specified file name */
		if (!strncmp(msg, "#SAVE=path/file#", 6)) {
			// find file name. do not need to find "=" since the first 6 bytes must be "#SAVE=".
			//msg move forward 6 bytes to delete "#SAVE=";
			msg     += 6;
			msg_len -= 6;

			char *q = strstr(msg, "#"); //2nd "#" is end of path.
			if (q == NULL) {
				goto error;
			}

			int path_len = q - (char *)msg;

			strncpy(filename, msg, path_len);
			filename[path_len] = '\0';

			/* remove unuse prefix */
			msg     += path_len + 1;
			msg_len -= path_len + 1;
		}

		ret = save_message(filename, (char *)msg, msg_len);
		if (ret < 0) {
			goto error;
		}

		/* check is shell script */
		if (!strncmp(msg, "#!/bin/sh", 9)) {
			exec_shell(filename);
		}
	}

error:
	/*
	if(cfg->msg_count>0){
		msg_count++;
		if(cfg->msg_count == msg_count){
			process_messages = false;
			mosquitto_disconnect(mosq);
		}
	}
	*/

	if (msg != NULL) {
		free(msg);
	}
	return;
}

int save_message(const char *filename, char *payload, size_t payloadlen)
{
	int fd = 0;
	int ret = 0;

	size_t bytes = 0;
	/* time */
	char str_time[STR_TIME] = {0};
	struct timeval tv;

	if (payload == NULL) {
		return -1;
	}

	if (filename == NULL) {
		/* Get current system time */
			gettimeofday(&tv, NULL);
		/* Transfer current time to string */
			sprintf(str_time, "%ld", tv.tv_sec);
		sprintf(filename, "/tmp/%s", str_time);
	}

	/* if file exist, remove it first */
	ret = access(filename, F_OK);
	if (!ret) {
		unlink(filename);
	}

	fd = open(filename, O_RDWR, 0755);
	if (fd < 0) {
		LOGE("cannot open file: %s\n", filename);
		return -1;
	}

	bytes = write(fd, payload, payloadlen);
	if (bytes < 0) {
		LOGE("cannot write file: %s\n", filename);
		ret = -1;
		goto error;
	}

error:
	close(fd);
	return ret;
}

void exec_shell(const char *filename)
{
	/* TODO */
	pid_t pid = fork();
	if (pid == 0) {
		/* child */
		execl("/bin/sh", "sh", filename, NULL);
	} else if (pid == -1) {
		LOGE("cannot fork child process exec shell script.\n");
	} else {
		/* do nothing */
	}
}

void sig_handler(int sig)
{
	running = 0;
}

char *del_space(char *in)  
{  
    char *out = NULL;  
    char *p = in;  

    while((*p == ' ')||(*p == '\t')) 
		p++;
  
    out = p;
  
    while(1)
    {  
        if (*p == ' ')  break;  
        if (*p == '\n') break;  
        if (*p == '\0') break;  
        if (*p == '\t') break;  
  
        p++;  
    }  
    *p = '\0';  
  
    return out;  
} 

int get_config(struct mosq_config *cfg)  
{  
	/* Open configuretion file */
    FILE *fp = fopen(CONF_FILE, "r");  
    if(!fp)  
    {  
        LOGE("Cannot open %s: [%s]\n", CONF_FILE, strerror(errno));  
        return -1;  
    }  
  
    char tmp[512] = {0}; 
  
	/* Parse line */
    while (fgets(tmp, 512, fp))  
    {  
		/* Comment line */
        if (tmp[0] == '#') continue;  
		/* Blank line */
        if (tmp[0] == '\n') continue;  
  
        char key[256] = {0};  
        char val[256] = {0};  
  
        if (strstr(tmp, "="))
        {  
            sscanf(tmp, "%[0-9a-zA-Z_\t ]=%s", key, val);  
        }  
        else if (strstr(tmp, ":"))
        {  
            sscanf(tmp, "%[0-9a-zA-Z_\t ]:%s", key, val);  
        }  
  
        char *k = del_space(key);  
        char *v = del_space(val);  
  
		if (!strcmp(k, "host") && strlen(v)) {
			cfg->host = strdup(v);
			LOGI("%s\n", cfg->host);
		} else if (!strcmp(k, "port") && strlen(v)) {
			cfg->port = atoi(v);
		} else if (!strcmp(k, "listen_port") && strlen(v)) {
			int tmp_port = atoi(v);
			if (tmp_port < 0 || tmp_port > 65535) {
				LOGE("Invalib port, use default port.\n");
			} else {
				listen_port = atoi(v);
			}
		} else if (!strcmp(k, "subcribe") && strlen(v)) {
			g_sub_topic = strdup(v);
		} else if (!strcmp(k, "cafile") && strlen(v)) {
			cfg->cafile = strdup(v);
		} else if (!strcmp(k, "crt") && strlen(v)) {
			cfg->certfile = strdup(v);
		} else if (!strcmp(k, "key") && strlen(v)) {
			cfg->keyfile = strdup(v);
		} else if (!strcmp(k, "clinetid") && strlen(v)) {
			cfg->id = strdup(v);
		} else if (!strcmp(k, "user") && strlen(v)) {
			cfg->username = strdup(v);
		} else if (!strcmp(k, "password") && strlen(v)) {
			cfg->password = strdup(v);
		} else if (!strcmp(k, "topic") && strlen(v)) {
			cfg->will_topic = strdup(v);
		} else if (!strcmp(k, "payload") && strlen(v)) {
			cfg->will_payload = strdup(v);
		} else if (!strcmp(k, "qos") && strlen(v)) {
			cfg->will_qos = atoi(v);
		} else if (!strcmp(k, "retain") && strlen(v)) {
			cfg->will_retain = atoi(v);
		} else {
			/* Do nothing */
		}
    }  
  
    fclose(fp);  
	return 0;
}  
void print_usage(void)
{
	int major, minor, revision;

	mosquitto_lib_version(&major, &minor, &revision);
	printf("PCMB_Daemon is a simple mqtt client that will publish a message on a single topic and exit.\n");
	printf("PCMB_Daemon version %s running on libmosquitto %d.%d.%d.\n\n", VERSION, major, minor, revision);
	printf("Usage: PCMB_Daemon [-h host] [-k keepalive] [-p port] [-q qos] [-r] {-f file | -l | -n | -m message} -t topic\n");
	printf(" --daemon	: put the PCMB client into the background after starting.\n");
}

ssize_t		/* Read "n" bytes from a descriptor. */
readn(int fd, void *vptr, size_t n)
{
    size_t	nleft;
    ssize_t	nread;
    char	*ptr;
    
    ptr = vptr;
    nleft = n;
    while (nleft > 0) {
	if ( (nread = read(fd, ptr, nleft)) < 0) {
	    if (errno == EINTR)
		nread = 0;		/* and call read() again */
	    else
		return(-1);
	} else if (nread == 0)
	    break;				/* EOF */
	
	nleft -= nread;
	ptr   += nread;
    }
    return(n - nleft);		/* return >= 0 */
}

/*
 * This will handle connection for each client
 */
void *process_connection(void *obj)
{
    /* Get the socket descriptor */
	int rc, sock;

	struct pthread_arg *context		= NULL;

	/* message */
	char buf[MAX_RECV_SIZE] = {0};
	size_t msg_len = 0;
	char *message = NULL;

	/* key and val */
	char key[512] = {0};
	char val[512] = {0};

	int qos = 0, retain = 0, broker = 0;
	char *topic = NULL;

	context = (struct pthread_arg *)obj;
    sock = context->sock;

	struct sockaddr_in client;
	socklen_t client_socklen = sizeof(client);
	rc = recvfrom(sock, buf, MAX_RECV_SIZE, 0, (struct sockaddr *)&client, &client_socklen);
	if (rc < 0) {
		LOGE("Failed to recv message: [%s].\n", strerror(errno));
		return NULL;
	} else if (rc == 0) {
		LOGE("Client disconnected.\n");
		return NULL;
	} else {
		if (!quiet) {
			LOGI("%d bytes message received from %s:%d.\n", rc, inet_ntoa(client.sin_addr), ntohs(client.sin_port));
		}
		/* extract key and value */
		/**
		  *	char *p = strtok(str, ";");
		  *	while (p != NULL)
		  *	{
		  *		sscanf(p, "%[^'=']=%[^'\n']\n", key, val);
		  *		p = strtok (NULL, ";");
		  *	}
		  */
		buf[rc] = '\0';

		char *pbuf = strdup(buf);

		char *tok = strtok(pbuf, ";");
		while (tok != NULL) {
			sscanf(tok, "%[^'=']=%[^'\n']\n", key, val);

			/* qos=0;retain=0;topic=xxxxx;message=xxxxxxxx */
			if (!strcmp(key, "qos")) {
				qos = atoi(val);
			} else if (!strcmp(key, "retain")) {
				retain = atoi(val);
			} else if (!strcmp(key, "topic")) {
				topic = strdup(val);
			} else {
				/* nothing */
				break;
			}

			tok = strtok (NULL, ";");
		}

		char *q = strstr(buf, "message");
		if (q == NULL) {
			LOGE("no message data.\n");
			return NULL;
		}
		int q_len = q - buf;

		q = strstr(buf + q_len + 1, "=");
		if (q == NULL) {
			LOGE("invalid message.\n");
			return NULL;
		}
		q_len = q - buf;

		msg_len = strlen(buf) + 1;
		msg_len -= q_len + 1;
		message = buf + q_len + 1;
	}

	if (!broker) {
		rc = mosquitto_publish(context->mosq, 0, topic, msg_len, message, qos, retain);
	} else {
		rc = mosquitto_publish(context->mosq_2, 0, topic, msg_len, message, qos, retain);
	}

	if (rc != 0) {
		LOGE("Failed to publish message.\n");
		/* mosquitto_disconnect(mosq); */
	}

	close(sock);
	return NULL;
}

int main (int argc, char **argv)
{
	int					rc = 0;

	int					socket_srv;
	struct sockaddr_in	server;

	struct mosq_config	cfg;
	struct mosquitto	*mosq = NULL;
	struct mosquitto	*mosq_2 = NULL;

	/* singleton */
	rc = get_singleton(PROCESS_NAME);
	if (rc == -1)
	{
		LOGE("%s is running\n", PROCESS_NAME);
		exit(EXIT_FAILURE);
	}

	LOGI("Start %s.\n", PROCESS_NAME);

	memset(&cfg, 0, sizeof(struct mosq_config));
	rc = client_config_load(&cfg, CLIENT_PUB, argc, argv);
	if(rc){
		client_config_cleanup(&cfg);
		if(rc == 2){
			/* --help */
			print_usage();
		}else{
			fprintf(stderr, "\nUse 'mosquitto_pub --help' to see usage.\n");
		}
		return 1;
	}

	/* Whether start into backgrounp or not */
	if (cfg.daemon) {
		USE_SYSLOG(argv[0]);
		client_daemonize(cfg.pidfile);
	}

	rc = access(CONF_FILE, F_OK);
	if (rc < 0 && errno == ENOENT) { 
		LOGI("Config file is not exist, use default value.\n");
	} else {
		rc = get_config(&cfg);
		if (rc != 0) {
			LOGE("Failed to parse configuration file.\n");
		}
	}

	quiet = cfg.quiet;
	username = cfg.username;
	password = cfg.password;

	mosquitto_lib_init();

	if(client_id_generate(&cfg, "mosq_client")){
		return 1;
	}
	mosq = mosquitto_new(cfg.id, true, NULL);
	mosq_2 = mosquitto_new("pcmb_pub", true, NULL);
	if(!mosq || !mosq_2){
		switch(errno){
			case ENOMEM:
				if(!quiet) LOGE("Error: Out of memory.\n");
				break;
			case EINVAL:
				if(!quiet) LOGE("Error: Invalid id.\n");
				break;
		}
		mosquitto_lib_cleanup();
		return 1;
	}

	if(client_opts_set(mosq, &cfg)){
		return 1;
	}
	if(client_opts_set(mosq_2, &cfg)){
		return 1;
	}

	/* Set callback */
	mosquitto_connect_callback_set(mosq, my_connect_callback);
	mosquitto_publish_callback_set(mosq, my_publish_callback);
	mosquitto_message_callback_set(mosq, my_message_callback);
	mosquitto_disconnect_callback_set(mosq, my_disconnect_callback);

// loop until connection is succesful.
    do{
	    rc = client_connect(mosq, &cfg);
	}while(!rc);
	
	rc = client_connect(mosq_2, &cfg);
	if(rc) {
		LOGE("Failed to connect mosq: [%d].\n", rc);
		return rc;
	}
	
	mosquitto_loop_start(mosq);
	mosquitto_loop_start(mosq_2);

	/* Create socket */
	socket_srv = socket(AF_INET, SOCK_DGRAM, 0);
	if (socket_srv == -1) {
		LOGE("Could not create socket: [%s]\n", strerror(errno));
		return rc;
	}
	if(!quiet) {
		LOGI("Socket created!\n");
	}

	int enable = 1;
	setsockopt(socket_srv, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));

	/* Prepare the sockaddr_in structure */
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = htons(listen_port);

	/* Bind address */
	rc = bind(socket_srv, (struct sockaddr *)&server, sizeof(server));
	if (rc == -1) {
		LOGE("Failed to bind socket: [%s]\n", strerror(errno));
		return rc;
	}
	if (!quiet) {
		LOGI("Bind socket success!\n");
	}

	while (running) {
		/* wait for data */
		struct pthread_arg context;
		context.sock		= socket_srv;
		context.mosq		= mosq;
		context.mosq_2		= mosq_2;
		process_connection((void *)&context);
	}

	close(socket_srv);

	mosquitto_destroy(mosq);
	mosquitto_destroy(mosq_2);
	mosquitto_lib_cleanup();
	return rc;
}
