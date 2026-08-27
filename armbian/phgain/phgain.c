/*
 * phgain - minimal realtime-safe stereo gain stage for the Move.
 *
 * The Move's master knob (MoveMaster, CC 79) is a relative encoder and the
 * XMOS implements no hardware volume, so master level has to be applied in
 * software. This sits between the appliance's audio output and
 * system:playback_{1,2} and scales it.
 *
 * Control: UDP datagrams on 127.0.0.1:7666, one ASCII float per packet in
 * 0.0 .. 1.0 ("0.75\n"). Received on a normal thread; the RT callback only
 * reads an atomic and slews towards it, so it never blocks or allocates.
 */
#include <jack/jack.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define CTRL_PORT 7666
#define SLEW      0.0015f      /* per-sample approach to the target */

static jack_client_t *client;
static jack_port_t *in_l, *in_r, *out_l, *out_r;
static _Atomic float target_gain = 1.0f;
static float cur_gain = 1.0f;
static volatile sig_atomic_t running = 1;

static int process(jack_nframes_t n, void *arg)
{
    (void)arg;
    const jack_default_audio_sample_t *il = jack_port_get_buffer(in_l, n);
    const jack_default_audio_sample_t *ir = jack_port_get_buffer(in_r, n);
    jack_default_audio_sample_t *ol = jack_port_get_buffer(out_l, n);
    jack_default_audio_sample_t *orr = jack_port_get_buffer(out_r, n);

    const float tgt = atomic_load_explicit(&target_gain, memory_order_relaxed);
    float g = cur_gain;
    for (jack_nframes_t i = 0; i < n; i++) {
        if (g < tgt) { g += SLEW; if (g > tgt) g = tgt; }
        else if (g > tgt) { g -= SLEW; if (g < tgt) g = tgt; }
        ol[i]  = il[i]  * g;
        orr[i] = ir[i] * g;
    }
    cur_gain = g;
    return 0;
}

static void *ctrl_thread(void *arg)
{
    (void)arg;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return NULL;
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(CTRL_PORT);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) { close(fd); return NULL; }
    char buf[64];
    while (running) {
        ssize_t k = recv(fd, buf, sizeof buf - 1, 0);
        if (k <= 0) continue;
        buf[k] = 0;
        float v = strtof(buf, NULL);
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        atomic_store_explicit(&target_gain, v, memory_order_relaxed);
    }
    close(fd);
    return NULL;
}

static void on_signal(int s) { (void)s; running = 0; }

/*
 * jackd owns /dev/ablspi0.0, so it gets restarted for reasons that have nothing
 * to do with us (the watchdog, a reinstall). Without this, losing the server
 * leaves the process ALIVE but with no ports: systemd sees Restart=always
 * satisfied because nothing exited, and the master knob silently stops working
 * with every service reporting "active". Exit instead, and let systemd
 * reconnect us.
 */
static void on_jack_shutdown(void *arg)
{
    (void)arg;
    fprintf(stderr, "phgain: JACK server went away — exiting so systemd restarts us\n");
    _exit(1);
}

int main(void)
{
    jack_status_t st;
    client = jack_client_open("phgain", JackNoStartServer, &st);
    if (!client) { fprintf(stderr, "phgain: cannot connect to JACK\n"); return 1; }

    in_l  = jack_port_register(client, "in_1",  JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    in_r  = jack_port_register(client, "in_2",  JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    out_l = jack_port_register(client, "out_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    out_r = jack_port_register(client, "out_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (!in_l || !in_r || !out_l || !out_r) { fprintf(stderr, "phgain: port register failed\n"); return 1; }

    jack_set_process_callback(client, process, NULL);
    jack_on_shutdown(client, on_jack_shutdown, NULL);
    if (jack_activate(client)) { fprintf(stderr, "phgain: activate failed\n"); return 1; }

    jack_connect(client, "phgain:out_1", "system:playback_1");
    jack_connect(client, "phgain:out_2", "system:playback_2");

    pthread_t th;
    pthread_create(&th, NULL, ctrl_thread, NULL);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    fprintf(stderr, "phgain: up, control udp 127.0.0.1:%d\n", CTRL_PORT);
    while (running) sleep(1);
    jack_client_close(client);
    return 0;
}
