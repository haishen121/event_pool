#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

#define handle_error(...) \
    do { fprintf(stderr, __VA_ARGS__); exit(EXIT_FAILURE); } while (0)

#define log(msg, ...) \
    do { time_t t; time(&t); fprintf(stdout, "[%ld] "msg"\n", t, __VA_ARGS__); fflush(stdout);} while (0)

#define NUM_PRODUCERS 2
#define NUM_CONSUMERS 8
#define MAX_EVENTS_SIZE 1024

typedef struct thread_info {
    pthread_t thread_id;
    int rank;
    int epfd;
} thread_info_t;

static void *consumer_routine(void *data) {
    struct thread_info *c = (struct thread_info *)data;
    struct epoll_event *events;
    int epfd = c->epfd;
    int nfds = -1;
    int i = -1;
    uint64_t result;

    log("Greetings from [consumer-%d]", c->rank);
    events = calloc(MAX_EVENTS_SIZE, sizeof(struct epoll_event));
    if (events == NULL) handle_error("calloc epoll events\n");

    for (;;) {
        nfds = epoll_wait(epfd, events, MAX_EVENTS_SIZE, 1000); // poll every second
        for (i = 0; i < nfds; i++) {
            if (events[i].events & EPOLLIN) {
                log("[consumer-%d] got event from fd-%d", c->rank, events[i].data.fd);
                // consume events (reset eventfd)
                read(events[i].data.fd, &result, sizeof(uint64_t));
                close(events[i].data.fd);   // NOTE: need to close here 否则会有未释放的fd
            }
        }
    }
}

static void *producer_routine(void *data) {
    struct thread_info *p = (struct thread_info *)data;
    struct epoll_event event;
    int epfd = p->epfd;
    int efd = -1;
    int ret = -1;

    log("Greetings from [producer-%d]", p->rank);
    while (1) {
        sleep(1);
        // create eventfd (no reuse, create new every time)
        efd = eventfd(1, EFD_CLOEXEC|EFD_NONBLOCK);
        if (efd == -1) handle_error("eventfd create: %s", strerror(errno));
        // register to poller
        event.data.fd = efd;
        event.events = EPOLLIN | EPOLLET;    // Edge-Triggered
        ret = epoll_ctl(epfd, EPOLL_CTL_ADD, efd, &event);
        if (ret != 0) handle_error("epoll_ctl");
        // trigger (repeatedly)
        write(efd, (void *)0xffffffff, sizeof(uint64_t));
    }
}

int main(int argc, char *argv[]) {
    struct thread_info *p_list = NULL, *c_list = NULL;
    int epfd = -1;
    int ret = -1, i = -1;
    // create epoll fd
    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd == -1) handle_error("epoll_create1: %s", strerror(errno));
    // producers
    p_list = calloc(NUM_PRODUCERS, sizeof(struct thread_info));
    if (!p_list) handle_error("calloc");
    for (i = 0; i < NUM_PRODUCERS; i++) {
        p_list[i].rank = i;
        p_list[i].epfd = epfd;
        ret = pthread_create(&p_list[i].thread_id, NULL, producer_routine, &p_list[i]);
        if (ret != 0) handle_error("pthread_create");
    }
    // consumers
    c_list = calloc(NUM_CONSUMERS, sizeof(struct thread_info));
    if (!c_list) handle_error("calloc");
    for (i = 0; i < NUM_CONSUMERS; i++) {
        c_list[i].rank = i;
        c_list[i].epfd = epfd;
        ret = pthread_create(&c_list[i].thread_id, NULL, consumer_routine, &c_list[i]);
        if (ret != 0) handle_error("pthread_create");
    }
    // join and exit
    for (i = 0; i < NUM_PRODUCERS; i++) {
        ret = pthread_join(p_list[i].thread_id, NULL);
        if (ret != 0) handle_error("pthread_join");
    }
    for (i = 0; i < NUM_CONSUMERS; i++) {
        ret = pthread_join(c_list[i].thread_id, NULL);
        if (ret != 0) handle_error("pthread_join");
    }
    free(p_list);
    free(c_list);
    return EXIT_SUCCESS;
}
