// Offline phone simulator for decoder-paced video acknowledgements.
#define main existing_protocol_test_main
#include "test_session_protocol.cpp"
#undef main

struct VideoFrames {
    pthread_mutex_t mutex;
    unsigned count;
};

static void on_video(uint32_t, std::vector<uint8_t>& frame, size_t offset, size_t length, uint64_t pts, void* ctx) {
    require(offset <= frame.size() && length <= frame.size() - offset && length == 5 && pts == 42 &&
            frame[offset + 4] == 0x65, "video callback preserves frame slice and timestamp");
    VideoFrames* frames = static_cast<VideoFrames*>(ctx);
    pthread_mutex_lock(&frames->mutex);
    ++frames->count;
    pthread_mutex_unlock(&frames->mutex);
}

static void wait_video(VideoFrames& frames, unsigned count) {
    for (unsigned i = 0; i < 200; ++i) {
        pthread_mutex_lock(&frames.mutex);
        bool ready = frames.count >= count;
        pthread_mutex_unlock(&frames.mutex);
        if (ready) return;
        usleep(10000);
    }
    require(0, "video callback deadline");
}

static void no_ack(int fd) {
    fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
    struct timeval timeout = { 0, 50000 };
    require(select(fd + 1, &fds, NULL, NULL, &timeout) == 0, "video ack remains deferred");
}

int main() {
    alarm(20); signal(SIGPIPE, SIG_IGN);
    check((int)psa_crypto_init(), "PSA init");
    configure_identity();
    setenv("NBT_AA_VIDEO_ACK_FORCE_MS", "500", 1);
    nbt::AASession session;
    VideoFrames frames;
    require(pthread_mutex_init(&frames.mutex, NULL) == 0, "video mutex");
    frames.count = 0;
    session.setVideoFrameCallback(on_video, &frames);
    session.setDeferredVideoAcks(true);
    peer* server = (peer*)calloc(1, sizeof(peer));
    peer* carrier = (peer*)calloc(1, sizeof(peer));
    require(server && carrier, "allocate phone");
    server->other = carrier; initialize(server, 1, NULL);
    int fd = begin(session); negotiate(fd, server, carrier);
    Bytes start = msg(0x8001); start.push_back(8); start.push_back(7); start.push_back(16); start.push_back(0);
    send_encrypted(fd, server, carrier, 6, start);
    Bytes frame = msg(0); frame.resize(10, 0); frame[9] = 42;
    frame.push_back(0); frame.push_back(0); frame.push_back(0); frame.push_back(1); frame.push_back(0x65);

    send_encrypted(fd, server, carrier, 6, frame);
    wait_video(frames, 1); no_ack(fd);
    session.postVideoConsumed(1);
    require(message(receive_encrypted(fd, server, 6)) == 0x8004, "consumption releases ack");

    send_encrypted(fd, server, carrier, 6, frame);
    wait_video(frames, 2);
    require(message(receive_encrypted(fd, server, 6)) == 0x8004, "stalled frame is force-acked");
    session.postVideoConsumed(1);
    send_encrypted(fd, server, carrier, 6, frame);
    wait_video(frames, 3); no_ack(fd);
    session.postVideoConsumed(1);
    require(message(receive_encrypted(fd, server, 6)) == 0x8004, "late consumption does not ack next frame");

    session.stop(); close(fd);
    release(server); free(carrier);
    pthread_mutex_destroy(&frames.mutex);
    puts("AA_VIDEO_ACKS_OK deferred forced late-credit");
    return 0;
}
