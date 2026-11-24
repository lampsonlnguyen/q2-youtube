#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define BASE "/tmp/q2-youtube"
#define SOCKET_PATH BASE ".browse.sock"
#define VIDEO_DIR "/mnt/mmc/Videos/YouTube"
#define RESULTS BASE ".results"

static volatile sig_atomic_t stopping;
static pid_t download;
static pid_t search;
static const char *search_status;
static char video_id[12];
static char pending[160], destination[160];
static const char *status = "Choose a result to save it to Videos";

static void stop(int signal_number) {
    (void)signal_number;
    stopping = 1;
}

static int valid_id(const char *id) {
    return id && strlen(id) == 11 &&
        strspn(id, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") == 11;
}

static void reap(void) {
    int result;
    if (search && waitpid(search, &result, WNOHANG) == search) {
        search = 0;
        if (WIFEXITED(result) && WEXITSTATUS(result) == 0) {
            search_status = "No matching videos";
        } else {
            unlink(RESULTS);
            search_status = "Search failed. Check companion URL and terminal";
        }
    }
    if (download && waitpid(download, &result, WNOHANG) == download) {
        download = 0;
        if (WIFEXITED(result) && WEXITSTATUS(result) == 0 &&
            rename(pending, destination) == 0) {
            status = "Ready in Library > Apps > Videos > YouTube";
        } else {
            unlink(pending);
            status = WIFEXITED(result) && WEXITSTATUS(result) == 127
                ? "This firmware has no wget"
                : "Transfer failed. Check companion terminal and card space";
        }
    }
}

static const char *read_base(char base[256]) {
    FILE *config = fopen("companion-url", "r");
    if (!config) return "Create .youtube/companion-url on the card";
    int read_ok = fgets(base, 256, config) != NULL;
    fclose(config);
    if (!read_ok) return "Could not read the companion URL";
    base[strcspn(base, "\r\n")] = 0;
    if (strncmp(base, "http://", 7) != 0 || strlen(base) < 16 ||
        base[strlen(base) - 1] == '/' ||
        strspn(base + 7, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.:/_-") != strlen(base + 7))
        return "Check the companion URL in .youtube/companion-url";
    return NULL;
}

static pid_t fetch_file(const char *url, const char *path, const char *timeout) {
    pid_t child = fork();
    if (child == 0) {
        execlp("wget", "wget", "-T", timeout, "-O", path, url, (char *)NULL);
        execl("/bin/busybox", "busybox", "wget", "-T", timeout, "-O", path, url, (char *)NULL);
        _exit(127);
    }
    return child;
}

static const char *start_search(const char *query) {
    if (!query || !*query || strlen(query) > 189) return "Enter a video title";
    if (search) return "A search is already running";
    char base[256];
    const char *error = read_base(base);
    if (error) return error;

    /* Q2SPOT already percent-encodes control bytes. Decode once, then encode
       every byte as URL data so spaces and punctuation reach the companion. */
    char url[850];
    size_t offset = (size_t)snprintf(url, sizeof url, "%s/search?q=", base);
    while (*query) {
        unsigned char byte = (unsigned char)*query++;
        if (byte == '%') {
            if (!isxdigit((unsigned char)query[0]) ||
                !isxdigit((unsigned char)query[1])) return "Invalid search text";
            char hex[] = {query[0], query[1], 0};
            byte = (unsigned char)strtoul(hex, NULL, 16);
            query += 2;
        }
        if (byte < 32 || byte == 127) return "Invalid search text";
        offset += (size_t)snprintf(url + offset, sizeof url - offset, "%%%02X", (unsigned)byte);
    }
    unlink(RESULTS);
    search = fetch_file(url, RESULTS, "60");
    if (search < 0) {
        search = 0;
        return "Could not start the search";
    }
    search_status = "Searching YouTube";
    return "";
}

static const char *queue(const char *id) {
    if (!valid_id(id)) return "Invalid YouTube video ID";
    if (download) return "A video is already transferring";
    char base[256];
    const char *error = read_base(base);
    if (error) return error;

    if ((mkdir("/mnt/mmc/Videos", 0777) < 0 && errno != EEXIST) ||
        (mkdir(VIDEO_DIR, 0777) < 0 && errno != EEXIST))
        return "Could not create the Videos/YouTube folder";

    snprintf(destination, sizeof destination, VIDEO_DIR "/%s.mp4", id);
    snprintf(pending, sizeof pending, VIDEO_DIR "/.%s.part", id);
    memcpy(video_id, id, sizeof video_id);
    if (access(destination, F_OK) == 0) {
        status = "Already saved in Videos > YouTube";
        return "";
    }

    char url[288];
    snprintf(url, sizeof url, "%s/%s", base, id);
    download = fetch_file(url, pending, "900");
    if (download < 0) {
        download = 0;
        return "Could not start the transfer";
    }
    status = "Preparing and transferring video";
    return "";
}

static void escaped(FILE *output, const char *value) {
    for (; *value; ++value) {
        unsigned char byte = (unsigned char)*value;
        if (byte == '%' || byte == '\t' || byte == '\n' || byte == '\r')
            fprintf(output, "%%%02X", (unsigned)byte);
        else fputc(byte, output);
    }
}

static int show_results(FILE *output) {
    FILE *results = fopen(RESULTS, "r");
    if (!results) return 0;
    char row[600];
    int count = 0;
    while (count < 12 && fgets(row, sizeof row, results)) {
        char *title = strchr(row, '\t');
        if (!title) continue;
        *title++ = 0;
        char *channel = strchr(title, '\t');
        if (!channel || !valid_id(row)) continue;
        *channel++ = 0;
        channel[strcspn(channel, "\r\n")] = 0;
        fprintf(output, "action\tfetch\t");
        escaped(output, title);
        fputc('\t', output);
        escaped(output, channel);
        fprintf(output, "\t%s\t\n", row);
        ++count;
    }
    fclose(results);
    return count;
}

static void serve(FILE *connection, char *request) {
    char *save;
    char *version = strtok_r(request, "\n", &save);
    char *line = strtok_r(NULL, "\n", &save);
    if (!version || strcmp(version, "Q2SPOT/1") || !line) return;
    char *id = strtok_r(line, "\t", &save);
    char *op = strtok_r(NULL, "\t", &save);
    char *arg = strtok_r(NULL, "\t", &save);
    if (!id || !op || strspn(id, "0123456789") != strlen(id)) return;

    if (!strcmp(op, "fetch") || !strcmp(op, "find")) {
        const char *message = !strcmp(op, "fetch") ? queue(arg) : start_search(arg);
        fprintf(connection, "Q2SPOT/1\n%s\tok\t%s\t\n", id, message);
        return;
    }
    fprintf(connection, "Q2SPOT/1\n%s\tok\t%s\t\n", id, download || search ? "refresh" : "");
    fprintf(connection, "field\tfind\tSearch YouTube\tEnter a video title\t\t\n");
    fprintf(connection, "info\tstatus\t%s\t%s\t\t\n",
            video_id[0] ? video_id : "Status", status);
    if ((search || !show_results(connection)) && search_status)
        fprintf(connection, "info\tsearch-status\tSearch results\t%s\t\t\n", search_status);
}

int main(void) {
    struct sigaction handler = { .sa_handler = stop };
    sigemptyset(&handler.sa_mask);
    sigaction(SIGTERM, &handler, NULL);
    sigaction(SIGINT, &handler, NULL);
    signal(SIGPIPE, SIG_IGN);

    int lock = open(BASE ".lock", O_RDWR | O_CREAT, 0600);
    struct flock exclusive = { .l_type = F_WRLCK, .l_whence = SEEK_SET };
    if (lock < 0 || fcntl(lock, F_SETLK, &exclusive) < 0) return 1;
    fcntl(lock, F_SETFD, FD_CLOEXEC);
    int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listener < 0) return 1;
    struct sockaddr_un address = { .sun_family = AF_UNIX, .sun_path = SOCKET_PATH };
    unlink(SOCKET_PATH);
    if (bind(listener, (struct sockaddr *)&address, sizeof address) < 0 ||
        chmod(SOCKET_PATH, 0666) < 0 || listen(listener, 4) < 0) {
        unlink(SOCKET_PATH);
        return 1;
    }

    FILE *pid = fopen(BASE, "w");
    if (!pid) {
        unlink(SOCKET_PATH);
        return 1;
    }
    fprintf(pid, "%ld\n", (long)getpid());
    fclose(pid);
    while (!stopping) {
        reap();
        struct pollfd waiting = { .fd = listener, .events = POLLIN };
        if (poll(&waiting, 1, 250) <= 0) continue;
        int client = accept(listener, NULL, NULL);
        if (client < 0) continue;
        fcntl(client, F_SETFD, FD_CLOEXEC);
        struct timeval timeout = { .tv_sec = 2 };
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
        FILE *connection = fdopen(client, "r");
        if (!connection) {
            close(client);
            continue;
        }
        char request[8193];
        size_t length = fread(request, 1, sizeof request - 1, connection);
        request[length] = 0;
        reap();
        if (!ferror(connection) && length < sizeof request - 1 && !memchr(request, 0, length)) {
            /* Separate streams keep socket input and output independent. */
            int output_fd = dup(client);
            FILE *output = output_fd < 0 ? NULL : fdopen(output_fd, "w");
            if (output) {
                fcntl(output_fd, F_SETFD, FD_CLOEXEC);
                serve(output, request);
                fclose(output);
            } else if (output_fd >= 0) close(output_fd);
        }
        fclose(connection);
    }
    if (download) {
        kill(download, SIGKILL);
        waitpid(download, NULL, 0);
        unlink(pending);
    }
    if (search) {
        kill(search, SIGKILL);
        waitpid(search, NULL, 0);
    }
    unlink(RESULTS);
    close(listener);
    unlink(SOCKET_PATH);
    unlink(BASE);
    return 0;
}
